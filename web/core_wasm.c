// The whole machine as WebAssembly: Kieran Connell's Master core (the Presto's and run_sim's, sim/libbeeb.c) with the player
// beside it (web/player_wasm.c). One binary for the browser and for Node, and it steps exactly as run_sim's step() does:
// the player decides on this frame's RAM, the keys due go to the machine, the machine runs 40,000 cycles.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sn76489.h"

static struct sn76489 s_sn;
#define MACHINE_SOUND_WRITE(m, byte) sn_write(&s_sn, (byte))
#include "../sim/libbeeb.c"
#include "sn76489.c"

#define EXPORT(name) __attribute__((export_name(name)))

// from player_wasm.c
extern uint8_t *wasm_ram(void);
extern int wasm_init(int mode, int machine);
extern void wasm_frame(void);
extern int elite_detect(int model, const uint8_t *ram);

static beeb_t *B;

// ---- the 6502 Second Processor (tube/tube.c), for Solid Elite's Tube version: the game is on the parasite, so the
// player's RAM is the parasite's &0000-&7FFF, with the game's own zero page while the frame layer borrows it
void tube_init(const uint8_t *rom2k);
uint8_t *tube_mem(void);
extern int tube_on, tube_zp_held;
extern uint16_t tube_hook_on, tube_hook_off;
static int s_zp_fix;
// (the pinned disc's parasite symbols, tube/build/psyms.txt at d52896e: F_FRAME keeps &06-&27 and FZ's &D2-&E1 in f_zsv
// from its BIT GMODE to the JSR p_flip after it puts them back; found in its code, so a disc built otherwise is left alone)
enum { T_F_FRAME = 0x7FCE, T_FF_STARS9 = 0x814F, T_GMODE = 0x1400, T_P_FLIP = 0x1BFD, T_F_ZSV = 0x1FD7 };
static void tube_find_hooks(void) {
    const uint8_t *pm = tube_mem();
    int on = 0, off = 0;
    for (int a = T_F_FRAME; a < T_F_FRAME + 0x140 && !on; a++)
        if (pm[a] == 0x2C && pm[a + 1] == (T_GMODE & 255) && pm[a + 2] == T_GMODE >> 8) on = a;
    for (int a = T_FF_STARS9; a < T_FF_STARS9 + 0x140 && !off; a++)
        if (pm[a] == 0x20 && pm[a + 1] == (T_P_FLIP & 255) && pm[a + 2] == T_P_FLIP >> 8) off = a;
    if (on && off) { tube_hook_on = (uint16_t) on; tube_hook_off = (uint16_t) off; s_zp_fix = 1; }
}
EXPORT("core_tube_hooks") int core_tube_hooks(void) { return s_zp_fix ? tube_hook_on << 16 | tube_hook_off : 0; }
static void agent_ram(uint8_t *ram) {
    if (!tube_on) { memcpy(ram, beeb_ram(B), 0x8000); return; }
    const uint8_t *pm = tube_mem();
    memcpy(ram, pm, 0x8000);
    if (s_zp_fix && tube_zp_held) { memcpy(ram + 0x06, pm + T_F_ZSV, 0x22); memcpy(ram + 0xD2, pm + T_F_ZSV + 0x22, 16); }
}
// a parasite on the Tube, its 2 KB client ROM (in the page's memory): after core_boot, before core_start
EXPORT("core_tube") void core_tube(const uint8_t *rom) { tube_init(rom); }
EXPORT("core_tube_mem") uint8_t *core_tube_mem(void) { return tube_mem(); }
static uint8_t *s_px;
static uint32_t *s_rgba;
static int16_t s_audio[640];

// ---- an Acorn Electron instead (electron/core_elk.c), for Solid Elite for the Electron: core_elk_boot in place of
// core_boot, then the same calls
int elk_core_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
int elk_core_start(int skew, int mode);
void elk_core_frame(int draw, uint8_t *px);
void elk_core_key(int code, int down);
int16_t *elk_core_audio(void);
static int s_elk;
// roms: 3 pointers of 16 KB each, the Electron's MOS 1.00, BASIC II and the ACP 1770 DFS, in the page's memory (kept)
EXPORT("core_elk_boot") int core_elk_boot(const uint8_t **roms, const uint8_t *image, uint32_t size) {
    s_elk = 1;
    return elk_core_boot(roms, image, size);
}

// ---- a BBC Model B instead (modelb/modelb.c: the Master core built again as a Model B), for Solid Elite for the BBC
// Micro: core_mb_boot in place of core_boot, then the same calls. Its sound chip is this one
int mb_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
int mb_core_start(int skew, int mode);
void mb_core_frame(int draw, uint8_t *px);
void mb_key(int code, int down);
void mb_analogue(int channel, int value);
static int s_mb;
void core_sn_write(uint8_t byte) { sn_write(&s_sn, byte); }
// roms: 3 pointers of 16 KB each, the Model B's MOS 1.20, BASIC II and Acorn's 1770 DFS, in the page's memory (kept)
EXPORT("core_mb_boot") int core_mb_boot(const uint8_t **roms, const uint8_t *image, uint32_t size) {
    s_mb = 1;
    sn_reset(&s_sn);
    return mb_boot(roms, image, size);
}

