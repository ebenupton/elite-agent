// elkh: this folder's Electron (elk.c) on the command line, with a subset of clkh's commands (clkh, the headless
// Electron on Clock Signal's core in Solid Elite for the Electron's repository: 6-harness/clkh), so the two can be run
// side by side (compare.mjs).
//   elkh --rompath DIR [--media X.ssd] [--shiftbreak N] CMD...
//     boot SECS | cycles N | until-pc HEXPC [MAXCYCLES] | key NAME down|up | peek HEXADDR LEN | peekrom SLOT HEXADDR LEN
//     regs | dump PREFIX (PREFIX.ppm, the display as the page draws it) | hash HEXADDR LEN | undoc | save FILE | load FILE
// ROMs: os.rom, basic.rom and DFS-1770-2.20.rom in DIR (clkh's names; the ACP 1770 DFS 2.10).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elk.h"

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

static const struct { const char *name; int code; } KEYS[] = {
    {"RIGHT", ELK_RIGHT}, {"COPY", ELK_COPY}, {"SPACE", ELK_SPACE}, {"LEFT", ELK_LEFT}, {"DOWN", ELK_DOWN},
    {"RETURN", ELK_RETURN}, {"DELETE", ELK_DELETE}, {"MINUS", ELK_MINUS}, {"UP", ELK_UP}, {"COLON", ELK_COLON},
    {"0", ELK_0}, {"P", ELK_P}, {"SEMICOLON", ELK_SEMICOLON}, {"SLASH", ELK_SLASH}, {"9", ELK_9}, {"O", ELK_O},
    {"L", ELK_L}, {"FULLSTOP", ELK_FULLSTOP}, {"8", ELK_8}, {"I", ELK_I}, {"K", ELK_K}, {"COMMA", ELK_COMMA},
    {"7", ELK_7}, {"U", ELK_U}, {"J", ELK_J}, {"M", ELK_M}, {"6", ELK_6}, {"Y", ELK_Y}, {"H", ELK_H}, {"N", ELK_N},
    {"5", ELK_5}, {"T", ELK_T}, {"G", ELK_G}, {"B", ELK_B}, {"4", ELK_4}, {"R", ELK_R}, {"F", ELK_F}, {"V", ELK_V},
    {"3", ELK_3}, {"E", ELK_E}, {"D", ELK_D}, {"C", ELK_C}, {"2", ELK_2}, {"W", ELK_W}, {"S", ELK_S}, {"X", ELK_X},
    {"1", ELK_1}, {"Q", ELK_Q}, {"A", ELK_A}, {"Z", ELK_Z}, {"ESCAPE", ELK_ESCAPE}, {"FUNC", ELK_FUNC},
    {"CONTROL", ELK_CTRL}, {"SHIFT", ELK_SHIFT},
};

static struct elk E;
static unsigned s_trace_pc; static int s_trace_left;
static void trace_hook(struct elk *e, uint16_t pc, uint64_t cyc)
{
    if (pc == s_trace_pc && s_trace_left > 0) { printf("hit %04x %llu\n", pc, (unsigned long long) cyc); s_trace_left--; }
}

