# Solid Elite for the Acorn Electron

`?game=electron` flies Eben Upton's [Solid Elite for the Acorn Electron](https://github.com/ebenupton/solid-elite-acorn-electron)
(BBC Micro disc Elite, ported to an Electron with a Plus 3 and 64K of sideways RAM, filled and sunlit) on an emulated Acorn
Electron, in the same `core.wasm` as the Master and its Second Processor, with the same player. Curious mode is the page's
default here as for the Tube; the other modes work as they do on the disc version.

## The machine ([elk.c](elk.c), [elk.h](elk.h))

An Acorn Electron with MOS 1.00 and BASIC II, an Acorn Plus 3 (WD1770) with the ACP 1770 DFS 2.10 in ROM slot 0, and
sideways RAM in every other free slot (1-7, 12-15): the machine the game is built for, and the one `clkh` (the headless
Electron on [Clock Signal](https://github.com/TomHarte/CLK)'s core in the game's repository, `6-harness/clkh`) runs it on.

- **The 6502** is Kieran Connell's cycle-stepped core (`cpu6502.c`, NMOS), compiled a second time under other names
  (`elk_cpu6502_*`), with every page of its tables empty, so each access comes through this machine.
- **The bus**, as Clock Signal has it (after hoglet67's ElectronFpga and JamSoft's ULA): ROM and sideways RAM at 2 MHz,
  one cycle an access; RAM, the keyboard (ROM 8/9 paged) and FRED, JIM and SHEILA at the ULA's next 1 MHz slot, three
  cycles from an even cycle of the line and two from an odd one; in MODEs 0-3 a RAM access in the line's fetch waits to
  its end.
- **The ULA**: lines of 128 cycles, fields of 313 and 312 lines; the real time clock interrupt at line 99 cycle 62 of the
  first field and line 98 cycle 126 of the second, display end at line 255 (249 in the text modes) cycle 96, the screen
  start address (`&FE02/3`) latched at the last line's end; the interrupt status, enables and clears (`&FE00`, `&FE05`),
  paging through `&FE05` with its quirk (while ROM 8-11 is paged, only a value with bit 3 set pages), the keyboard as
  the 14 address lines and 4 data lines of ROM 8/9, the palette (`&FE08-&FE0F`), the screen modes, the tone channel
  (`&FE06`, `&FE07`) and the cassette shifter's status bits (it runs while the sound is on, as Clock Signal's does).
  Each access runs the raster over its cycles first, then happens; the 6502 takes an IRQ when the line was up after the
  access two before an instruction's last, as Clock Signal's does.
- **The Plus 3**: Kieran Connell's 1770 and disc (ports of beebjit's, `fdc.c`, `disc.c`, also under other names), its
  latch at `&FCC0` and its registers at `&FCC4-&FCC7`. Clock Signal wires no NMI to it (the 1770 DFS polls), nor does this.
- **The boot**, as Clock Signal boots a disc: power on, then a power-on reset 1,000,000 cycles later with SHIFT held, let
  go at the DFS's first 1770 access. The raster starts 64 cycles before the end of a first field, where Clock Signal's
  CRT leaves it after running the video alone to lock its framing.
- **The picture** is the field about to be shown, rendered from RAM when it starts (so whole frames: the game draws into
  the other buffer), 640 x 256 with the lines doubled in the Master core's 768 x 640 canvas, so the page crops and scales
  it as it does the Master's.

**Checked against clkh** with [compare.mjs](compare.mjs), which boots both on the same disc and compares the cycle counts
at every opcode fetch of a PC (and main RAM and the sideways RAM slots after). Booting to BASIC (no disc boot), every one
of 3,000 passes of the MOS's RAM clear (`&D904`) and 5,000 of its idle loop at the prompt (`&E341`, interrupts and all)
come at the same cycle on both, with main RAM the same. Booting the game, the disc loads take a few hundred cycles more
or less (the 1770s are different models: this one is beebjit's, Clock Signal's its own), and after them the game
locks to the raster again: the title's 50 frames from its 11th on (`RDKEY` at `&95CE`) come at the same cycles on both,
and the screens and the game's state are the same, main RAM differing only in the MOS's clock and stack leftovers.
A career's keys played into clkh ([replay.mjs](replay.mjs)) over 3,000 frames (docked, trading, the launch, a jump, a
fight) leave both with the same game state but for one main loop iteration's counter; over longer stretches the disc loads'
small differences add up, and the careers part.

    web/electron/build.sh                                   # build/elkh, build/native_run
    node web/electron/compare.mjs --shiftbreak 0 D904 3000  # the MOS's RAM clear, on both
    node web/electron/compare.mjs --every 10 95CE 60        # the game's title, on both

