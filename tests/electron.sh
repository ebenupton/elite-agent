#!/bin/sh
# tests/electron.sh: Solid Elite for the Acorn Electron on web/core.wasm, pinned. A 10-game-minute Curious career
# (BITKICK, skew 13, from Jameson) must write the journal tests/electron.sha256 has the hash of, the same with the picture
# drawn every 7th frame (drawing changes nothing), on the disc whose hash is there too: a new disc (the port rebuilt)
# means gen-ev.mjs and --update. Needs the Electron's ROMs (scripts/fetch-electron-roms.sh, or ROMS=) and the disc
# (DISC=). ~30 s.
#   DISC=solid-elite-acorn-electron.ssd tests/electron.sh [--update]
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd); OUT=${TMPDIR:-/tmp}/electron-$$; mkdir -p "$OUT"
ROMS=${ROMS:-$TOP/rp2350-bbc-micro/roms/electron}; DISC=${DISC:?DISC=<solid-elite-acorn-electron.ssd>}
SHA() { command -v sha256sum >/dev/null && sha256sum "$@" || shasum -a 256 "$@"; }
run() { ELK=1 ROMS="$ROMS" DISC="$DISC" MODE=curious NAME=BITKICK SKEW=13 DRAW=$1 node "$TOP/web/run_core.mjs" 10 "$OUT/j$1.jsonl" > "$OUT/run$1.log" 2>&1 || { tail "$OUT/run$1.log"; exit 1; }; }
run 0; run 7
cd "$OUT"
DISCSHA=$(SHA "$DISC" | cut -c1-64); J0=$(SHA j0.jsonl | cut -c1-64); J7=$(SHA j7.jsonl | cut -c1-64)
[ "$J0" = "$J7" ] || { echo "electron: drawing changed the career ($J0, $J7)"; exit 1; }
if [ "$1" = --update ]; then printf '%s  disc\n%s  journal\n' "$DISCSHA" "$J0" > "$TOP/tests/electron.sha256"; echo "updated: journal $(echo $J0 | cut -c1-12)"; exit 0; fi
WANT_DISC=$(grep ' disc$' "$TOP/tests/electron.sha256" | cut -c1-64); WANT=$(grep ' journal$' "$TOP/tests/electron.sha256" | cut -c1-64)
[ "$DISCSHA" = "$WANT_DISC" ] || { echo "electron: not the pinned disc (sha256 $DISCSHA): regenerate the table (web/electron/gen-ev.mjs) and --update"; exit 1; }
if [ "$J0" = "$WANT" ]; then echo "electron: identical ($(grep -c . j0.jsonl) journal lines; $(tail -1 run0.log | sed 's/,.*//'))"
else echo "electron: the journal differs; kept in $OUT/j0.jsonl (this machine's hash $J0)"; exit 1; fi
