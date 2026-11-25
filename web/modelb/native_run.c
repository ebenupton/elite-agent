// The page's machine and player (core_wasm.c, player_wasm.c) built natively, with the Model B: a career on Solid Elite
// for the BBC Micro in Curious mode (or MODE=0/1), its log on stdout (LOG=2 for more), PPM screenshots every SHOT
// frames (mb<frame>.ppm), KEYS=1 shows the keys. Build: build.sh.
//   native_run <roms dir: os12.rom, basic2.rom, dfs226.rom> <disc> <frames>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
int core_mb_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
int core_start(int skew, int mode);
void core_frame(int draw);
void core_key(int code, int down);
uint32_t *core_rgba(void);
int core_width(void), core_height(void);
uint32_t wasm_frame_no(void);
double wasm_tally(void);
int32_t wasm_cash(void);
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
    static const char *names[] = {"os12.rom", "basic2.rom", "dfs226.rom"};
    const uint8_t *roms[3]; size_t n;
    for (int i = 0; i < 3; i++) { char p[512]; snprintf(p, sizeof p, "%s/%s", argv[1], names[i]); roms[i] = slurp(p, &n); }
    uint8_t *disc = slurp(argv[2], &n);
    core_mb_boot(roms, disc, (uint32_t) n);
    int ok = core_start(getenv("SKEW") ? atoi(getenv("SKEW")) : 13, getenv("MODE") ? atoi(getenv("MODE")) : 2);
    printf("core_start: %d\n", ok);
    if (!ok) return 1;
    int frames = atoi(argv[3]), shot = getenv("SHOT") ? atoi(getenv("SHOT")) : 0;
    clock_t t0 = clock();
    for (int f = 1; f <= frames; f++) {
        core_frame(shot && f % shot == 0);
        if (shot && f % shot == 0) {
            char p[64]; snprintf(p, sizeof p, "mb%07d.ppm", f);
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
