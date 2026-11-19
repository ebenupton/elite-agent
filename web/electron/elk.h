// An Acorn Electron for the page: MOS 1.00 and BASIC II, an Acorn Plus 3 (WD1770) with the ACP 1770 DFS in ROM slot 0,
// and sideways RAM in every other free slot (1-7, 12-15), the machine Solid Elite for the Electron is built for and the
// one clkh (Clock Signal's Electron, headless) runs it on. The 6502 is Kieran Connell's cycle-stepped core (cpu6502.c,
// NMOS), the 1770 and the disc are his ports of beebjit's (fdc.c, disc.c); the ULA's timing follows Clock Signal's
// Electron (after hoglet67's ElectronFpga and JamSoft's ULA): see elk.c.
#pragma once
#include <stdint.h>
#ifndef CPU6502_FN
#define CPU6502_FN(name) elk_##name     // (the 6502 under this machine's names: the Master's may be in the same binary)
#endif
#include "cpu6502.h"
#include "fdc.h"

#define ELK_PAIR 80000              // 2 MHz cycles in two fields (313 + 312 lines of 128 cycles)
#define ELK_FIELD_A 40064           // the first, 313 lines
#define ELK_RENDER_W 768            // the picture: 640 x 256 pixels, lines doubled, in a border, as the Master core's
#define ELK_RENDER_H 640
#define ELK_RENDER_X 64             // where the display's first pixel goes (its first line at ELK_RENDER_Y)
#define ELK_RENDER_Y 80
#define ELK_SND_LOG 2048

// the keys: Clock Signal's codes (its Keyboard.hpp), the column (address line A0-A13) in bits 4-7 and the row's bit in 0-3
enum {
    ELK_RIGHT = 0x01, ELK_COPY = 0x02, ELK_SPACE = 0x08, ELK_LEFT = 0x11, ELK_DOWN = 0x12, ELK_RETURN = 0x14,
    ELK_DELETE = 0x18, ELK_MINUS = 0x21, ELK_UP = 0x22, ELK_COLON = 0x24, ELK_0 = 0x31, ELK_P = 0x32,
    ELK_SEMICOLON = 0x34, ELK_SLASH = 0x38, ELK_9 = 0x41, ELK_O = 0x42, ELK_L = 0x44, ELK_FULLSTOP = 0x48,
    ELK_8 = 0x51, ELK_I = 0x52, ELK_K = 0x54, ELK_COMMA = 0x58, ELK_7 = 0x61, ELK_U = 0x62, ELK_J = 0x64, ELK_M = 0x68,
    ELK_6 = 0x71, ELK_Y = 0x72, ELK_H = 0x74, ELK_N = 0x78, ELK_5 = 0x81, ELK_T = 0x82, ELK_G = 0x84, ELK_B = 0x88,
    ELK_4 = 0x91, ELK_R = 0x92, ELK_F = 0x94, ELK_V = 0x98, ELK_3 = 0xa1, ELK_E = 0xa2, ELK_D = 0xa4, ELK_C = 0xa8,
    ELK_2 = 0xb1, ELK_W = 0xb2, ELK_S = 0xb4, ELK_X = 0xb8, ELK_1 = 0xc1, ELK_Q = 0xc2, ELK_A = 0xc4, ELK_Z = 0xc8,
    ELK_ESCAPE = 0xd1, ELK_FUNC = 0xd2, ELK_CTRL = 0xd4, ELK_SHIFT = 0xd8,
};

struct elk {
    struct cpu6502 cpu;
    uint8_t ram[0x8000];
    uint8_t *slot[16];              // ROM and sideways RAM images, 16 KB each (NULL: empty, reads &FF)
    uint8_t slot_w[16];             // writeable (sideways RAM, and the DFS's slot as Clock Signal has it)
    const uint8_t *mos;
    uint8_t empty[0x4000];          // an empty slot (all &FF)
    uint8_t scratch[256];           // where writes to ROM go
    uint8_t *rmap[256], *wmap[256]; // each page's bytes for the CPU (NULL: the I/O path)
    uint8_t slow[256];              // 1 MHz pages (RAM; the keyboard; FRED, JIM, SHEILA)
    uint8_t *null_tab[256];         // the CPU's page tables: every page through the machine (elk.c)

