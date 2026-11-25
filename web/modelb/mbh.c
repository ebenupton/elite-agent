// mbh: this folder's Model B (modelb.c: the Master core built as a Model B) on the command line, for compare.mjs, which
// runs it beside jsbeeb (Solid Elite for the BBC Micro's own emulator, in its 6-harness).
//   mbh --rompath DIR [--media X.ssd] [--noboot] CMD...   (--noboot: no SHIFT at power-on)
//     cycles N | trace-all N (the next N instructions: "PC CYCLE") | trace-pc HEXPC N (each of the next N opcode fetches at PC: "hit PC CYCLE", the cycle the instruction
//     starts on) | hash (main RAM, FNV-1a) | peek HEXADDR LEN | peekrom SLOT HEXADDR LEN | regs | shot FILE.ppm
//     (two fields through the renderer) | undoc
// ROMs: os12.rom (MOS 1.20), basic2.rom (BASIC II, slot 15), dfs226.rom (Acorn's 1770 DFS 2.26, slot 14) in DIR.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define RENDER_INDEXED 1
#include "vstream.h"
#include "teletext.c"
#include "render.c"

int mb_boot(const uint8_t **roms, const uint8_t *image, uint32_t size);
void mb_run(uint64_t cycles);
void mb_shot(uint8_t *out, uint64_t cycles);
void mb_noboot(void);
const uint8_t *mb_slot(int s);
uint32_t mb_undoc(uint16_t *pc);
struct machine;
struct machine *mb_machine(void);
uint64_t mb_cycles(void);
const uint8_t *mb_ram(void);
void mb_regs(char *buf, int n);
void core_sn_write(uint8_t byte) { (void) byte; }
// (the Tube's hooks in the patched machine: no Tube here)
uint8_t tube_host_irq; int tube_on;
uint8_t tube_host_read(uint16_t a) { (void) a; return 0xfe; }
void tube_host_write(uint16_t a, uint8_t v) { (void) a; (void) v; }
void tube_par_run(uint64_t c) { (void) c; }
// (modelb.c's page calls, not used here)
uint8_t *wasm_ram(void) { static uint8_t r[0x8000]; return r; }
int wasm_init(int mode, int machine) { (void) mode; (void) machine; return 0; }
void wasm_frame(void) {}

static unsigned s_pc; static long s_left, s_all; static uint32_t s_last; static uint64_t s_hi;
void mb_trace(uint16_t pc, uint32_t cyc, uint32_t a, uint32_t x, uint32_t y, uint32_t sp, uint32_t nzc)
{
    if (cyc < s_last) s_hi += 1ull << 32;
    s_last = cyc;
    if (s_all > 0) { printf("%04x %llu %02x %02x %02x %02x %x\n", pc, (unsigned long long) (s_hi | cyc), a, x, y, sp, nzc); s_all--; }
    if (s_left > 0 && pc == s_pc) { printf("hit %04x %llu\n", pc, (unsigned long long) (s_hi | cyc)); s_left--; }
}

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = (size_t) ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = calloc(1, *n + 0x4000);
    if (fread(b, 1, *n, f) != *n) exit(1);
    fclose(f);
    return b;
}
static void hexdump(const uint8_t *p, unsigned n) { for (unsigned i = 0; i < n; i++) printf("%02x%c", p[i], i % 16 == 15 ? '\n' : ' '); if (n % 16) printf("\n"); }

int main(int argc, char **argv)
{
    const char *rompath = ".", *media = NULL;
    int i = 1, noboot = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--rompath")) rompath = argv[++i];
        else if (!strcmp(argv[i], "--media")) media = argv[++i];
        else if (!strcmp(argv[i], "--noboot")) noboot = 1;
        else break;
    }
    static const char *names[] = {"os12.rom", "basic2.rom", "dfs226.rom"};
    const uint8_t *roms[3]; size_t n;
    for (int r = 0; r < 3; r++) { char p[1024]; snprintf(p, sizeof p, "%s/%s", rompath, names[r]); roms[r] = slurp(p, &n); }
    static uint8_t blank[256];
    uint8_t *disc = media ? slurp(media, &n) : blank;
    if (!media) n = sizeof blank;
    mb_boot(roms, disc, (uint32_t) n);
    if (noboot) mb_noboot();
    for (; i < argc; i++) {
        const char *c = argv[i];
        if (!strcmp(c, "cycles")) mb_run(strtoull(argv[++i], 0, 0));
        else if (!strcmp(c, "trace-all")) { s_all = atol(argv[++i]); while (s_all > 0) mb_run(2000); }
        else if (!strcmp(c, "trace-pc")) {
            s_pc = (unsigned) strtoul(argv[++i], 0, 16); s_left = atol(argv[++i]);
            while (s_left > 0) mb_run(20000);
        } else if (!strcmp(c, "hash")) {
            uint32_t h = 2166136261u; const uint8_t *r = mb_ram();
            for (int a = 0; a < 0x8000; a++) h = (h ^ r[a]) * 16777619u;
            printf("%08x\n", h);
        } else if (!strcmp(c, "peek")) { unsigned a = (unsigned) strtoul(argv[++i], 0, 16), l = (unsigned) atoi(argv[++i]); hexdump(mb_ram() + a, l); }
        else if (!strcmp(c, "peekrom")) {
            int s = atoi(argv[++i]); unsigned a = (unsigned) strtoul(argv[++i], 0, 16), l = (unsigned) atoi(argv[++i]);
            const uint8_t *p = mb_slot(s); static uint8_t z[0x4000];
            hexdump((p ? p : z) + (a - 0x8000), l);
        } else if (!strcmp(c, "regs")) { char b[128]; mb_regs(b, sizeof b); printf("%s\n", b); }
        else if (!strcmp(c, "undoc")) { uint16_t pc; uint32_t k = mb_undoc(&pc); printf("undoc %u (last at %04x)\n", k, pc); }
        else if (!strcmp(c, "shot")) {
            static uint8_t px[RENDER_WIDTH * RENDER_HEIGHT];
            static const uint8_t PAL[8][3] = {{0,0,0},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255}};
            mb_shot(px, 40000);
            FILE *o = fopen(argv[++i], "wb"); fprintf(o, "P6 %d %d 255\n", RENDER_WIDTH, RENDER_HEIGHT);
            for (int k = 0; k < RENDER_WIDTH * RENDER_HEIGHT; k++) fwrite(PAL[px[k] & 7], 1, 3, o);
            fclose(o);
        } else { fprintf(stderr, "mbh: %s?\n", c); return 1; }
    }
    return 0;
}
