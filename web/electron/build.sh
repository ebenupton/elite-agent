#!/bin/bash
# web/electron/build.sh: the native tools for the Electron (build/): elkh, the machine alone with clkh's commands
# (compare.mjs runs it against clkh).
# Needs the emulator's source (scripts/fetch-emulator.sh) and a C compiler (CC, default clang).
set -e
cd "$(dirname "$0")"
CC=${CC:-clang}
KC=../../rp2350-bbc-micro/src/cores/kc-beeb
[ -f $KC/cpu6502.c ] || { echo "no emulator source: run scripts/fetch-emulator.sh"; exit 1; }
mkdir -p build
$CC -O2 -fwrapv -Wall -Wno-unused-function -I$KC -o build/elkh elkh.c elk.c
ls -l build/elkh