    // the ULA
    uint8_t isr, ier;               // interrupt status (&FE00 read), enables (&FE00 write)
    uint16_t screen_base;           // &FE02/3
    uint8_t mode, fe07, pal_src[8], pal[16];
    uint8_t m40, text;              // the mode's 40 bytes a line, its text rows (until &FE07 is written: 40, graphics)
    uint16_t mode_base;             // where its screen wraps to
    int active;                     // the paged slot (&FE05)
    int kbd, basic;                 // ROM 8/9 (the keyboard) or 10/11 (BASIC) paged
    uint8_t keys[14];               // the matrix: each column's rows held
    uint32_t fpos;                  // the raster: 2 MHz cycles into the field pair (ELK_PAIR)
    uint32_t next_ev;               // fpos of the next ULA event
    uint64_t now;                   // 2 MHz cycles since power on
    uint32_t fields;                // fields completed

    // the tape's shifter (it keeps running while the sound is on: its status bits are the ULA's)
    uint64_t tape_t;
    uint32_t tape_acc;
    uint16_t tape_data;
    int tape_min_full, tape_bits_left, tape_enabled, tape_input, tape_running;
    uint8_t tape_st, tape_posted;

    // the Plus 3
    struct fdc fdc;
    struct disc disc;
    uint64_t fdc_t;
    uint8_t plus3_ctrl;
    int shift_held;                 // SHIFT held for a SHIFT+BREAK boot, let go at the DFS's first 1770 access
    int64_t restart_in;             // cycles to the boot's power-on reset (Clock Signal's shift restart), 0 none

    // sound: the tone channel's writes since the last audio frame
    struct { uint64_t t; uint8_t reg, val; } snd[ELK_SND_LOG];
    int n_snd;
    uint64_t snd_t;                 // the last audio frame's end
    uint8_t snd_div, snd_on;
    uint32_t snd_phase;

    // the picture: the field about to be displayed, rendered at its start (whole frames: the game draws into the other
    // buffer), as colour numbers 0-7 (the BBC's order: bit 0 red, 1 green, 2 blue)
    uint8_t *px;
    int want_px, have_px;
    uint32_t undoc;                 // undocumented opcodes run (none expected)
    // (tools: called at each opcode fetch with the cycles at its end, as clkh's breakpoints see them)
    void (*fetch_hook)(struct elk *e, uint16_t pc, uint64_t cycles);
    void (*key_hook)(struct elk *e, int code, int down);     // (tools: each key going down or up, at e->now)
};

// roms: the MOS, BASIC and the DFS (16 KB each); disc: an .ssd. Sideways RAM is allocated here.
void elk_init(struct elk *e, const uint8_t *mos, const uint8_t *basic, const uint8_t *dfs);
void elk_insert_ssd(struct elk *e, const uint8_t *image, uint32_t size);
// power on; shift_boot: then as Clock Signal boots a disc, a power-on reset 1,000,000 cycles later with SHIFT held
void elk_power_on(struct elk *e, int shift_boot);
// runs to `cycles` 2 MHz cycles since power on
void elk_run(struct elk *e, uint64_t cycles);
void elk_key(struct elk *e, int code, int down);
// a byte as the CPU would see it with `slot` paged (slot -1: whatever is paged; main RAM and the MOS are the same in all)
uint8_t elk_peek(const struct elk *e, int slot, uint16_t addr);
void elk_poke(struct elk *e, int slot, uint16_t addr, uint8_t v);
// the field about to be displayed, the next time one starts: elk_run until have_px
void elk_want_picture(struct elk *e);
// n samples at one per `period` 2 MHz cycles of what the tone channel played since the last call, ±amp
void elk_audio(struct elk *e, int16_t *out, int n, int period, int amp);
// the whole machine to a file and back (tools: elkh's save and load); 1 if it went
int elk_save(const struct elk *e, const char *path);
int elk_load(struct elk *e, const char *path);
// the disc in the drive as an .ssd (what the game saved to it); returns sectors unreadable
int elk_disc_dump(struct elk *e, uint8_t *out, int size);
