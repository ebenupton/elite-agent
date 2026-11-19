// An Acorn Electron with a Plus 3 and sideways RAM (elk.h). The timing is Clock Signal's Electron (Thomas Harte's CLK,
// Machines/Acorn/Electron, after hoglet67's ElectronFpga and JamSoft's ULA), which is the oracle this machine is checked
// against (clkh, README.md):
//
// - The clock is the 6502's 2 MHz. An access to ROM or sideways RAM takes one cycle; an access to RAM, to the keyboard
//   (ROM 8/9 paged) or to FRED, JIM and SHEILA (&FC00-&FEFF) waits for the ULA's next 1 MHz slot: three cycles from an
//   even cycle of the line, two from an odd one (run_until_io_slot). In MODEs 0-3 a RAM access in the display's fetch
//   waits on to the end of the line's pixels (run_until_ram_slot).
// - A line is 128 cycles, a field 313 lines then 312 (field_), 80,000 cycles a pair. The ULA raises the real time clock
//   interrupt at line 99 cycle 62 of the first field and line 98 cycle 126 of the second, display end at line 255 (249 in
//   the text modes) cycle 96, and latches the screen's start address at the last line's cycle 127.
// - Each access runs the raster over its cycles first, then takes place: an interrupt raised in those cycles is seen by
//   the access (a read of &FE00) and by the 6502's interrupt poll from the next access on. Clock Signal's 6502 takes an
//   IRQ at the end of an instruction if the line was up after the access two before the last, I clear: here every access
//   goes through this file (the CPU's page tables are all empty), which keeps the core's IRQ history one bit an access.
// - The tape's shifter runs while the ULA is in its sound or output mode, so its status bits change as Clock Signal's do;
//   the Plus 3's 1770 interrupts nothing (Clock Signal wires no NMI to it: the 1770 DFS polls).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the 1770 and the disc, Kieran Connell's (rp2350-bbc-micro's kc-beeb core), under this machine's names so that the
// Master's own copies can sit beside them in one binary
#define fdc_init elk_fdc_init
#define fdc_power_on_reset elk_fdc_power_on_reset
#define fdc_break_reset elk_fdc_break_reset
#define fdc_read elk_fdc_read
#define fdc_write elk_fdc_write
#define fdc_ticks_to_event elk_fdc_ticks_to_event
#define fdc_advance elk_fdc_advance
#define disc_crc_init elk_disc_crc_init
#define disc_crc_add elk_disc_crc_add
#define disc_fm_to_pulses elk_disc_fm_to_pulses
#define disc_pulses_to_fm elk_disc_pulses_to_fm
#define disc_mfm_to_pulses elk_disc_mfm_to_pulses
#define disc_pulses_to_mfm elk_disc_pulses_to_mfm
#define disc_init elk_disc_init
#define disc_load_ssd elk_disc_load_ssd
#define disc_read_sector elk_disc_read_sector
#include "disc.c"
#include "fdc.c"

#include "elk.h"

enum { I_MASTER = 0x01, I_POR = 0x02, I_DE = 0x04, I_RTC = 0x08, I_RDF = 0x10, I_TDE = 0x20, I_HTD = 0x40 };

// ---- the raster's events, in a field pair
enum { EV_RTC, EV_DE_TXT, EV_DE_GFX, EV_LATCH, EV_WRAP };
static const struct { uint32_t pos; uint8_t what; } EVENTS[] = {
    {99 * 128 + 62, EV_RTC}, {249 * 128 + 96, EV_DE_TXT}, {255 * 128 + 96, EV_DE_GFX}, {312 * 128 + 127, EV_LATCH},
    {ELK_FIELD_A + 98 * 128 + 126, EV_RTC}, {ELK_FIELD_A + 249 * 128 + 96, EV_DE_TXT},
    {ELK_FIELD_A + 255 * 128 + 96, EV_DE_GFX}, {ELK_FIELD_A + 311 * 128 + 127, EV_LATCH}, {ELK_PAIR, EV_WRAP},
};
#define N_EVENTS (int) (sizeof EVENTS / sizeof EVENTS[0])
static int ev_index(uint32_t fpos) { int i = 0; while (EVENTS[i].pos < fpos) i++; return i; }

