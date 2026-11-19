#!/bin/sh
# Fetches the Acorn Electron's ROMs for the native and headless runs into rp2350-bbc-micro/roms/electron/ (not part of
# this repository; the ROMs are Acorn's and ACP's): MOS 1.00 and BASIC II from hoglet67's ElectronFpga at a pinned
# commit, the same two the page fetches. The Plus 3's ACP 1770 DFS 2.10 is in no public repository: give it as
# ELK_DFS=<a 16 KB file>, or ELK_PLUS3_ZIP=<MAME's electron_plus3.zip>, which holds it as acp_dfs1770_210.rom. The files
# are named as clkh names them (os.rom, basic.rom, DFS-1770-2.20.rom), so the folder serves both. Re-running is safe.
#   [ELK_DFS=file | ELK_PLUS3_ZIP=zip] scripts/fetch-electron-roms.sh
set -e
TOP=$(cd "$(dirname "$0")/.." && pwd); DIR=$TOP/rp2350-bbc-micro/roms/electron
FPGA=https://raw.githubusercontent.com/hoglet67/ElectronFpga/db0971434123bd77c71af6ca0575c36b63510b39/roms
mkdir -p "$DIR"
[ -f "$DIR/os.rom" ] || curl -fsSL -o "$DIR/os.rom" "$FPGA/os100.rom"
[ -f "$DIR/basic.rom" ] || curl -fsSL -o "$DIR/basic.rom" "$FPGA/Basic2.rom"
if [ ! -f "$DIR/DFS-1770-2.20.rom" ]; then
    if [ -n "$ELK_DFS" ]; then cp "$ELK_DFS" "$DIR/DFS-1770-2.20.rom"
    elif [ -n "$ELK_PLUS3_ZIP" ]; then unzip -p "$ELK_PLUS3_ZIP" acp_dfs1770_210.rom > "$DIR/DFS-1770-2.20.rom"
    else echo "no DFS ROM: give ELK_DFS=<file> or ELK_PLUS3_ZIP=<electron_plus3.zip>"; exit 1; fi
fi
SHA1() { command -v sha1sum >/dev/null && sha1sum "$@" || shasum "$@"; }
cd "$DIR" && SHA1 os.rom basic.rom DFS-1770-2.20.rom | while read -r h f; do
    case "$f:$h" in
        os.rom:2863b45dc880a7ed91ad9828795a3eb5ed0bcdd4|basic.rom:4a7393f3a45ea309f744441c16723e2ef447a281|DFS-1770-2.20.rom:c62f1290f689788dad6a2df30ace083eb827cffe) ;;
        *) echo "$f: not the expected ROM (sha1 $h)"; exit 1 ;;
    esac
done
echo "the Electron's ROMs: ready in $DIR"
