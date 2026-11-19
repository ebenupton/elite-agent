#!/bin/bash
# web/electron/build.sh: the native tools for the Electron (build/): elkh, the machine alone with clkh's commands
# (compare.mjs runs it against clkh), and native_run, the page's whole machine and player (core_wasm.c with the Master,
# the Tube and the Electron) built natively. The page's core.wasm, with the Electron in it, is web/tube/build.sh's.
# Needs the emulator's source (scripts/fetch-emulator.sh) and a C compiler (CC, default clang).
set -e
cd "$(dirname "$0")"
CC=${CC:-clang}
KC=../../rp2350-bbc-micro/src/cores/kc-beeb
[ -f $KC/cpu6502.c ] || { echo "no emulator source: run scripts/fetch-emulator.sh"; exit 1; }
mkdir -p build
$CC -O2 -fwrapv -Wall -Wno-unused-function -I$KC -o build/elkh elkh.c elk.c
# (native_run: the page's machine, the Master's core patched for the Tube as web/tube/build.sh does)
rm -rf build/kc-beeb && cp -r $KC build/kc-beeb && (cd build/kc-beeb && patch -s -p1 < ../../../tube/kc-beeb-tube.patch)
FILES=(); for f in keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock; do FILES+=(../../cplayer/$f.c); done
$CC -O2 -fwrapv -std=gnu2x -I../../cplayer -Ibuild/kc-beeb -I.. -I. -DPLAYER_CURIOUS=0 -Wno-everything \
  -o build/native_run native_run.c ../core_wasm.c ../player_wasm.c ../tube/tube.c elk.c core_elk.c "${FILES[@]}" -lm
ls -l build/elkh build/native_run