// (cpu6502_run works on its own copy of struct cpu6502: while a run is on, the IRQ line goes to the copy, whose
// pointer each access leaves here)
static struct cpu6502 *s_cpu;
static inline struct cpu6502 *cpu_of(struct elk *e) { return s_cpu ? s_cpu : &e->cpu; }
static void set_isr(struct elk *e, uint8_t v)
{
    e->isr = v;
    if (e->isr & e->ier) e->isr |= I_MASTER; else e->isr &= (uint8_t) ~I_MASTER;
    cpu_of(e)->irq_line = e->isr & I_MASTER;
}
static void raise_irq(struct elk *e, uint8_t bits) { set_isr(e, (uint8_t) (e->isr | bits)); }

// ---- the tape's shifter (Clock Signal's Electron/Tape.cpp, its output side; no tape is ever in)
static void tape_post(struct elk *e)
{
    if (e->tape_posted != e->tape_st) {
        e->tape_posted = e->tape_st;
        set_isr(e, (uint8_t) ((e->isr & ~(I_TDE | I_RDF | I_HTD)) | e->tape_st));
    }
}
static void tape_bit(struct elk *e, int bit)
{
    e->tape_data = (uint16_t) ((e->tape_data >> 1) | (bit << 10));
    if (e->tape_min_full) e->tape_min_full--;
    if (e->tape_min_full == 8) e->tape_st &= (uint8_t) ~I_RDF;
    if (!e->tape_min_full && (e->tape_data & 3) == 1) {
        e->tape_st |= I_RDF;
        if (e->tape_input) e->tape_min_full = 9;
    }
    if (e->tape_bits_left) e->tape_bits_left--;
    if (!e->tape_bits_left) e->tape_st |= I_TDE;
    if (e->tape_data == 0x3ff) e->tape_st |= I_HTD; else e->tape_st &= (uint8_t) ~I_HTD;
    tape_post(e);
}
static void tape_catchup(struct elk *e)
{
    uint64_t n = e->now - e->tape_t;
    e->tape_t = e->now;
    if (!e->tape_enabled || e->tape_input) return;
    if (n > 0x40000000) n = 0x40000000;
    e->tape_acc += (uint32_t) n;
    // (a 1 a bit at 1200 baud; once the shifter is all 1s nothing more changes)
    while (e->tape_acc > 1664) {
        e->tape_acc -= 1664;
        if (e->tape_data == 0x3ff && !e->tape_bits_left && !e->tape_min_full && (e->tape_st & I_TDE) && (e->tape_st & I_HTD)) {
            e->tape_acc %= 1664;
            break;
        }
        tape_bit(e, 1);
    }
}

// ---- the picture
static void render(struct elk *e)
{
    uint8_t *px = e->px;
    memset(px, 0, ELK_RENDER_W * ELK_RENDER_H);
    int mode = e->mode, m40 = e->m40, text = e->text;
    int bpp = (mode == 1 || mode == 5) ? 2 : mode == 2 ? 4 : 1;
    uint16_t mode_base = e->mode_base;
    int lines = text ? 250 : 256, cols = m40 ? 40 : 80;
    uint16_t row = e->screen_base;
    for (int v = 0; v < lines; v++) {
        int cr = text ? v % 10 : v & 7;
        uint8_t *out = px + (ELK_RENDER_Y + 2 * v) * ELK_RENDER_W + ELK_RENDER_X;
        if (cr < 8) {
            uint16_t a = row;
            for (int c = 0; c < cols; c++) {
                uint8_t d = e->ram[(a | cr) & 0x7fff];
                int w = m40 ? 16 : 8;           // the byte's width on the 640-pixel line
                for (int i = 0; i < 8 / bpp; i++) {
                    int idx;
                    if (bpp == 1) idx = ((d << i) >> 4) & 8;
                    else if (bpp == 2) idx = (((d << i) >> 4) & 8) | (((d << i) >> 2) & 2);
                    else idx = (((d << i) >> 4) & 8) | (((d << i) >> 3) & 4) | (((d << i) >> 2) & 2) | (((d << i) >> 1) & 1);
                    uint8_t col = e->pal[idx];
                    int pw = w * bpp / 8;
                    for (int j = 0; j < pw; j++) out[c * w + i * pw + j] = col;
                }
                a = (uint16_t) (a + 8);
                if (!(a & 0x7800)) a = (uint16_t) (mode_base | (a & 0x7ff));
            }
        }
        memcpy(out + ELK_RENDER_W, out, 640);
        if (cr == (text ? 9 : 7)) {
            // (the next character row: the address the line's last byte left, bits 14-6, as the hardware keeps it)
            uint16_t a = row;
            for (int c = 0; c < cols; c++) { a = (uint16_t) (a + 8); if (!(a & 0x7800)) a = (uint16_t) (mode_base | (a & 0x7ff)); }
            row = a & 0x7fc0;
        }
    }
    e->have_px = 1;
    e->want_px = 0;
}