int main(int argc, char **argv)
{
    const char *rompath = ".", *media = NULL;
    long shiftbreak = -1;
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--rompath")) rompath = argv[++i];
        else if (!strcmp(argv[i], "--media")) media = argv[++i];
        else if (!strcmp(argv[i], "--shiftbreak")) shiftbreak = atol(argv[++i]);
        else break;
    }
    char p[1024];
    size_t n;
    snprintf(p, sizeof p, "%s/os.rom", rompath); uint8_t *os = slurp(p, &n);
    snprintf(p, sizeof p, "%s/basic.rom", rompath); uint8_t *basic = slurp(p, &n);
    snprintf(p, sizeof p, "%s/DFS-1770-2.20.rom", rompath); uint8_t *dfs = slurp(p, &n);
    elk_init(&E, os, basic, dfs);
    if (media) { uint8_t *d = slurp(media, &n); elk_insert_ssd(&E, d, (uint32_t) n); }
    elk_power_on(&E, media && shiftbreak != 0);
    if (shiftbreak > 0) E.restart_in = shiftbreak;
    for (; i < argc; i++) {
        const char *c = argv[i];
        if (!strcmp(c, "boot")) elk_run(&E, E.now + (uint64_t) (atof(argv[++i]) * 2e6));
        else if (!strcmp(c, "cycles")) elk_run(&E, E.now + strtoull(argv[++i], 0, 0));
        else if (!strcmp(c, "until-pc")) {
            unsigned pc = (unsigned) strtoul(argv[++i], 0, 16);
            uint64_t max = (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') ? strtoull(argv[++i], 0, 0) : 100000000;
            uint64_t end = E.now + max;
            while (E.now < end && E.cpu.pc != pc) elk_run(&E, E.now + 1);
            printf("pc %04x at %llu\n", E.cpu.pc, (unsigned long long) E.now);
        } else if (!strcmp(c, "trace-pc")) {
            s_trace_pc = (unsigned) strtoul(argv[++i], 0, 16); s_trace_left = atoi(argv[++i]);
            E.fetch_hook = trace_hook;
            uint64_t end = E.now + 400000000ull;
            while (s_trace_left > 0 && E.now < end) elk_run(&E, E.now + 1);
            E.fetch_hook = NULL;
        } else if (!strcmp(c, "key")) {
            const char *name = argv[++i]; int down = !strcmp(argv[++i], "down");
            for (size_t k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++) if (!strcmp(KEYS[k].name, name)) elk_key(&E, KEYS[k].code, down);
        } else if (!strcmp(c, "peek") || !strcmp(c, "peekrom") || !strcmp(c, "hash")) {
            int slot = !strcmp(c, "peekrom") ? atoi(argv[++i]) : -1;
            unsigned a = (unsigned) strtoul(argv[++i], 0, 16), len = (unsigned) strtoul(argv[++i], 0, 0);
            if (!strcmp(c, "hash")) {
                uint32_t h = 2166136261u;
                for (unsigned k = 0; k < len; k++) h = (h ^ elk_peek(&E, slot, (uint16_t) (a + k))) * 16777619u;
                printf("%08x\n", h);
            } else {
                for (unsigned k = 0; k < len; k++) printf("%02x%s", elk_peek(&E, slot, (uint16_t) (a + k)), (k % 16 == 15 || k + 1 == len) ? "\n" : " ");
            }
        } else if (!strcmp(c, "regs")) {
            printf("pc %04x a %02x x %02x y %02x s %02x p %02x cycles %llu fields %u fpos %u romsel %d\n", E.cpu.pc, E.cpu.a,
                   E.cpu.x, E.cpu.y, E.cpu.s, elk_cpu6502_get_p(&E.cpu, 1), (unsigned long long) E.now, E.fields, E.fpos, E.active);
        } else if (!strcmp(c, "save")) { if (!elk_save(&E, argv[++i])) { perror(argv[i]); return 1; } }
        else if (!strcmp(c, "load")) { if (!elk_load(&E, argv[++i])) { fprintf(stderr, "elkh: cannot load %s\n", argv[i]); return 1; } }
        else if (!strcmp(c, "undoc")) printf("undocumented opcodes run: %u\n", E.undoc);
        else if (!strcmp(c, "dump")) {
            const char *pre = argv[++i];
            elk_want_picture(&E);
            while (!E.have_px) elk_run(&E, E.now + 2000);
            snprintf(p, sizeof p, "%s.ppm", pre);
            FILE *f = fopen(p, "wb");
            fprintf(f, "P6 %d %d 255\n", ELK_RENDER_W, ELK_RENDER_H);
            for (int k = 0; k < ELK_RENDER_W * ELK_RENDER_H; k++) {
                uint8_t col = E.px[k], rgb[3] = {(uint8_t) (col & 1 ? 255 : 0), (uint8_t) (col & 2 ? 255 : 0), (uint8_t) (col & 4 ? 255 : 0)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        } else { fprintf(stderr, "elkh: what is %s?\n", c); return 1; }
    }
    return 0;
}
