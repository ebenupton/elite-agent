#!/usr/bin/env node
// gen-ev.mjs [BUILD_DIR] [OUT.h]: the player's address table for Solid Elite for the Acorn Electron or for the BBC Micro
// (the Model B), from the build's symbols (BUILD_DIR: its 3-assembled-output, by default ~/elite-electron/3-assembled-output),
// written to cplayer/elite_ver_elk.h (the Electron's build, whose build.json says machine "electron") or
// cplayer/elite_ver_mb.h (the Model B's, whose build.json names no machine). Run it again whenever the game is rebuilt:
// the ports move their state about. Both ports are disc Elite with the same layout of programs and banks.
//
// Every address the player reads (elite_ver.h's A_ fields) is looked up by name in the flight and docked programs'
// symbols (which must agree), then in the loader's. The port is BBC Micro disc Elite, so the fields the disc version
// lacks (TALLYL, distaway) stay 0 and the rest of the table (NI%, the save check's length, the title screens' ships, the
// keys-only fire, a kill counting 1) is the disc version's.
//
// The player is given a 32 KB view of the machine (web/electron/core_elk.c, web/modelb/modelb.c): main RAM as it is,
// except &3000-&7FFF (the screens, and on the Model B the dashboard and hot code), which the player never reads. A variable in main RAM is read where it is. A variable the port keeps
// in a sideways RAM bank (an address in &8000-&BFFF, its program's bank role from the banks files) is copied into the
// view each frame from that bank, whatever is paged, the bank's slot read from the loader's byte for that role (BANK4 ..
// BANK7, which PROBE fills at boot); its view address is in &3000 up. Nearby banked variables share one copied window,
// so arrays and blocks keep their layout. Two more windows hold the first bytes of each program's main loop (TT100) in
// its home bank, which is how the player recognises the game.
import { readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { createHash } from "node:crypto";

const here = new URL(".", import.meta.url).pathname;
const dir = (process.argv[2] || `${homedir()}/elite-electron/3-assembled-output`).replace(/\/$/, "");
const loadSyms = (p) => { const s = {}; for (const m of readFileSync(p, "utf8").matchAll(/'([^']+)':(\d+)L?/g)) if (!(m[1] in s)) s[m[1]] = +m[2]; return s; };
const loadBanks = (p) => Object.fromEntries(readFileSync(p, "utf8").trim().split("\n").map((l) => l.trim().split(/\s+/)));
const build = JSON.parse(readFileSync(`${dir}/build.json`, "utf8"));
if (build.machine && build.machine !== "electron") throw new Error(`${dir}/build.json: machine ${build.machine}`);
const ELK = build.machine === "electron";
const P = ELK ? "EV_ELK" : "EV_MB";        // (the generated names, and the player's version id)
const NAME = ELK ? "Solid Elite for the Acorn Electron (Eben Upton's Electron port of disc Elite)"
    : "Solid Elite for the BBC Micro (Eben Upton's Model B port of disc Elite)";
const GAME = ELK ? "Solid Elite for the Acorn Electron" : "Solid Elite for the BBC Micro";
const out = process.argv[3] || `${here}../../cplayer/elite_ver_${ELK ? "elk" : "mb"}.h`;
const F = loadSyms(`${dir}/${build.flight.syms}`), D = loadSyms(`${dir}/${build.docked.syms}`), L = loadSyms(`${dir}/${build.loader}`);
const FB = loadBanks(`${dir}/${build.flight.banks}`), DB = loadBanks(`${dir}/${build.docked.banks}`);
const roleSym = build.banks;          // e.g. { H: "BANK4", C: "BANK7", 6: "BANK6" }

const h = readFileSync(`${here}../../cplayer/elite_ver.h`, "utf8");
const fields = [...h.slice(h.indexOf("uint16_t A_"), h.indexOf(";", h.indexOf("uint16_t A_"))).matchAll(/A_\w+/g)].map((m) => m[0]);
const NOT_IN_DISC = new Set(["A_TALLYL", "A_distaway"]);
const SYM = { A_K_PC: "K%", A_NA_PC: "NA%" };
// (how much of each the player reads: arrays and blocks; 1 otherwise)
const LEN = { "K%": 20 * 37, "NA%": 96, TP: 74, FRIN: 21, MANY: 34, AVL: 17, QQ20: 17, NAME: 8, CASH: 4, LASER: 4, TALLY: 2,
    RAND: 4, QQ22: 2, QQ8: 2 };

const where = {};       // name -> { addr, role }
for (const f of fields) {
    if (NOT_IN_DISC.has(f)) continue;
    const n = SYM[f] || f.slice(2);
    const fv = F[n], dv = D[n];
    if (fv !== undefined && dv !== undefined && fv !== dv) throw new Error(`${n}: flight &${fv.toString(16)}, docked &${dv.toString(16)}`);
    const addr = fv ?? dv ?? L[n];
    if (addr === undefined) throw new Error(`${n}: no symbol in the build`);
    let role = null;
    if (addr >= 0x8000 && addr < 0xc000) {
        const fr = fv !== undefined ? FB[n] : null, dr = dv !== undefined ? DB[n] : null;
        if (fr && dr && fr !== dr) throw new Error(`${n}: bank ${fr} in the flight code, ${dr} in the docked code`);
        role = fr || dr;
        if (!role || !roleSym[role]) throw new Error(`${n}: at &${addr.toString(16)}, in no bank the build names`);
    } else if (addr >= 0x3000) throw new Error(`${n}: at &${addr.toString(16)}, in the screens`);
    where[n] = { f, addr, role };
}

// the windows: banked variables grouped by bank, merged when close
const VIEW0 = 0x3000;
let view = VIEW0;
const windows = [];        // { view, src, len, slotAddr, what }
const add = (src, len, role, what) => {
    const slotAddr = L[roleSym[role]];
    if (slotAddr === undefined) throw new Error(`${roleSym[role]}: not in the loader's symbols`);
    windows.push({ view, src, len, slotAddr, what });
    view += len;
    if (view > 0x8000) throw new Error("the windows do not fit in &3000-&7FFF");
};
const sig = [];
for (const [prog, syms, img] of [["docked", D, build.docked.images], ["flight", F, build.flight.images]]) {
    const at = syms.TT100, image = img.find(([, map]) => map === "H");
    if (at === undefined || !image || at < 0x8000) throw new Error(`${prog}: no TT100 in its home bank`);
    const bytes = readFileSync(`${dir}/${image[0]}`).subarray(at - parseInt(image[2], 16), at - parseInt(image[2], 16) + 32);
    let hsh = 2166136261; for (const b of bytes) hsh = Math.imul(hsh ^ b, 16777619) >>> 0;
    sig.push({ view, hash: hsh });
    add(at, 32, "H", `${prog} TT100`);
}
const banked = Object.entries(where).filter(([, w]) => w.role).sort((a, b) => a[1].role.localeCompare(b[1].role) || a[1].addr - b[1].addr);
for (let i = 0; i < banked.length;) {
    const role = banked[i][1].role, start = banked[i][1].addr;
    let end = start + (LEN[banked[i][0]] || 1), names = [banked[i][0]];
    let j = i + 1;
    while (j < banked.length && banked[j][1].role === role && banked[j][1].addr <= end + 32) {
        end = Math.max(end, banked[j][1].addr + (LEN[banked[j][0]] || 1)); names.push(banked[j][0]); j++;
    }
    const v0 = view;
    add(start, end - start, role, names.join(" "));
    for (const n of names) where[n].view = v0 + (where[n].addr - start);
    i = j;
}

const lines = fields.map((f) => {
    const n = SYM[f] || f.slice(2), w = where[n];
    const v = !w ? 0 : w.role ? w.view : w.addr;
    return `.${f} = 0x${v.toString(16).toUpperCase().padStart(4, "0")}`;
});
const srcHash = createHash("sha256").update(readFileSync(`${dir}/${build.flight.syms}`)).update(readFileSync(`${dir}/${build.docked.syms}`)).update(readFileSync(`${dir}/${build.loader}`)).digest("hex").slice(0, 16);
writeFileSync(out, `// Generated by web/electron/gen-ev.mjs from ${GAME}'s symbols (3-assembled-output, symbols
// sha256 ${srcHash}...): do not edit; run it again when the game is rebuilt.
// ${Object.values(where).filter((w) => w.role).length} of the player's addresses are in sideways RAM banks (${banked.map(([n, w]) => `${n} ${w.role}:&${w.addr.toString(16)}`).join(", ") || "none"}),
// the rest in main RAM.
#pragma once
#define ${P}_INIT { .id = ${P}, .name = "${NAME}", \\
    ${lines.join(", ")}, \\
    .ni = 37, .save_cmp = 73, .title_type = {11, 19}, .kills_by_type = 0, .joy_fire_key = 0 }
// the player's view (${ELK ? "web/electron/core_elk.c" : "web/modelb/modelb.c"}): main RAM, with these windows copied in each frame from sideways RAM: the
// view address, the bank's address (&8000-&BFFF), the length, and the main RAM byte that holds the bank's slot
#ifndef EV_WINDOW_T
#define EV_WINDOW_T
typedef struct { uint16_t view, src, len, slot_at; } ev_window_t;
#endif
#define ${P}_WINDOWS { \\${windows.map((w) => `\n    {0x${w.view.toString(16).toUpperCase()}, 0x${w.src.toString(16).toUpperCase()}, ${w.len}, 0x${w.slotAddr.toString(16).toUpperCase().padStart(4, "0")}},     /* ${w.what} */ \\`).join("")}\n}
// the game recognised: either program's first 32 bytes of TT100, as its window has them (FNV-1a)
#define ${P}_SIG_D_AT 0x${sig[0].view.toString(16).toUpperCase()}
#define ${P}_SIG_D 0x${sig[0].hash.toString(16).toUpperCase().padStart(8, "0")}u
#define ${P}_SIG_F_AT 0x${sig[1].view.toString(16).toUpperCase()}
#define ${P}_SIG_F 0x${sig[1].hash.toString(16).toUpperCase().padStart(8, "0")}u
// the loader's slots of the four banks (BANK7, BANK6, BANK5, BANK4 from here)
#define ${P}_BANKS 0x${L.BANKS.toString(16).toUpperCase().padStart(4, "0")}
`);
console.log(`${out}: ${fields.length} addresses, ${banked.length} in banks; ${windows.length} windows (${view - VIEW0} bytes)`);