static void fdc_catchup(struct elk *e)
{
    while (e->fdc_t < e->now) {
        uint32_t ev = fdc_ticks_to_event(&e->fdc);
        uint64_t left = e->now - e->fdc_t;
        if (ev == 0xffffffffu) { e->fdc_t = e->now; break; }
        if (ev == 0) ev = 1;
        uint32_t n = ev < left ? ev : (uint32_t) left;
        e->fdc_t += n;
        fdc_advance(&e->fdc, n, (uint32_t) e->fdc_t);
    }
}

// the raster moved on to `end` (fpos + an access's cycles): the events at positions before it
static __attribute__((noinline)) void events(struct elk *e, uint32_t end)
{
    int i = ev_index(e->fpos);
    for (;;) {
        uint32_t q = EVENTS[i].pos;
        if (q >= end) break;
        switch (EVENTS[i].what) {
        case EV_RTC: raise_irq(e, I_RTC); break;
        case EV_DE_TXT: if (e->text) raise_irq(e, I_DE); break;
        case EV_DE_GFX: if (!e->text) raise_irq(e, I_DE); break;
        case EV_LATCH:
            e->fields++;
            if (e->want_px && e->px) render(e);
            tape_catchup(e);
            if (e->fdc.drive[0].spinning || e->fdc.timer_state) fdc_catchup(e);
            break;
        case EV_WRAP:
            end -= ELK_PAIR;
            i = -1;
            break;
        }
        i++;
    }
    e->fpos = end;
    e->next_ev = EVENTS[i].pos;
}

// one access of `addr`: the raster run over its cycles; returns them
static inline uint32_t tick(struct elk *e, struct cpu6502 *c, uint16_t addr)
{
    uint32_t d = 1;
    if (e->slow[addr >> 8]) {
        d = 3 - (e->fpos & 1);
        if (addr < 0x8000 && !e->m40) {
            // (MODEs 0-3: no RAM for the CPU while the line's pixels are fetched)
            uint32_t p = e->fpos + d;
            if (p >= ELK_PAIR) p -= ELK_PAIR;
            uint32_t slot = p & 127, v = p < ELK_FIELD_A ? p >> 7 : (p - ELK_FIELD_A) >> 7;
            int text = e->text;
            if (slot < 80 && v < (text ? 250u : 256u) && (!text || v % 10 < 8)) d += 81 - slot;
        }
    }
    e->now += d;
    c->cyc += d;
    uint32_t end = e->fpos + d;
    if (end > e->next_ev) events(e, end); else e->fpos = end;
    return d;
}
static inline void hist(struct cpu6502 *c)
{
    c->irq_hist = (c->irq_hist << 1) | c->irq_line;
    c->last_sync = c->cyc;
}

static void remap(struct elk *e)
{
    e->kbd = (e->active & 0xe) == 0x8;
    e->basic = (e->active & 0xe) == 0xa;
    uint8_t *img = e->slot[e->active] ? e->slot[e->active] : e->empty;
    for (int p = 0x80; p < 0xc0; p++) {
        e->rmap[p] = (e->kbd || e->basic) ? NULL : img + (p - 0x80) * 256;
        e->wmap[p] = e->slot_w[e->active] ? img + (p - 0x80) * 256 : e->scratch;
        e->slow[p] = (uint8_t) e->kbd;
    }
}