const uint8_t *mb_ram(void);
EXPORT("core_mb_ram") const uint8_t *core_mb_ram(void) { return mb_ram(); }     // (the Model B's main RAM: modelb/jsagent.mjs)

EXPORT("core_alloc") void *core_alloc(uint32_t n) { return calloc(1, n); }

// roms: 8 pointers of 16 KB each, the MOS then ROM slots 9 to 15, in the page's own memory (kept)
EXPORT("core_boot") int core_boot(const uint8_t **roms, const uint8_t *image, uint32_t size) {
    B = calloc(1, sizeof *B);
    struct machine *m = &B->m;
    machine_init(m);
    for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) m->swram[i] = calloc(0x4000, 1);
    m->mos = roms[0];
    for (int i = 0; i < 7; i++) m->rom[0x9 + i] = roms[1 + i];
    m->vs = NULL;
    machine_reset(m);
    uint32_t *pulses = calloc((size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, sizeof(uint32_t));
    disc_init(&B->disc, pulses, 1, 1);
    disc_load_ssd(&B->disc, image, (size + 255) & ~255u);
    machine_set_disc(m, 0, &B->disc);
    machine_autoboot(m);
    sn_reset(&s_sn);
    return 1;
}

// the boot as run_sim's: to the title screen, a game that loads for longer waited for, skew frames more; then the player.
// Returns 0 if it isn't an Elite the player knows
EXPORT("core_start") int core_start(int skew, int mode) {
    if (s_elk) return elk_core_start(skew, mode);
    if (s_mb) return mb_core_start(skew, mode);
    beeb_frames(B, 700);
    for (int i = 0; i < 40; i++) { agent_ram(wasm_ram()); if (elite_detect(beeb_model(), wasm_ram())) break; beeb_frames(B, 50); }
    // (the Tube: the frame layer's code in, the parasite's code all loaded)
    for (int i = 0; tube_on && i < 40 && (tube_find_hooks(), !s_zp_fix); i++) beeb_frames(B, 50);
    // (and its first title screen up, as the Master's games are after the 700 frames: QQ12 set, QQ11 0, distaway 200)
    for (int i = 0; tube_on && i < 40 && !(tube_mem()[0x87] && !tube_mem()[0x81] && tube_mem()[0xC29] == 200); i++) beeb_frames(B, 50);
    beeb_frames(B, skew);
    agent_ram(wasm_ram());
    return wasm_init(mode, beeb_model());
}

// one frame. draw: this frame through the renderer, into the indexed picture (core_rgba() gives it in colour)
EXPORT("core_frame") void core_frame(int draw) {
    if (s_elk) {
        if (draw && !s_px) s_px = calloc(1, (size_t) RENDER_WIDTH * RENDER_HEIGHT);
        elk_core_frame(draw, s_px);
        return;
    }
    if (s_mb) {
        if (draw && !s_px) s_px = calloc(1, (size_t) RENDER_WIDTH * RENDER_HEIGHT);
        mb_core_frame(draw, s_px);
        return;
    }
    agent_ram(wasm_ram());
    wasm_frame();
    if (draw) {
        if (!s_px) s_px = malloc((size_t) RENDER_WIDTH * RENDER_HEIGHT);
        beeb_shot(B, s_px, 1);
    } else beeb_frames(B, 1);
}

EXPORT("core_key") void core_key(int code, int down) { if (s_elk) elk_core_key(code, down); else if (s_mb) mb_key(code, down); else beeb_key(B, code, down); }
// (the Tube version reads the stick from JOPOS in the parasite's memory, but its host never sends it across: the host's
// IRQ1 keeps the ADC's readings in its own memory. As Solid Elite's own agent harness does (tube/tools/tmachine.mjs),
// the stick's high bytes go straight into the parasite's JOPOS, as a host that forwarded them would put them)
enum { T_JOPOS = 0x0CA7 };
EXPORT("core_analogue") void core_analogue(int channel, int value) {
    if (s_elk) return;          // (no analogue port on an Electron without a Plus 1; the port flies on keys)
    if (s_mb) { mb_analogue(channel, value); return; }
    beeb_analogue(B, channel, value);
    if (tube_on && channel >= 0 && channel < 3) tube_mem()[T_JOPOS + channel] = (uint8_t) (value >> 8);
}
EXPORT("core_width") int core_width(void) { return RENDER_WIDTH; }
EXPORT("core_height") int core_height(void) { return RENDER_HEIGHT; }

EXPORT("core_rgba") uint32_t *core_rgba(void) {
    static const uint32_t PAL[8] = {0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff, 0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff};
    if (!s_rgba) s_rgba = malloc((size_t) RENDER_WIDTH * RENDER_HEIGHT * 4);
    if (s_px) for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) s_rgba[i] = PAL[s_px[i] & 7];
    return s_rgba;
}

// a frame of the sound chip, as it stands now, at 31,250 Hz: 625 samples
EXPORT("core_audio") int16_t *core_audio(void) {
    if (s_elk) return elk_core_audio();
    for (int i = 0; i < 625; i++) s_audio[i] = (int16_t) (sn_render8(&s_sn) / 8);
    return s_audio;
}
