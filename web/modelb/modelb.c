// A BBC Model B for the page (?game=modelb): Kieran Connell's Master core (machine.c and what it includes: the 6502,
// the CRTC, the VIAs, the 1770 and its disc) built a second time with MACHINE_MODEL_B (kc-beeb-modelb.patch), under
// its own names, beside the Master's. MOS 1.20, BASIC II in slot 15, Acorn's 1770 DFS in slot 14 behind an Acorn 1770
// interface at &FE80, sideways RAM in slots 0-7 (as jsbeeb's Model B), the NMOS 6502; the CRTC, the video ULA (its clock switched in mid-line
// as the Master's is), the VIAs, the keyboard and the sound chip are the Master core's own. The page's calls are at the
// end (core_wasm.c calls them when the page asks for a Model B), with the player's view of the machine.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// (the machine's global names, so the Master's in core_wasm.c and these can be linked together)
#define cpu6502_get_p mb_cpu6502_get_p
#define cpu6502_map_flat mb_cpu6502_map_flat
#define cpu6502_reset mb_cpu6502_reset
#define cpu6502_run mb_cpu6502_run
#define cpu6502_set_p mb_cpu6502_set_p
#define crtc_read mb_crtc_read
#define crtc_reset mb_crtc_reset
#define crtc_set_ula mb_crtc_set_ula
#define crtc_write mb_crtc_write
#define disc_crc_add mb_disc_crc_add
#define disc_crc_init mb_disc_crc_init
#define disc_fm_to_pulses mb_disc_fm_to_pulses
#define disc_init mb_disc_init
#define disc_load_ssd mb_disc_load_ssd
#define disc_mfm_to_pulses mb_disc_mfm_to_pulses
#define disc_pulses_to_fm mb_disc_pulses_to_fm
#define disc_pulses_to_mfm mb_disc_pulses_to_mfm
#define disc_read_sector mb_disc_read_sector
#define fdc_advance mb_fdc_advance
#define fdc_break_reset mb_fdc_break_reset
#define fdc_init mb_fdc_init
#define fdc_power_on_reset mb_fdc_power_on_reset
#define fdc_read mb_fdc_read
#define fdc_write mb_fdc_write
#define fdc_ticks_to_event mb_fdc_ticks_to_event
#define machine_autoboot mb_machine_autoboot
#define machine_init mb_machine_init
#define machine_profile_skip mb_machine_profile_skip
#define machine_queue_key mb_machine_queue_key
#define machine_reset mb_machine_reset
#define machine_run mb_machine_run
#define machine_set_analogue mb_machine_set_analogue
#define machine_set_disc mb_machine_set_disc
#define machine_set_key mb_machine_set_key

#define MACHINE_MODEL_B 1
#define RENDER_INDEXED 1
void core_sn_write(uint8_t byte);           // (core_wasm.c: the one SN76489, the Master's or this machine's)
#define MACHINE_SOUND_WRITE(m, byte) core_sn_write(byte)
// (the native tools: each opcode fetch to a hook, with the cycle count; undocumented opcodes counted)
#ifdef MB_TRACE
void mb_trace(uint16_t pc, uint32_t cyc, uint32_t a, uint32_t x, uint32_t y, uint32_t s, uint32_t nzc);
#define CPU6502_TRACE(cpu) mb_trace((cpu)->pc, (cpu)->cyc, (cpu)->a, (cpu)->x, (cpu)->y, (cpu)->s, \
                                    ((cpu)->n ? 4 : 0) | ((cpu)->z ? 2 : 0) | ((cpu)->c ? 1 : 0))
#endif
static uint32_t s_undoc;
static uint16_t s_undoc_pc;
#define CPU6502_UNDOC(cpu) (s_undoc++, s_undoc_pc = (uint16_t) ((cpu)->pc - 1))
#include "machine.c"
#include "render.h"
#include "../../cplayer/elite_ver.h"
#include "../../cplayer/elite_ver_mb.h"

static struct machine *M;
static struct disc s_disc;

