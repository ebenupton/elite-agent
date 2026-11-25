# The web page

[8bitkick.cc/elite-agent](https://8bitkick.cc/elite-agent/): the agent flying Fair mode live in the browser.

- **The machine and the agent** are one WebAssembly file, [core.wasm](core.wasm): [Kieran Connell's Master
  core](https://github.com/kieranhj/esp32-beeb) (the emulator on the Presto and in `run_sim`) and the C player from `cplayer/`,
  stepped as `run_sim`'s `step()` is: the player decides on this frame's RAM, the keys due go to the machine, and the machine
  runs 40,000 cycles ([core_wasm.c](core_wasm.c), [player_wasm.c](player_wasm.c), [core.js](core.js)).
- **Pinned:** the site ships the committed `core.wasm`, and `build_site.sh` checks its hash. This fork builds it with
  [tube/build.sh](tube/build.sh) (clang's wasm32-wasi target from the WASI SDK, and the emulator's source from
  `scripts/fetch-emulator.sh`). A new pin means flying the careers in [STARTS.md](STARTS.md) again.
- **Five games**, the `.games` selector or `?game=`: `tube` (the default: Solid Elite on a Master with a 6502 Second
  Processor, [tube/README.md](tube/README.md)), `electron` (Solid Elite for the Acorn Electron, [electron/README.md](electron/README.md)),
  `modelb` (Solid Elite for the BBC Micro, on a Model B, [modelb/README.md](modelb/README.md)), `flicker` ("Wireframe", Mark
  Moxon's flicker-free Master Elite) and `solid` (Solid Elite on the Master).
- **Elite and the ROMs** are fetched by the browser at the commits in [pins.json](pins.json), the one place for them: each
  game's disc from its repository (and the Electron's DFS from its game's), the Master's MOS 3.20 ROMs from beebjit's
  repository, the Second Processor's and the Model B's ROMs from jsbeeb's, the Electron's MOS and BASIC from ElectronFpga.
  This site serves none of them.
- **The Acorn Electron** (`?game=electron`): Solid Elite for the Electron on an emulated Electron with a Plus 3 and sideways
  RAM, in the same `core.wasm` ([electron/README.md](electron/README.md)).
- **The BBC Model B** (`?game=modelb`): Solid Elite for the BBC Micro on Kieran Connell's Master core built as a Model B (MOS
  1.20, a 1770 DFS, sideways RAM), in the same `core.wasm`, checked against jsbeeb ([modelb/README.md](modelb/README.md)).
- **The panel** is the film's, drawn on a canvas in the film's own 1920x1080 frame ([src/panel.js](src/panel.js)): the clock,
  rank, tally, credits, system and the tally chart; the dogfight network (yellow) in a fight, the docking network (cyan) on the
  way in; the galaxy map when docked. A phone gets the rank bar and time above the screen, and tabs: AI, Galaxy, Log and Options.
- **Speed:** one button, tapped round: **1×, 2×, 4×** hold that speed; **ACTION** (the default) is flat out, but 4× while docked, and 1× in fights and
  the docking's final and for 3 s after, the button lit in the colour of the AI flying; **MAX** is flat out, always. Flat out takes 80% of
  each display frame, the drawing included. The page shows the speed it measures. None of it changes the career.
- **Modes:** `?mode=optimal|fair|curious`, or the Mode switch: each flies its own AI pilot models ([wsp/MODES.md](../wsp/MODES.md)); the
  panel draws whichever network decided last.
- **Not on this page yet:** saving and loading a commander and "Jump to Deadly", which the jsbeeb page had. `src/save.js` and
  `chapters/` are kept for them. Sound is wired from the core's SN76489 a frame at a time, but untested by ear.

Licences: Kieran Connell's core is GPL-3.0, so the page as a whole is offered under the GPL too (`LICENSES.txt`); elite-agent's
own code stays MIT. Elite © Ian Bell and David Braben is not part of the site.

Build: `web/build_site.sh` (Node 22) writes `web/_site`; the `pages` workflow publishes it.

Headless: [run_core.mjs](run_core.mjs), or [tools/fly.sh](tools/fly.sh) for a career and its row in [STARTS.md](STARTS.md).
[tools/visit.mjs](tools/visit.mjs) flies the page in a real browser and reports its journal's hash (`?check=` for further).
`node tests/games.mjs` flies each of the five games for 10 game minutes in Node, in Curious and Fair mode and drawing every 7th
frame, and checks each journal's hash and each disc's against [pins.json](pins.json); the page checks a visitor's machine
against the same hashes at 10 game minutes (the JS console says whether it matches).

## Re-pinning

The pins are [pins.json](pins.json): each game's repository, commit and disc path. The player's addresses for the four
Solid games are generated from each game's build, so a new commit of a game needs its build's symbols. For each game whose
commit changes:

1. Put the new commit in `pins.json` (`games.<game>.commit`; and `disc` or `dfs` if a path changed).
2. Build that commit of the game from a clean clone, so that its symbols are the disc's (each build must reproduce its
   repository's disc byte for byte: check with `cmp`), and generate the player's table from it:

       git clone https://github.com/ebenupton/solid-elite-bbc-micro && cd solid-elite-bbc-micro && git checkout <commit>
       make BEEBASM=<beebasm 1.11> PYTHON=python3                       # the Model B; the Electron's and Master's the same
       node web/electron/gen-ev.mjs <clone>/3-assembled-output          # modelb or electron: cplayer/elite_ver_mb.h, _elk.h
       node web/tools/gen-solid.mjs solid <solid-elite-bbc-master clone>             # cplayer/elite_ver_solid.h
       node web/tools/gen-solid.mjs tube <solid-elite-6502-second-processor clone>   # after sh master/build.sh && sh tube/build.sh

   (`flicker`, Mark Moxon's disc, has a hand-written table and is not re-pinned.)
3. Rebuild the core and pin it: `WASI_SDK=<wasi-sdk> web/tube/build.sh`, then `shasum -a 256 web/core.wasm > web/core.wasm.sha256`
   (in `web/`).
4. `node tests/games.mjs --update` (JOBS=3 to fly three at once): every game's 10-minute journals and disc hash into
   `pins.json`. Read the careers' logs it prints: the agent should launch, trade, jump and dock in each.
5. `web/build_site.sh`, and `node web/tools/visit.mjs` with `QUERY=?game=<game>` for each game: the page's journal must be
   the pinned one. Then fly the careers in [STARTS.md](STARTS.md) again for the games it lists.

The dev page, [/dev/](https://8bitkick.cc/elite-agent/dev/), is built by the same workflow from the `wsp-modes` branch (its [wsp/MODES.md](https://github.com/8bitkick/elite-agent/blob/wsp-modes/wsp/MODES.md)): each mode flying its own AI pilots, `?mode=optimal|fair|curious`, and a Deadly button that jumps to a Deadly-rank save. Its fight networks are drawn by pilot, the camera purple and the interceptor red, and ACTION runs them all at 1x. The camera keeps the fight in the picture. The site carries the blog, `blog.html`. A push to that branch doesn't run the workflow: a push under `web/` on main does, or "Run workflow" on it.








