# Solid Elite for the BBC Micro

`?game=modelb` flies Eben Upton's [Solid Elite for the BBC Micro](https://github.com/ebenupton/solid-elite-bbc-micro)
(BBC Micro disc Elite on a Model B with 64K of sideways RAM, filled and sunlit, a 2 MHz four-colour screen over the
original's 1 MHz dashboard) on an emulated BBC Model B, in the same `core.wasm` as the Master, its Second Processor and the
Electron, with the same player. Curious mode is the page's default here too.

## The machine ([modelb.c](modelb.c), [../tube/kc-beeb-modelb.patch](../tube/kc-beeb-modelb.patch))

Kieran Connell's Master core, built a second time as a Model B: `modelb.c` compiles the core's `machine.c` (and with it the
6502, the 6845, the VIAs, the 1770 and the disc) with `MACHINE_MODEL_B`, under other names, beside the Master's. The patch
leaves the Master's own build as it was. As a Model B the machine has

- **the NMOS 6502** (the core's `cpu6502.c` has both; the undocumented opcodes are counted, and the game uses none);
- **MOS 1.20, BASIC II in slot 15 and Acorn's 1770 DFS 2.26 in slot 14**, behind an Acorn 1770 interface at `&FE80` (the
  control latch at `&FE80`, with the chip's own bits, the 1770 at `&FE84`); no shadow RAM, ANDY, HAZEL or CMOS, and a
  four-bit ROM latch;
- **sideways RAM in slots 0-7**, as jsbeeb's Model B has (the game takes the first four free banks down from 15: 7 to 4);
- **SHEILA as the Model B decodes it** and its 1 MHz map (`&FE00-&FE1F`, `&FE40-&FE7F`, `&FEC0-&FEDF`, FRED and JIM: beebjit's
  and jsbeeb's), the ADC at `&FEC0`;
- the Master core's own 6845, video ULA, VIAs, keyboard and SN76489. The game switches the ULA between its 2 MHz and 1 MHz
  clocks twice a field, in mid-line, timed to the cycle by the user VIA's timer 2 and calibrated against the vertical sync
  at start-up: the core's 6845 steps at either clock, tick by tick, so the switches land as the game's calibration puts them.

The boot is the core's: power on with SHIFT held until cycle 3,000,000, so the disc's `!BOOT` runs. The picture is the
core's renderer, a frame's video stream at a time; the machine counts the horizontal syncs since the vertical sync, so a
picture started mid-field puts the renderer's beam where the raster is (the game's field is two 6845 frames, the first with
no vertical sync, and the renderer's own count is stale after undrawn frames). A key the player presses reschedules the
machine, so that the keyboard's interrupt is taken where it falls, whatever is drawn.

## Checked against jsbeeb

jsbeeb is the emulator the game is developed and gated on (its `6-harness`, a "B-DFS1.2"). The comparisons use its "B1770"
without the ADFS: the same three ROMs as this machine, sideways RAM in slots 0-7, an Acorn 1770 (jsbeeb's own model of it).

- **The MOS** ([compare.mjs](compare.mjs), `mbh`): booting to BASIC's prompt, both run the same 172,630 instructions with the
  same registers, each starting one 2 MHz cycle later on this machine than on jsbeeb (from the first 1 MHz access on: the two
  models' CPUs sit on opposite phases of the 1 MHz clock); then an interrupt is taken an instruction apart and the two drift
  by a few hundred cycles.
- **The game** ([jsagent.mjs](jsagent.mjs)): the player flies the game on both at once, from power-on, each given its
  machine's RAM in the same view. Over 10 game minutes (a Curious career: Lave, the trade, the jump to Leesti, docking,
  saving, the jump to Diso) both journals have the same events, purchases and saves, the first within a frame of each other
  and the later ones a few seconds apart (the pilot reacts to the slightly different timing). The game's speed, its main
  loop's iterations every 3,000 frames, is the same on both to within 1%.
- **The screens**: in 395 frames of the space view whose game state is identical on both, the two screen buffers
  (`&1000`, `&4000`) are byte for byte the same in 359 and 380 of them; above them only the rupture's own calibration, seven
  bytes, differs. The pictures, the page's renderer against jsbeeb's video, are the same within 1% of their pixels in 103 of
  the 134 frames compared (the rest mid-flip); this machine's picture mostly sits one scan line lower than jsbeeb's.
- **A difference: the rupture's race with the ADC** (up to 4d265a0; fixed in the game since, below). On this machine the
  split screen broke for stretches of a few hundred frames (two to three in a 10-minute career: the space view shown in
  halves, the dashboard in the space view's colours, and in Fair mode, which reads the screen, phantom fights); on
  jsbeeb it never does (every field's two rupture interrupts, over 18,000 frames). The cause is in the game, and the two
  machines' timing decides whether it shows: the loader has the MOS sample three ADC channels at 8 bits, so the MOS
  starts a 4 ms conversion at each 100 Hz tick and chains two more, and the third ends about 24,500 cycles after the
  tick, just before the next rupture interrupt when the period is the 192-line one. `IRQG` takes the end of conversion,
  finds timer 1 not yet fired and passes the interrupt to the MOS; if timer 1 fires in the 70 or so cycles before the
  MOS reads the flags, the MOS services it as its clock tick and clears it, `RUPT` never runs, frame B never gets its
  registers and the 6845 loses its vertical sync for four fields. Here the third end of conversion lands about 84 cycles
  before the interrupt, inside that window; with the ADC's interrupts held off the picture never breaks. On jsbeeb it
  lands just outside. A real Model B could be either.
- **The fix landed** in the game: Solid Elite for the BBC Micro's 89ec7d3 ("Interrupts: the MOS never sees the rupture's
  timer when IRQG passes it another"), in b4b51aa, the commit the page now flies. `IRQG` passes such an interrupt on with
  timer 1 hidden in the MOS's mask for the system VIA (`&0279`), so a timer 1 that fires meanwhile stays pending and is
  taken as the rupture's when the MOS returns. The player carried no workaround for the race, so nothing was dropped here.

      web/modelb/build.sh                                          # build/mbh, build/native_run
      node web/modelb/compare.mjs --noboot --every 500 E581 3000   # the MOS to BASIC's prompt, on both
      CMP=2200:7000 node web/modelb/jsagent.mjs solid-elite-bbc-micro.ssd 30000 curious 13

## The player on the Model B

- **Its view and addresses**, as the Electron's ([../electron/README.md](../electron/README.md)): main RAM, with windows of
  sideways RAM at `&3000` up for any variable the port keeps in a bank, read from the bank's slot as the loader recorded it
  (`BANK4`..`BANK7`). [../electron/gen-ev.mjs](../electron/gen-ev.mjs) writes the table from the build's symbols into
  [../../cplayer/elite_ver_mb.h](../../cplayer/elite_ver_mb.h); at b4b51aa every address the player reads is in main RAM,
  and the windows hold only each program's `TT100`, by which the player recognises the game.
- **Its keys** are the BBC Micro's own, straight onto the matrix; disc Elite is a keyboard game, so the player flies on keys.
- **The game is disc Elite** to the player (`ev_disc()`): its commander file is the disc version's, kept apart from the
  Master games' in the browser.

## ROMs

MOS 1.20, BASIC II and the Acorn 1770 DFS 2.26 are Acorn's and not in this repository: the page fetches them from Matt
Godbolt's [jsbeeb](https://github.com/mattgodbolt/jsbeeb) at a pinned commit (`roms` in [../pins.json](../pins.json);
`?mbroms=<base url>` gives others). For the native and headless runs, put them in a folder as `os12.rom`, `basic2.rom` and
`dfs226.rom` (`tests/games.mjs` does this from the pinned commit).

## Running

    WASI_SDK=/path/to/wasi-sdk web/tube/build.sh             # web/core.wasm, with the Model B in it
    MB=1 ROMS=<roms folder> DISC=solid-elite-bbc-micro.ssd MODE=curious NAME=BITKICK node web/run_core.mjs 10
    web/modelb/build/native_run <roms folder> solid-elite-bbc-micro.ssd 30000      # SHOT=n for PPM pictures

Licences: as for the rest of `core.wasm`, the page as a whole is offered under the GPL.