// ---- the machine
// roms: MOS 1.20, BASIC II and the 1770 DFS, 16 KB each (kept); image: an .ssd. Power on with SHIFT held until
// 3,000,000 cycles (the Master core's autoboot, as beebjit's): the disc's !BOOT
int mb_boot(const uint8_t **roms, const uint8_t *image, uint32_t size)
{
    if (!M) {
        M = calloc(1, sizeof *M);
        machine_init(M);
        for (int i = 0; i < MACHINE_SWRAM_SLOTS; i++) M->swram[i] = calloc(0x4000, 1);
        for (int i = 0; i < 4; i++) s_swram_lo[i] = calloc(0x4000, 1);
        uint32_t *pulses = calloc((size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, sizeof(uint32_t));
        disc_init(&s_disc, pulses, 1, 1);
    } else {
        uint8_t *sw[MACHINE_SWRAM_SLOTS];
        memcpy(sw, M->swram, sizeof sw);
        machine_init(M);
        memcpy(M->swram, sw, sizeof sw);
    }
    M->mos = roms[0];
    M->rom[15] = roms[1];
    M->rom[14] = roms[2];
    M->vs = NULL;
    machine_reset(M);
    for (int i = 0; i < 4; i++) memset(s_swram_lo[i], 0, 0x4000);
    disc_load_ssd(&s_disc, image, (size + 255) & ~255u);
    machine_set_disc(M, 0, &s_disc);
    machine_autoboot(M);
    return 1;
}
struct machine *mb_machine(void) { return M; }
// (the native tools: no SHIFT, so no disc boot: BASIC's prompt)
void mb_noboot(void) { M->key_head = M->key_count = 0; machine_set_key(M, 0, 0, 0); }
void mb_run(uint64_t cycles) { machine_run(M, M->cpu.cycles + cycles); }
uint32_t mb_undoc(uint16_t *pc) { if (pc) *pc = s_undoc_pc; return s_undoc; }
// a key of the BBC matrix (row << 4 | column, the player's codes), a little after now, as the Master's beeb_key
void mb_key(int code, int down) { machine_queue_key(M, M->synced + 2000, (code >> 4) & 7, code & 15, down); }
void mb_analogue(int channel, int value) { machine_set_analogue(M, channel, (uint16_t) value); }
// a slot's 16 KB (sideways RAM 4-7, a ROM, or NULL)
const uint8_t *mb_slot(int s)
{
    s &= 15;
    if (s >= 4 && s < 4 + MACHINE_SWRAM_SLOTS) return M->swram[s - 4];
    if (s < 4) return s_swram_lo[s];
    return M->rom[s];
}

// ---- the picture: a frame with the video stream on, through the core's renderer (core_wasm.c's render.c), as
// libbeeb.c's beeb_shot
static struct vstream s_stream;
static struct render s_render;
static render_px *s_canvas;
static int32_t s_vofs;
static void consume(void)
{
    struct vstream *vs = &s_stream;
    while (vs_available(vs) > 0) {
        uint16_t w = vs->ring[vs->tail & (VS_RING - 1)];
        if (!(w & VS_EVENT)) {
            int n = 0;
            while (n < vs_available(vs) && !(vs->ring[(vs->tail + n) & (VS_RING - 1)] & VS_EVENT)) n++;
            render_consume_n(&s_render, vs, n);
            continue;
        }
        vs->tail++;
        render_word(&s_render, w);
    }
}
void mb_shot(uint8_t *out, uint64_t cycles)
{
    if (!s_canvas) {
        s_canvas = calloc(RENDER_WIDTH * RENDER_HEIGHT, sizeof(render_px));
        render_init(&s_render, s_canvas);
    }
    memset(&s_stream, 0, sizeof s_stream);
    // (the renderer's beam where the raster is: the lines since the last vertical sync, nothing drawn until the next
    // horizontal sync, less the row the renderer starts an odd field on, as it did last time (s_vofs). Its own count would be stale after frames without a stream, and the game's field is two CRTC
    // frames, the first without a vertical sync, so the renderer would place up to a field wrongly)
    s_render.vert_beam_pos = (int32_t) (2 * s_hsyncs) + s_vofs;
    s_render.horiz_beam_pos = 4096;
    s_render.render_pos = RENDER_LINE_END;
    s_render.span_start = s_render.span_end = 0;
    for (int i = 0; i < 16; i++) render_word(&s_render, VS_EV(VS_ULAPAL, (i << 4) | M->ula_pal[i]));
    render_word(&s_render, VS_EV(VS_ULACTRL, M->ula_ctrl));
    M->vs = &s_stream;
    uint64_t until = M->cpu.cycles + cycles;
    while (M->cpu.cycles < until) {
        machine_run(M, M->cpu.cycles + 2000 < until ? M->cpu.cycles + 2000 : until);
        consume();
    }
    M->vs = NULL;
    s_vofs = s_render.vert_beam_pos - (int32_t) (2 * s_hsyncs);
    if (s_vofs < -1 || s_vofs > 0) s_vofs = 0;
    if (out) for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) out[i] = (uint8_t) s_canvas[i];
}