static uint8_t io_rd(struct elk *e, uint16_t a)
{
    if (a >= 0x8000 && a < 0xc000) {
        if (e->kbd) {
            uint8_t v = 0;
            for (int line = 0; line < 14; line++) if (!(a & (1 << line))) v |= e->keys[line];
            return v;
        }
        // BASIC (10 or 11): what the slot holds, ANDed with BASIC
        const uint8_t *img = e->slot[e->active] ? e->slot[e->active] : e->empty;
        return img[a & 0x3fff] & e->slot[10][a & 0x3fff];
    }
    switch (a & 0xff0f) {
    case 0xfe00: { uint8_t v = e->isr; e->isr &= (uint8_t) ~I_POR; return v; }
    case 0xfe04:
        tape_catchup(e);
        e->tape_st &= (uint8_t) ~I_RDF; tape_post(e);
        return (uint8_t) (e->tape_data >> 2);
    case 0xfc00:
        if ((a & 0xf0) == 0xc0) return 1;
        break;
    case 0xfc04: case 0xfc05: case 0xfc06: case 0xfc07:
        if ((a & 0xf0) == 0xc0) {
            if (e->shift_held && a == 0xfcc4) { e->shift_held = 0; e->keys[13] &= (uint8_t) ~8; }
            fdc_catchup(e);
            return fdc_read(&e->fdc, (uint16_t) (4 + (a & 3)));
        }
        break;
    }
    return 0xff;
}

static void set_palette(struct elk *e, int reg, uint8_t v)
{
    e->pal_src[reg & 7] = (uint8_t) ~v;
    int pair = reg & 6, base = pair == 0 ? 0 : pair == 2 ? 4 : pair == 4 ? 5 : 1;
    const uint8_t *s0 = &e->pal_src[pair], *s1 = &e->pal_src[pair + 1];
    // (Clock Signal's set_palette_group: each of four entries' red, green and blue from bits of the two registers; the
    // colour kept here in the BBC's order, bit 0 red, 1 green, 2 blue)
#define BIT(p, b) (((*(p)) >> (b)) & 1)
#define RGB(r, g, b) (uint8_t) ((r) | (g) << 1 | (b) << 2)
    e->pal[base | 0] = RGB(BIT(s1, 0), BIT(s1, 4), BIT(s0, 4));
    e->pal[base | 2] = RGB(BIT(s1, 1), BIT(s1, 5), BIT(s0, 5));
    e->pal[base | 8] = RGB(BIT(s1, 2), BIT(s0, 2), BIT(s0, 6));
    e->pal[base | 10] = RGB(BIT(s1, 3), BIT(s0, 3), BIT(s0, 7));
#undef BIT
#undef RGB
}

static void snd_apply(struct elk *e, int reg, uint8_t v)
{
    if (reg == 6) e->snd_div = v;
    else { int on = (v & 6) == 2; if (on != e->snd_on) { e->snd_on = (uint8_t) on; e->snd_phase = 0; } }
}
static void snd_drop(struct elk *e, int k)
{
    for (int i = 0; i < k; i++) snd_apply(e, e->snd[i].reg, e->snd[i].val);
    memmove(e->snd, e->snd + k, (size_t) (e->n_snd - k) * sizeof e->snd[0]);
    e->n_snd -= k;
}
static void snd_log(struct elk *e, uint8_t reg, uint8_t v)
{
    if (e->n_snd == ELK_SND_LOG) snd_drop(e, ELK_SND_LOG / 2);
    e->snd[e->n_snd].t = e->now; e->snd[e->n_snd].reg = reg; e->snd[e->n_snd].val = v; e->n_snd++;
}