## The player on the Electron ([core_elk.c](core_elk.c), [gen-ev.mjs](gen-ev.mjs))

- **Its view.** The player reads a 32 KB RAM image. On the Electron it is given main RAM, except `&3000-&7FFF` (the two
  screens, which it never reads), where windows of sideways RAM are copied in each frame: any of the game's variables the
  port keeps in a bank, read from that bank's slot whatever is paged (the slot from the loader's bank table, `BANK4`..
  `BANK7`, which the loader fills from slot 15 down), and the first bytes of each program's main loop (`TT100`) in its home
  bank, by which it recognises the game.
- **Its addresses** come from the game's build: [gen-ev.mjs](gen-ev.mjs) reads the symbols in its `3-assembled-output`
  (the flight and docked programs', which must agree, then the loader's; the banks files for a banked variable's bank)
  and writes [../../cplayer/elite_ver_elk.h](../../cplayer/elite_ver_elk.h): the address table (`EV_ELK`), the windows
  and the signatures. The port is moving its state into sideways RAM, so this is run again whenever the game is rebuilt:

      node web/electron/gen-ev.mjs ~/elite-electron/3-assembled-output    # from a fresh make: stale symbols don't match the disc

  The rest of the table (keys only, a kill counting 1, the title's ships) is the disc version's, which the port is;
  `ev_disc()` is true for it, so the player plays it as it plays disc Elite.
- **Its keys.** The player presses BBC Micro keys; each goes to the Electron key the port itself reads for it (its `kbkey`
  table, measured on MOS 1.00), a function key to FUNC and its digit, and @ to SHIFT and 0, as the port has them. TAB, the
  energy bomb's key, has no Electron key. There is no joystick: an Electron has no analogue port without a Plus 1, and the
  port's disc version is a keyboard game, so the player flies on keys, as it does disc Elite; `core_analogue` does nothing.
- **Its sound** is the ULA's tone channel, a square wave at 1 MHz / 16 (n + 1), from the writes to `&FE06` and `&FE07`
  during the frame.

## ROMs

The Electron's ROMs are Acorn's and ACP's, and not in this repository. The page fetches MOS 1.00 and BASIC II from
hoglet67's [ElectronFpga](https://github.com/hoglet67/ElectronFpga) at a pinned commit (the same images as clkh's), and
the ACP 1770 DFS from the game's repository (`roms/DFS-1770-2.20.rom`, at the game's pin in `../pins.json`). `?elkdfs=<url>` gives
another, and if the fetch fails the page asks the visitor for their copy (16 KB) once and keeps it in the browser. For
the native and headless runs, `scripts/fetch-electron-roms.sh` fetches the first two and takes the DFS from `ELK_DFS=`
or `ELK_PLUS3_ZIP=`.

## The game's disc

The page fetches `solid-elite-acorn-electron.ssd` from the root of the game's repository at the commit in
[../pins.json](../pins.json) (`?disc=<url>` points it at another copy); re-pinning is in [../README.md](../README.md).

## Running

    scripts/fetch-electron-roms.sh                       # ROMs into rp2350-bbc-micro/roms/electron
    web/electron/build.sh                                # native tools
    WASI_SDK=/path/to/wasi-sdk web/tube/build.sh         # web/core.wasm, with the Electron in it
    ELK=1 ROMS=rp2350-bbc-micro/roms/electron DISC=solid-elite-acorn-electron.ssd MODE=curious NAME=BITKICK node web/run_core.mjs 20
    web/electron/build/native_run rp2350-bbc-micro/roms/electron solid-elite-acorn-electron.ssd 30000
    GAMES=electron node tests/games.mjs                  # pinned 10-minute careers, in Node

`native_run` takes `SHOT=n` (PPM screenshots), `PEEK=n`, `KEYS=1`, `DUMP=frame:addr:len`, `TRACEPC=hex`, `KEYLOG=file`
(for replay.mjs) and `LOG=2`.

Licences: the 6502, the 1770 and the disc are Kieran Connell's (GPL-3.0); the ULA's timing follows Clock Signal (MIT) and
ElectronFpga; as for the rest of `core.wasm`, the page as a whole is offered under the GPL.
