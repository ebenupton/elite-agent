#!/bin/bash
# web/modelb/build.sh: the native tools for the Model B (build/): mbh, the machine alone (compare.mjs runs it beside
# jsbeeb), and native_run, the page's whole machine and player (core_wasm.c with the Master, the Tube, the Electron and
# the Model B) built natively. The page's core.wasm, with the Model B in it, is web/tube/build.sh's.
# Needs the emulator's source (scripts/fetch-emulator.sh) and a C compiler (CC, default clang).
set -e
cd "$(dirname "$0")"
CC=${CC:-clang}
KC=../../rp2350-bbc-micro/src/cores/kc-beeb
[ -f $KC/cpu6502.c ] || { echo "no emulator source: run scripts/fetch-emulator.sh"; exit 1; }
mkdir -p build
rm -rf build/kc-beeb && cp -r $KC build/kc-beeb
(cd build/kc-beeb && patch -s -p1 < ../../../tube/kc-beeb-tube.patch && patch -s -p1 < ../../../tube/kc-beeb-modelb.patch)
$CC -O2 -fwrapv -Wno-everything -DMB_TRACE -I../../cplayer -Ibuild/kc-beeb -o build/mbh \
  mbh.c modelb.c ../../cplayer/elite_ver.c
FILES=(); for f in keyq agent cfly combat docked drv elite_ver fair flight galaxy pilot ships show wsp_pilot wsp_dock; do FILES+=(../../cplayer/$f.c); done
$CC -O2 -fwrapv -std=gnu2x -I../../cplayer -Ibuild/kc-beeb -I.. -I../electron -DPLAYER_CURIOUS=0 $NATIVE_FLAGS -Wno-everything \
  -o build/native_run native_run.c ../core_wasm.c ../player_wasm.c ../tube/tube.c ../electron/elk.c ../electron/core_elk.c modelb.c "${FILES[@]}" -lm
ls -l build/mbh build/native_run