static void io_wr(struct elk *e, uint16_t a, uint8_t v)
{
    switch (a & 0xff0f) {
    case 0xfe00: e->ier = v & (uint8_t) ~1; set_isr(e, e->isr); return;
    case 0xfe02: e->screen_base = (uint16_t) ((e->screen_base & 0x7e00) | ((v << 1) & 0x01c0)); return;
    case 0xfe03: e->screen_base = (uint16_t) (((v << 9) & 0x7e00) | (e->screen_base & 0x01c0)); return;
    case 0xfe04:
        tape_catchup(e);
        e->tape_data = (uint16_t) ((v << 2) | 1); e->tape_bits_left = 9;
        e->tape_st &= (uint8_t) ~I_TDE; tape_post(e);
        return;
    case 0xfe05: {
        uint8_t clr = v & 0xf0;
        if (clr) {
            uint8_t s = e->isr;
            if (clr & 0x10) s &= (uint8_t) ~I_DE;
            if (clr & 0x20) s &= (uint8_t) ~I_RTC;
            if (clr & 0x40) s &= (uint8_t) ~I_HTD;
            set_isr(e, s);
        }
        // (the ULA takes the paging bits at every write, but while 8-11 is paged only a value with bit 3 set)
        if ((e->active & 0xc) != 0x8 || (v & 0x08)) {
            e->active = v & 0xf;
            remap(e);
        }
        return;
    }
    case 0xfe06:
        snd_log(e, 6, v);
        tape_catchup(e);
        e->tape_acc = 0; e->tape_bits_left = 0;
        return;
    case 0xfe07:
        snd_log(e, 7, v);
        tape_catchup(e);
        e->tape_enabled = (v & 6) != 6;
        e->tape_input = (v & 6) == 0;
        e->tape_running = (v & 0x40) != 0;
        e->fe07 = v;
        e->mode = (v >> 3) & 7;
        e->m40 = e->mode >= 4;
        e->text = e->mode == 3 || e->mode == 6;
        // (mode 7 is MODE 4's display wrapping to &5800, as Clock Signal has it)
        e->mode_base = e->mode < 3 ? 0x3000 : e->mode == 3 ? 0x4000 : e->mode == 6 ? 0x6000 : 0x5800;
        return;
    case 0xfe08: case 0xfe09: case 0xfe0a: case 0xfe0b: case 0xfe0c: case 0xfe0d: case 0xfe0e: case 0xfe0f:
        set_palette(e, a & 7, v);
        return;
    case 0xfc00:
        if ((a & 0xf0) == 0xc0) {
            fdc_catchup(e);
            e->plus3_ctrl = v;
            // (the Plus 3's latch: drive 0 and 1 in bits 0 and 1, the side in 2, single density in 3, as the
            // Master's control register has them for the 1770 port)
            fdc_write(&e->fdc, 0, (uint8_t) (0x04 | (v & 3) | (v & 4 ? 0x10 : 0) | (v & 8 ? 0x20 : 0)));
        }
        return;
    case 0xfc04: case 0xfc05: case 0xfc06: case 0xfc07:
        if ((a & 0xf0) == 0xc0) {
            if (e->shift_held && a == 0xfcc4) { e->shift_held = 0; e->keys[13] &= (uint8_t) ~8; }
            fdc_catchup(e);
            fdc_write(&e->fdc, (uint16_t) (4 + (a & 3)), v);
        }
        return;
    }
}

static inline __attribute__((always_inline)) uint8_t elk_rd(struct cpu6502 *c, uint16_t a)
{
    struct elk *e = (struct elk *) c->m;
    s_cpu = c;
    tick(e, c, a);
    const uint8_t *p = e->rmap[a >> 8];
    uint8_t v = p ? p[a & 0xff] : io_rd(e, a);
    hist(c);
    return v;
}
static inline __attribute__((always_inline)) void elk_wr(struct cpu6502 *c, uint16_t a, uint8_t v)
{
    struct elk *e = (struct elk *) c->m;
    s_cpu = c;
    tick(e, c, a);
    uint8_t *p = e->wmap[a >> 8];
    if (p) p[a & 0xff] = v; else io_wr(e, a, v);
    hist(c);
}

#define CPU6502_CMOS_ONLY 0
#define CPU6502_IO_RD(c, a) elk_rd(c, a)
#define CPU6502_IO_WR(c, a, v) elk_wr(c, a, v)
// (no catch-up is ever due: each access brings the machine up to date itself)
#define CPU6502_SYNC(c) ((c)->sync_due = (c)->cyc + 0x40000000u, \
    (c)->stop = (int32_t) ((c)->sync_due - (c)->run_end) < 0 ? (c)->sync_due : (c)->run_end)
