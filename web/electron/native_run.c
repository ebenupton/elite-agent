// The page's machine and player (core_wasm.c, player_wasm.c) built natively, with the Electron: a career on Solid Elite
// for the Electron in Curious mode (or MODE=0/1), its log on stdout (LOG=2 for more), PPM screenshots every SHOT
// frames; KEYS=1 shows the keys, PEEK=n a few of the game's bytes every n frames. Build: build.sh.
//   native_run <electron roms dir> <disc> <frames>
// (the ROMs dir as elkh's: os.rom, basic.rom, DFS-1770-2.20.rom)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
int core_elk_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
int core_start(int skew, int mode);
void core_frame(int draw);
void core_key(int code, int down);
uint32_t *core_rgba(void);
int core_width(void), core_height(void);
uint32_t wasm_frame_no(void);
double wasm_tally(void);
int32_t wasm_cash(void);
extern uint8_t *wasm_ram(void);
#include "elk.h"
struct elk *elk_core(void);
// (TRACEPC=hex[,hex...]: each opcode fetch at one of these, logged with the frame)
static unsigned s_trace[8]; static int s_ntrace;
static FILE *s_keylog;
static void keyhook(struct elk *e, int code, int down) { fprintf(s_keylog, "%llu %d %d\n", (unsigned long long) e->now, code, down); }
static void trace(struct elk *e, uint16_t pc, uint64_t cyc) {
    for (int i = 0; i < s_ntrace; i++) if (pc == s_trace[i]) printf("%7u pc %04x (slot %d) at %llu\n", wasm_frame_no(), pc, e->active, (unsigned long long) cyc);
}
static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = (size_t) ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = calloc(1, *n + 0x4000); if (fread(b, 1, *n, f) != *n) exit(1); fclose(f); return b;
}
void js_key(int code, int down) { if (getenv("KEYS")) printf("%7u key %02x %d\n", wasm_frame_no(), code, down); core_key(code, down); }
void js_analogue(int ch, int v) { (void) ch; (void) v; }
void js_log(int level, const char *line, int len) { if (level <= (getenv("LOG") ? atoi(getenv("LOG")) : 1)) printf("%7u %.*s\n", wasm_frame_no(), len, line); }
void js_note(const char *json, int len) { if (!getenv("NONOTE")) printf("%7u NOTE %.*s\n", wasm_frame_no(), len, json); }
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "native_run <roms dir> <disc> <frames>\n"); return 1; }
    static const char *names[] = {"os.rom", "basic.rom", "DFS-1770-2.20.rom"};
    const uint8_t *roms[3]; size_t n;
    for (int i = 0; i < 3; i++) { char p[512]; snprintf(p, sizeof p, "%s/%s", argv[1], names[i]); roms[i] = slurp(p, &n); }
    uint8_t *disc = slurp(argv[2], &n);
    core_elk_boot(roms, disc, (uint32_t) n);
    int mode = getenv("MODE") ? atoi(getenv("MODE")) : 2;
    int ok = core_start(getenv("SKEW") ? atoi(getenv("SKEW")) : 13, mode);
    printf("core_start: %d\n", ok);
    if (!ok) return 1;
    if (getenv("TRACEPC")) {
        for (char *q = getenv("TRACEPC"); q && s_ntrace < 8; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) s_trace[s_ntrace++] = (unsigned) strtoul(q, 0, 16);
        elk_core()->fetch_hook = trace;
    }
    // (KEYLOG=file: each Electron key going down or up, with the cycle: replay.mjs plays them into clkh)
    static FILE *keylog;
    if (getenv("KEYLOG")) { keylog = fopen(getenv("KEYLOG"), "w"); s_keylog = keylog; elk_core()->key_hook = keyhook; }
    int frames = atoi(argv[3]), shot = getenv("SHOT") ? atoi(getenv("SHOT")) : 0;
    clock_t t0 = clock();
    for (int f = 1; f <= frames; f++) {
        int draw = shot && (f % shot == 0 || f % shot == shot - 1);
        core_frame(draw);
        if (getenv("PEEK") && f % atoi(getenv("PEEK")) == 0) {
            uint8_t *r = wasm_ram();
            printf("%7d QQ11 %02x QQ12 %02x MCNT %02x DELTA %02x VIEW %02x ENERGY %02x pc %04x\n", f, r[0x87], r[0x8e], r[0x8a], r[0x7d], r[0x345], r[0x3a7], elk_core()->cpu.pc);
            if (getenv("PEEKKEYS")) { struct elk *e = elk_core(); printf("        keys"); for (int i = 0; i < 14; i++) printf(" %x", e->keys[i]); printf(" ier %02x isr %02x mode %d rom %d\n", e->ier, e->isr, e->mode, e->active); }
        }
        // (DUMP=frame:addr:len: the player's view there, in hex)
        if (getenv("DUMP")) {
            unsigned df = 0, da = 0, dl = 0;
            for (char *q = getenv("DUMP"); q && sscanf(q, "%u:%x:%u", &df, &da, &dl) == 3; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
                if ((unsigned) f == df) { printf("%7d @%llu %04x:", f, (unsigned long long) elk_core()->now, da); for (unsigned i = 0; i < dl; i++) printf(" %02x", wasm_ram()[da + i]); printf("\n"); }
        }
        // (SNAP=frame:file: the machine saved there, for elkh's load)
        if (getenv("SNAP")) { unsigned sf = 0; char sp[256]; if (sscanf(getenv("SNAP"), "%u:%255s", &sf, sp) == 2 && (unsigned) f == sf) elk_save(elk_core(), sp); }
        if (shot && f % shot == 0) {
            char p[64]; snprintf(p, sizeof p, "elk%07d.ppm", f);
            FILE *o = fopen(p, "wb"); int W = core_width(), H = core_height(); fprintf(o, "P6 %d %d 255\n", W, H);
            uint32_t *px = core_rgba();
            for (int i = 0; i < W * H; i++) { uint8_t c[3] = {px[i] & 255, (px[i] >> 8) & 255, (px[i] >> 16) & 255}; fwrite(c, 1, 3, o); }
            fclose(o);
        }
    }
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    printf("tally %.2f cash %.1f after %d frames (%.1f game minutes) in %.1f s: %.1fx real time\n", wasm_tally(), wasm_cash() / 10.0,
           frames, frames / 3000.0, secs, frames / 50.0 / (secs > 0 ? secs : 1));
    return 0;
}
