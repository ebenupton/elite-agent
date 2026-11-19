// The Electron in the page's machine (web/core_wasm.c calls these when the page asks for an Electron): elk.c's
// machine, the player's view of it, its keyboard as the player presses BBC Micro keys, its picture and its sound.
#include <stdlib.h>
#include <string.h>
#include "elk.h"
#include "../../cplayer/elite_ver.h"
#include "../../cplayer/elite_ver_elk.h"

extern uint8_t *wasm_ram(void);
extern int wasm_init(int mode, int machine);
extern void wasm_frame(void);

static struct elk *E;
static int16_t s_audio[625];

// ---- the player's view: main RAM, and the windows of sideways RAM the address table names (gen-ev.mjs), each from
// its bank's slot as the loader recorded it, whatever is paged
static const elk_window_t WINDOWS[] = EV_ELK_WINDOWS;
static void view(uint8_t *ram)
{
    memcpy(ram, E->ram, 0x8000);
    memset(ram + 0x3000, 0, 0x5000);
    for (size_t i = 0; i < sizeof WINDOWS / sizeof WINDOWS[0]; i++) {
        const elk_window_t *w = &WINDOWS[i];
        const uint8_t *img = E->slot[E->ram[w->slot_at] & 15];
        if (img && w->src >= 0x8000 && w->src + w->len <= 0xc000) memcpy(ram + w->view, img + (w->src - 0x8000), w->len);
    }
}

// ---- the keyboard: the player presses the BBC Micro's keys (internal key numbers, row << 4 | column). Each is the
// Electron key the port's own table gives for it (kbkey in its flight code: the matrix position of each BBC key the
// Electron's MOS reports, as measured on MOS 1.00); a function key is FUNC and its digit, as on Acornsoft's Electron
// Elite and the port; @ is SHIFT and 0 (the port's docked code); TAB, the energy bomb's key, has no Electron key.
static uint8_t BBC_TO_ELK[128][2];
static uint8_t s_held[0xe0];            // how many BBC keys hold each Electron key
static void keys_init(void)
{
    static const uint8_t KBKEY[128] = {
        0xD8, 0xD4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xC2, 0xA1, 0x91, 0x81, 0x00, 0x51, 0x00, 0x21, 0x11, 0x11, 0, 0, 0, 0, 0, 0,
        0x00, 0xB2, 0xA2, 0x82, 0x61, 0x52, 0x41, 0x31, 0x12, 0x12, 0, 0, 0, 0, 0, 0,
        0xC1, 0xB1, 0xA4, 0x92, 0x71, 0x62, 0x42, 0x32, 0x02, 0x22, 0, 0, 0, 0, 0, 0,
        0xD2, 0xC4, 0xB8, 0x94, 0x72, 0x64, 0x54, 0x31, 0x24, 0x14, 0, 0, 0, 0, 0, 0,
        0x00, 0xB4, 0xA8, 0x84, 0x74, 0x78, 0x44, 0x34, 0x02, 0x18, 0, 0, 0, 0, 0, 0,
        0x00, 0xC8, 0x08, 0x98, 0x88, 0x68, 0x58, 0x48, 0x38, 0x02, 0, 0, 0, 0, 0, 0,
        0xD1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0, 0, 0, 0, 0, 0,
    };
    // (the BBC's internal numbers of f0-f9, and the Electron's digits)
    static const uint8_t FKEY[10] = {0x20, 0x71, 0x72, 0x73, 0x14, 0x74, 0x75, 0x16, 0x76, 0x77};
    static const uint8_t DIGIT[10] = {ELK_0, ELK_1, ELK_2, ELK_3, ELK_4, ELK_5, ELK_6, ELK_7, ELK_8, ELK_9};
    memset(BBC_TO_ELK, 0, sizeof BBC_TO_ELK);
    for (int k = 0; k < 128; k++) BBC_TO_ELK[k][0] = KBKEY[k];
    for (int d = 0; d < 10; d++) { BBC_TO_ELK[FKEY[d]][0] = ELK_FUNC; BBC_TO_ELK[FKEY[d]][1] = DIGIT[d]; }
    BBC_TO_ELK[0x47][0] = ELK_SHIFT; BBC_TO_ELK[0x47][1] = ELK_0;
    memset(s_held, 0, sizeof s_held);
}
static void key(int bbc, int down)
{
    if (bbc < 0 || bbc >= 128) return;
    for (int i = 0; i < 2; i++) {
        uint8_t k = BBC_TO_ELK[bbc][i];
        if (!k) continue;
        if (down) { if (s_held[k]++ == 0) elk_key(E, k, 1); }
        else if (s_held[k] && --s_held[k] == 0) elk_key(E, k, 0);
    }
}

// ---- the page's calls
// roms: the MOS, BASIC and the ACP 1770 DFS, 16 KB each (kept); image: an .ssd. Boots it as Clock Signal does a disc
// (a power-on reset a second in with SHIFT held: the disc's !BOOT)
int elk_core_boot(const uint8_t **roms, const uint8_t *image, uint32_t size)
{
    if (!E) E = calloc(1, sizeof *E);
    elk_init(E, roms[0], roms[1], roms[2]);
    elk_insert_ssd(E, image, size);
    elk_power_on(E, 1);
    keys_init();
    return 1;
}

static void frames(int n) { for (int i = 0; i < n; i++) elk_run(E, E->now + 40000); }

// to the game's first title screen (the loader takes about 14 s, the Plus 3 reading its banks), skew frames more;
// then the player. Returns 0 if the game wasn't recognised
int elk_core_start(int skew, int mode)
{
    frames(600);
    int found = 0;
    for (int i = 0; i < 40 && !found; i++) { view(wasm_ram()); if (elite_detect(EV_MODEL_ELECTRON, wasm_ram())) found = 1; else frames(50); }
    if (!found) return 0;
    // (the title: QQ12 set, QQ11 0, its Cobra Mk III)
    for (int i = 0; i < 200; i++) {
        const uint8_t *r = E->ram;
        if (r[g_ev.A_QQ12] && !r[g_ev.A_QQ11] && r[g_ev.A_TYPE] == g_ev.title_type[0]) break;
        frames(5);
    }
    frames(skew);
    view(wasm_ram());
    return wasm_init(mode, EV_MODEL_ELECTRON);
}

// one frame: the player decides on this frame's view, its keys go to the machine, the machine runs 20 ms. draw: the
// field the frame starts showing, into px (RENDER-sized, colour numbers 0-7), kept from the last if none started
void elk_core_frame(int draw, uint8_t *px)
{
    view(wasm_ram());
    wasm_frame();
    if (draw) elk_want_picture(E);
    frames(1);
    if (draw && E->have_px && px) memcpy(px, E->px, ELK_RENDER_W * ELK_RENDER_H);
}

void elk_core_key(int code, int down) { key(code, down); }

// the tone channel's last frame at 31,250 Hz
int16_t *elk_core_audio(void) { elk_audio(E, s_audio, 625, 64, 4000); return s_audio; }

struct elk *elk_core(void) { return E; }