#define CPU6502_TRACE(c) elk_trace(c)
static inline void elk_trace(struct cpu6502 *c);
#include "cpu6502.c"

// (the NMOS core runs an undocumented opcode as a 2-cycle NOP, which none of this machine's software should need:
// counted, from cpu6502.c's NMOS table)
static const uint8_t UNDOC[256 / 8] = {
    0x9c, 0x98, 0x9c, 0x9c, 0x8c, 0x88, 0x9c, 0x9c, 0x9c, 0x88, 0x9c, 0x9c, 0x9c, 0x88, 0x9c, 0x9c,
    0x8d, 0x8a, 0x8c, 0xd8, 0x88, 0x88, 0x8c, 0x88, 0x8c, 0x88, 0x9c, 0x9c, 0x8c, 0x88, 0x9c, 0x9c,
};
static inline void elk_trace(struct cpu6502 *c)
{
    struct elk *e = (struct elk *) c->m;
    const uint8_t *p = e->rmap[c->pc >> 8];
    if (p) { uint8_t op = p[c->pc & 0xff]; if (UNDOC[op >> 3] >> (op & 7) & 1) e->undoc++; }
    if (e->fetch_hook) {
        uint32_t d = e->slow[c->pc >> 8] ? 3 - (e->fpos & 1) : 1;
        e->fetch_hook(e, c->pc, e->now + d);
    }
}

// ---- the machine
static void cpu_reset(struct elk *e)
{
    // (Clock Signal's reset program: two reads at PC, three at the stack going down, then the vector)
    struct cpu6502 *c = &e->cpu;
    s_cpu = NULL;
    (void) elk_rd(c, c->pc);
    (void) elk_rd(c, c->pc);
    for (int i = 0; i < 3; i++) { (void) elk_rd(c, (uint16_t) (0x100 | c->s)); c->s = (c->s - 1) & 0xff; }
    c->i = 1;
    uint16_t lo = elk_rd(c, 0xfffc);
    c->pc = (uint16_t) (lo | elk_rd(c, 0xfffd) << 8);
    c->cycles = e->now;
}

static void no_nmi(void *ctx, int level) { (void) ctx; (void) level; }