// ---- the page's calls (core_wasm.c)
extern uint8_t *wasm_ram(void);
extern int wasm_init(int mode, int machine);
extern void wasm_frame(void);

// the player's view: main RAM, except &3000-&7FFF (the screens, the dashboard and hot code, which it never reads),
// where the windows of sideways RAM the address table names (gen-ev.mjs) are copied, each from its bank's slot as the
// loader recorded it, whatever is paged
static const ev_window_t WINDOWS[] = EV_MB_WINDOWS;
static void view(uint8_t *ram)
{
    memcpy(ram, M->ram, 0x8000);
    memset(ram + 0x3000, 0, 0x5000);
    for (size_t i = 0; i < sizeof WINDOWS / sizeof WINDOWS[0]; i++) {
        const ev_window_t *w = &WINDOWS[i];
        const uint8_t *img = mb_slot(M->ram[w->slot_at]);
        if (img && w->src >= 0x8000 && w->src + w->len <= 0xc000) memcpy(ram + w->view, img + (w->src - 0x8000), w->len);
    }
}

static void frames(int n) { for (int i = 0; i < n; i++) mb_run(40000); }

// to the game's first title screen, skew frames more, then the player. Returns 0 if the game wasn't recognised
int mb_core_start(int skew, int mode)
{
    frames(700);
    int found = 0;
    for (int i = 0; i < 40 && !found; i++) { view(wasm_ram()); if (elite_detect(EV_MODEL_BBC_B, wasm_ram())) found = 1; else frames(50); }
    if (!found) return 0;
    // (the title: QQ12 set, QQ11 0, its Cobra Mk III)
    for (int i = 0; i < 200; i++) {
        const uint8_t *r = M->ram;
        if (r[g_ev.A_QQ12] && !r[g_ev.A_QQ11] && r[g_ev.A_TYPE] == g_ev.title_type[0]) break;
        frames(5);
    }
    frames(skew);
    view(wasm_ram());
    return wasm_init(mode, EV_MODEL_BBC_B);
}

// one frame: the player decides on this frame's view, its keys go to the machine, the machine runs 40,000 cycles;
// draw: through the renderer into px (RENDER-sized colour numbers)
void mb_core_frame(int draw, uint8_t *px)
{
    view(wasm_ram());
    wasm_frame();
    if (draw) mb_shot(px, 40000);
    else mb_run(40000);
}

// (for the native tools)
uint64_t mb_cycles(void) { return M->cpu.cycles; }
const uint8_t *mb_ram(void) { return M->ram; }
void mb_regs(char *buf, int n)
{
    struct cpu6502 *c = &M->cpu;
    snprintf(buf, (size_t) n, "pc %x a %x x %x y %x s %x cycles %llu romsel %x", c->pc, c->a, c->x, c->y, c->s,
             (unsigned long long) c->cycles, M->romsel);
}