void elk_init(struct elk *e, const uint8_t *mos, const uint8_t *basic, const uint8_t *dfs)
{
    uint8_t *px = e->px;
    memset(e, 0, sizeof *e);
    e->px = px;
    memset(e->empty, 0xff, sizeof e->empty);
    e->mos = mos;
    e->slot[10] = (uint8_t *) basic;
    e->slot[0] = malloc(0x4000);            // the DFS: writeable, as Clock Signal has it
    memcpy(e->slot[0], dfs, 0x4000);
    e->slot_w[0] = 1;
    for (int s = 0; s < 16; s++) {
        if (s == 0 || (s >= 8 && s < 12)) continue;
        e->slot[s] = malloc(0x4000);
        memset(e->slot[s], 0xff, 0x4000);
        e->slot_w[s] = 1;
    }
    for (int p = 0; p < 0x80; p++) { e->rmap[p] = e->wmap[p] = e->ram + p * 256; e->slow[p] = 1; }
    for (int p = 0xc0; p < 0x100; p++) { e->rmap[p] = (uint8_t *) mos + (p - 0xc0) * 256; e->wmap[p] = e->scratch; }
    for (int p = 0xfc; p < 0xff; p++) { e->rmap[p] = e->wmap[p] = NULL; e->slow[p] = 1; }
    e->active = 0;
    remap(e);
    e->isr = I_POR | I_TDE | 0x80;
    e->screen_base = 0;
    e->mode = 0;
    e->m40 = 1;
    struct cpu6502 *c = &e->cpu;
    memset(c, 0, sizeof *c);
    c->cmos = 0;
    c->m = e;
    for (int i = 0; i < 256; i++) e->null_tab[i] = NULL;
    c->rd_tabs[0] = c->rd_tabs[1] = c->wr_tabs[0] = c->wr_tabs[1] = e->null_tab;
    c->rd_tab = c->wr_tab = e->null_tab;
    fdc_init(&e->fdc, no_nmi, e);
    fdc_power_on_reset(&e->fdc);
    fdc_write(&e->fdc, 0, 0x04);
    static uint32_t *pulses;
    if (!pulses) pulses = calloc((size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, sizeof(uint32_t));
    disc_init(&e->disc, pulses, 1, 1);
    // (where the raster is when the 6502 starts: Clock Signal's CRT runs the video alone for 124 x 10,000 cycles to
    // lock its framing first, which leaves it 64 cycles from the end of a first field)
    e->fpos = 40000;
    e->next_ev = EVENTS[ev_index(e->fpos)].pos;
}

void elk_insert_ssd(struct elk *e, const uint8_t *image, uint32_t size)
{
    uint32_t n = (size + 255) & ~255u;
    uint8_t *copy = calloc(1, n + 256);
    memcpy(copy, image, size);
    disc_load_ssd(&e->disc, copy, n);
    free(copy);
    e->fdc.drive[0].disc = &e->disc;
}

void elk_power_on(struct elk *e, int shift_boot)
{
    struct cpu6502 *c = &e->cpu;
    c->a = c->x = c->y = 0;
    c->s = 0;
    c->pc = 0;
    c->i_poll = 1;
    c->sync_due = c->cyc;
    cpu_reset(e);
    e->restart_in = shift_boot ? 1000000 : 0;
}

void elk_run(struct elk *e, uint64_t cycles)
{
    struct cpu6502 *c = &e->cpu;
    while (e->now < cycles) {
        uint64_t to = cycles;
        if (e->restart_in > 0) {
            uint64_t at = (uint64_t) e->restart_in;
            if (at <= e->now) {
                // (Clock Signal's shift restart: a power-on reset with SHIFT held, let go at the DFS's first 1770 access)
                e->restart_in = 0;
                e->keys[13] |= 8;
                e->shift_held = 1;
                cpu_reset(e);
                continue;
            }
            if (at < to) to = at;
        }
        c->cycles = e->now;
        elk_cpu6502_run(c, to);
        s_cpu = NULL;
    }
}

void elk_key(struct elk *e, int code, int down)
{
    if (code < 0 || code >= 0xe0) return;
    if (e->key_hook) e->key_hook(e, code, down);
    if (down) e->keys[code >> 4] |= (uint8_t) (code & 15);
    else e->keys[code >> 4] &= (uint8_t) ~(code & 15);
}

uint8_t elk_peek(const struct elk *e, int slot, uint16_t a)
{
    if (a < 0x8000) return e->ram[a];
    if (a >= 0xc000) return e->mos[a & 0x3fff];
    int s = slot < 0 ? e->active : slot;
    const uint8_t *img = e->slot[s] ? e->slot[s] : e->empty;
    return img[a & 0x3fff];
}

void elk_poke(struct elk *e, int slot, uint16_t a, uint8_t v)
{
    if (a < 0x8000) { e->ram[a] = v; return; }
    if (a >= 0xc000) return;
    int s = slot < 0 ? e->active : slot;
    if (e->slot[s] && e->slot_w[s]) e->slot[s][a & 0x3fff] = v;
}

void elk_want_picture(struct elk *e)
{
    if (!e->px) e->px = calloc(ELK_RENDER_W, ELK_RENDER_H);
    e->want_px = 1;
    e->have_px = 0;
}

void elk_audio(struct elk *e, int16_t *out, int n, int period, int amp)
{
    // (the last n periods up to now; what came before them only sets the state)
    uint64_t t = e->now >= (uint64_t) n * period ? e->now - (uint64_t) n * period : 0;
    if (e->snd_t > t) t = e->snd_t;
    int k = 0;
    while (k < e->n_snd && e->snd[k].t < t) k++;
    snd_drop(e, k);
    k = 0;
    for (int i = 0; i < n; i++) {
        int high = 0;
        for (int j = 0; j < period; j++, t++) {
            while (k < e->n_snd && e->snd[k].t <= t) { snd_apply(e, e->snd[k].reg, e->snd[k].val); k++; }
            if (e->snd_on) {
                // (a square wave at 1 MHz / 16 (n + 1): half a period is 16 (n + 1) cycles of 2 MHz)
                uint32_t half = 16u * (e->snd_div + 1u);
                if (++e->snd_phase >= 2 * half) e->snd_phase = 0;
                high += e->snd_phase >= half;
            }
        }
        out[i] = e->snd_on ? (int16_t) ((2 * high - period) * amp / period) : 0;
    }
    // (the writes after the samples are kept for the next call; those in them are done)
    memmove(e->snd, e->snd + k, (size_t) (e->n_snd - k) * sizeof e->snd[0]);
    e->n_snd -= k;
    e->snd_t = t;
}

// ---- snapshots: the struct, then the writeable slots and the disc's pulses; the pointers are this instance's own
int elk_save(const struct elk *e, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(e, sizeof *e, 1, f);
    for (int s = 0; s < 16; s++) if (e->slot_w[s]) fwrite(e->slot[s], 0x4000, 1, f);
    fwrite(e->disc.pulses, sizeof(uint32_t), (size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, f);
    fclose(f);
    return 1;
}

int elk_load(struct elk *e, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct elk *old = malloc(sizeof *old);
    int ok = fread(old, sizeof *old, 1, f) == 1;
    // (what belongs to this instance)
    uint8_t *slot[16], *px = e->px; const uint8_t *mos = e->mos; uint32_t *pulses = e->disc.pulses;
    void (*fh)(struct elk *, uint16_t, uint64_t) = e->fetch_hook; void (*kh)(struct elk *, int, int) = e->key_hook;
    memcpy(slot, e->slot, sizeof slot);
    uintptr_t base = (uintptr_t) &old->fdc.drive[0], was = (uintptr_t) old->fdc.cur;
    memcpy(e, old, sizeof *e);
    memcpy(e->slot, slot, sizeof slot);
    e->px = px; e->mos = mos; e->fetch_hook = fh; e->key_hook = kh;
    e->cpu.m = e;
    e->cpu.rd_tabs[0] = e->cpu.rd_tabs[1] = e->cpu.wr_tabs[0] = e->cpu.wr_tabs[1] = e->null_tab;
    e->cpu.rd_tab = e->cpu.wr_tab = e->null_tab;
    for (int p = 0; p < 0x80; p++) e->rmap[p] = e->wmap[p] = e->ram + p * 256;
    for (int p = 0xc0; p < 0x100; p++) { e->rmap[p] = (uint8_t *) mos + (p - 0xc0) * 256; e->wmap[p] = e->scratch; }
    for (int p = 0xfc; p < 0xff; p++) e->rmap[p] = e->wmap[p] = NULL;
    remap(e);
    e->fdc.set_nmi = no_nmi; e->fdc.ctx = e;
    e->disc.pulses = pulses;
    for (int i = 0; i < 2; i++) e->fdc.drive[i].disc = old->fdc.drive[i].disc ? &e->disc : NULL;
    e->fdc.cur = old->fdc.cur ? &e->fdc.drive[(was - base) / sizeof(struct fdc_drive)] : NULL;
    for (int s = 0; s < 16 && ok; s++) if (e->slot_w[s]) ok = fread(e->slot[s], 0x4000, 1, f) == 1;
    if (ok) ok = fread(pulses, sizeof(uint32_t), (size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK, f) == (size_t) DISC_TRACKS * DISC_BYTES_PER_TRACK;
    free(old);
    fclose(f);
    return ok;
}

int elk_disc_dump(struct elk *e, uint8_t *out, int size)
{
    int bad = 0, o = 0;
    uint8_t s[256];
    for (int t = 0; t < 80; t++)
        for (int sc = 0; sc < 10; sc++) {
            if (!disc_read_sector(&e->disc, 0, t, sc, s)) { memset(s, 0, 256); bad++; }
            if (o + 256 <= size) memcpy(out + o, s, 256);
            o += 256;
        }
    return bad;
}
