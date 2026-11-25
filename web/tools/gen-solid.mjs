#!/usr/bin/env node
// gen-solid.mjs solid|tube REPO_DIR [OUT.h]: the player's table for one of Eben Upton's Solid Elite builds, from the build's
// symbols and binaries, written to cplayer/elite_ver_solid.h or cplayer/elite_ver_tube.h. Run it again whenever the
// game the page flies is re-pinned: the ports move their state and code about.
//   solid: Solid Elite for the BBC Master (github.com/ebenupton/solid-elite-bbc-master), built with make: its
//          3-assembled-output (symbols.txt, banks.txt, BCODE.bin, BANK6.bin)
//   tube:  Solid Elite for the 6502 Second Processor (github.com/ebenupton/solid-elite-6502-second-processor), built with
//          master/build.sh and tube/build.sh: tube/build (psyms.txt, the parasite's symbols, and PCODE.bin, its image)
//
// Both are SNG47's game logic: every address the player reads (elite_ver.h's A_ fields) is looked up by name, and must be
// in the RAM the player is given (the Master's main RAM; the parasite's &0000-&7FFF). The rest of the table (NI%, the save
// check's length, the title screens, the A key's fire, a kill scoring by type) is the Master's.
//
// Recognition: FNV-1a hashes of 256 bytes of code as loaded, which nothing writes to: on the Master, the start of BCODE
// (the glue area) and the main loop's M% (the hot pass, in main RAM); on the parasite, TT100 and MLOOP in its resident
// code. (SIG_AT=hex,hex hashes two other addresses instead: to check against a table picked by hand.)
//
// The frame layer (F_FRAME, once an iteration) borrows the zero page the player reads (&06-&27 and &D2-&E1: ENERGY, the
// shields, QQ8-QQ10, the text cursor) while it draws, keeping the game's own copy, which the player is given instead:
//   solid: in f_zsv in its bank (or, in builds before f_zsv, pushed on the stack under F_FRAME's return addresses), told
//          by those return addresses on the stack (G_FRAME's JSR F_FRAME and hframe's JSR G_FRAME, hframe being called
//          from the main loop's top level), with the banks: the frame's and the rasteriser's borrow it, and the game's
//          while the frame runs game code (T_GAME: GPRJ set)
//   tube:  in f_zsv, between F_FRAME's BIT GMODE and the JSR p_flip after ff_stars9 (web/core_wasm.c finds the two in the
//          parasite's code and web/tube/tube.c watches its PC)
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";

const here = new URL(".", import.meta.url).pathname;
const [game, repoArg, outArg] = process.argv.slice(2);
if (!["solid", "tube"].includes(game) || !repoArg) {
    console.error("usage: gen-solid.mjs solid|tube REPO_DIR [OUT.h]");
    process.exit(1);
}
const repo = repoArg.replace(/\/$/, "");
const dir = game === "solid" ? (existsSync(`${repo}/3-assembled-output`) ? `${repo}/3-assembled-output` : repo)
    : (existsSync(`${repo}/tube/build`) ? `${repo}/tube/build` : repo);
const out = outArg || `${here}../../cplayer/elite_ver_${game}.h`;
const hex = (v, n = 4) => "0x" + v.toString(16).toUpperCase().padStart(n, "0");
const fail = (m) => { throw new Error(`${dir}: ${m}`); };

const symText = readFileSync(`${dir}/${game === "solid" ? "symbols.txt" : "psyms.txt"}`, "utf8");
const S = {};
for (const m of symText.matchAll(/'([^']+)':(\d+)L?/g)) if (!(m[1] in S)) S[m[1]] = +m[2];
const sym = (n) => { if (S[n] === undefined) fail(`no symbol ${n}`); return S[n]; };
let commit = "";
try { commit = execFileSync("git", ["-C", repo, "rev-parse", "--short=7", "HEAD"], { stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); } catch {}

// the image the player's RAM holds the code of, and where it loads
let image, load;
if (game === "solid") {
    const m = symText.match(/S\.BCODE &([0-9A-F]+)/i);
    if (!m) fail("no S.BCODE line (BCODE's load address)");
    image = readFileSync(`${dir}/BCODE.bin`); load = parseInt(m[1], 16);
} else {
    image = readFileSync(`${dir}/PCODE.bin`); load = sym("PCODE_START");
}
const at = (a, n) => {
    if (a < load || a + n > load + image.length) fail(`&${a.toString(16)}: not in the loaded image`);
    return image.subarray(a - load, a - load + n);
};
const fnv = (b) => { let h = 2166136261; for (const x of b) h = Math.imul(h ^ x, 16777619) >>> 0; return h; };

// the address table
const h = readFileSync(`${here}../../cplayer/elite_ver.h`, "utf8");
const i0 = h.indexOf("uint16_t A_");
const fields = [...h.slice(i0, h.indexOf(";", i0)).matchAll(/A_\w+/g)].map((m) => m[0]);
const NAME = { A_K_PC: "K%", A_NA_PC: "NA%" };
const addr = {};
for (const f of fields) {
    const v = sym(NAME[f] || f.slice(2));
    if (v >= 0x8000) fail(`${f}: &${v.toString(16)}, outside the RAM the player is given`);
    addr[f] = v;
}

// recognition
const sigAt = process.env.SIG_AT ? process.env.SIG_AT.split(",").map((s) => parseInt(s, 16))
    : game === "solid" ? [load, sym("M%")] : [sym("TT100"), sym("MLOOP")];
const sig = sigAt.map((a) => { if (a + 256 > 0x8000) fail(`signature at &${a.toString(16)}: past &7FFF`); return { at: a, hash: fnv(at(a, 256)) }; });

// the bytes of f_zsv that F_FRAME keeps (STA f_zsv+i in its code), as a mask
const kept = (code, from, to) => {
    let mask = 0n;
    for (let i = from; i < to - 2; i++) {
        const t = code[i + 1] | code[i + 2] << 8;
        if (code[i] === 0x8D && t >= S.f_zsv && t < S.f_zsv + 0x32) mask |= 1n << BigInt(t - S.f_zsv);
    }
    if (!(mask & 1n << BigInt(0xD7 - 0xD2 + 0x22))) fail("F_FRAME keeps no ENERGY in f_zsv");
    return mask;
};

const name = game === "solid" ? "BBC Master Solid Elite (Eben Upton's port of SNG47)"
    : "Solid Elite for the 6502 Second Processor (Eben Upton's Tube port of SNG47)";
const P = game === "solid" ? "EV_SOLID" : "EV_TUBE";
const symHash = createHash("sha256").update(symText).digest("hex").slice(0, 16);
let txt = `// Generated by web/tools/gen-solid.mjs from ${game === "solid" ? "Solid Elite for the BBC Master" : "Solid Elite for the 6502 Second Processor"}'s build
// (${commit ? `commit ${commit}, ` : ""}symbols sha256 ${symHash}...): do not edit; run it again when the game is re-pinned:
//   node web/tools/gen-solid.mjs ${game} <its repository, built>
#pragma once
#define ${P}_INIT { .id = ${P}, .name = "${name}", \\
    ${fields.map((f) => `.${f} = ${hex(addr[f])}`).join(", ")}, \\
    .ni = 37, .save_cmp = 73, .title_type = {0, 0}, .kills_by_type = 1, .joy_fire_key = 1 }
// recognised by FNV-1a hashes of 256 bytes of code as loaded: ${game === "solid" ? (process.env.SIG_AT ? "(SIG_AT)" : "BCODE's start and M%") : (process.env.SIG_AT ? "(SIG_AT)" : "TT100 and MLOOP")}
#define ${P}_SIG1_AT ${hex(sig[0].at)}
#define ${P}_SIG1 ${hex(sig[0].hash, 8)}u
#define ${P}_SIG2_AT ${hex(sig[1].at)}
#define ${P}_SIG2 ${hex(sig[1].hash, 8)}u
`;

if (game === "solid") {
    // the banks: banks.txt gives each banked label's bank (ROMSEL, &80 for ANDY over it)
    const banks = Object.fromEntries(readFileSync(`${dir}/banks.txt`, "utf8").trim().split("\n").map((l) => l.trim().split(/\s+/)));
    const bankOf = (n) => { if (!banks[n]) fail(`${n}: in no bank (banks.txt)`); return parseInt(banks[n], 16); };
    const gameBank = bankOf("TT100"), frameBank = bankOf("F_FRAME"), coldBank = bankOf("DOENTRY_C");
    // (every other bank with code is the renderer's: they borrow the zero page; the cold bank also holds the beach
    // ball's meridian window, which the frame pages in to draw the planet and the game to set it up: in a space view)
    const draw = [...new Set(Object.values(banks).map((b) => parseInt(b, 16)))].filter((b) => b !== gameBank && b !== coldBank).sort((a, b) => a - b);
    if (!draw.includes(frameBank)) fail("F_FRAME's bank is the game's or the cold pass's");
    // F_FRAME's return addresses: G_FRAME's JSR F_FRAME, hframe's JSR G_FRAME, and what hframe pushes before it
    const jsr = (from, to, what) => {
        const code = at(from, 0x40);
        for (let i = 0; i < code.length - 2; i++) if (code[i] === 0x20 && (code[i + 1] | code[i + 2] << 8) === to) return { a: from + i, pre: [...code.subarray(0, i)] };
        fail(`no JSR ${what} in its first &40 bytes`);
    };
    const rf = jsr(sym("G_FRAME"), sym("F_FRAME"), "F_FRAME in G_FRAME"), rg = jsr(sym("hframe"), sym("G_FRAME"), "G_FRAME in hframe");
    const retF = rf.a + 2, retG = rg.a + 2;
    if (!rg.pre.every((b) => [0x08, 0x48, 0xDA, 0x5A].includes(b))) fail("hframe does more than push before its JSR G_FRAME");
    const retAt = 0x200 - 2 - rg.pre.length - 2 - 2;      // (hframe called with S = &FF: its pushes, then the two JSRs)
    let zsv = 0, zsvBank = frameBank, mask = 0n;
    if (S.f_zsv !== undefined) {
        zsv = S.f_zsv;
        if (zsv < 0x8000 || zsv >= 0xC000) fail("f_zsv: not in the frame layer's bank");
        const b6 = readFileSync(`${dir}/BANK${frameBank.toString(16).toUpperCase()}.bin`);      // (the frame layer's image, from &8000)
        const f0 = sym("F_FRAME") - 0x8000, ff9 = sym("ff_stars9") - 0x8000;
        mask = kept(b6, f0, f0 + 0x140);
        let back = 0n;      // (and puts back the same bytes after ff_stars9)
        for (let i = ff9; i < ff9 + 0x140; i++) { const t = b6[i + 1] | b6[i + 2] << 8; if (b6[i] === 0xAD && t >= zsv && t < zsv + 0x32) back |= 1n << BigInt(t - zsv); }
        if (back !== mask) fail("F_FRAME puts back other bytes than it keeps");
    }
    txt += `// the frame layer: the banks that borrow the zero page (the game's is ${gameBank}, the cold pass's ${coldBank}; in a space view the cold
// bank is the beach ball's meridian window, which borrows it too); with the game's bank paged, the frame running game code
// (T_GAME) while GPRJ is set
#define EV_SOLID_HELD(bank, qq11) (${draw.map((b) => `(bank) == ${hex(b, 2)}`).join(" || ")} || ((bank) == ${hex(coldBank, 2)} && (qq11) == 0))
#define EV_SOLID_BANK_GAME ${hex(gameBank, 2)}
#define EV_SOLID_GPRJ ${hex(S.GPRJ !== undefined && S.T_GAME !== undefined ? S.GPRJ : 0)}
// inside F_FRAME: its return addresses on the stack from here (G_FRAME's JSR at &${(retF - 2).toString(16).toUpperCase()}, hframe's at &${(retG - 2).toString(16).toUpperCase()}), low byte first
#define EV_SOLID_RET_AT ${hex(retAt)}
#define EV_SOLID_RET { ${[retF & 255, retF >> 8, retG & 255, retG >> 8].join(", ")} }
// where F_FRAME keeps the game's &06-&27 then &D2-&E1: f_zsv, in the frame layer's bank (0: pushed on the stack below the return addresses),
// the bytes of it that it keeps (bit n: f_zsv + n)
#define EV_SOLID_ZSV_BANK ${zsvBank}
#define EV_SOLID_ZSV ${hex(zsv)}
#define EV_SOLID_ZSV_KEPT ${zsv ? hex(Number(mask), 13) + "ull" : "0x3FFFFFFFFFFFFull"}
`;
} else {
    const f0 = sym("F_FRAME"), ff9 = sym("ff_stars9");
    const mask = kept(image, f0 - load, f0 - load + 0x140);
    txt += `// the frame layer: F_FRAME keeps the game's &06-&27 then &D2-&E1 in f_zsv from its BIT GMODE, and puts them back before
// the JSR p_flip after ff_stars9 (web/core_wasm.c looks for the two from F_FRAME and ff_stars9); the bytes of f_zsv it
// keeps (bit n: f_zsv + n)
#define EV_TUBE_F_FRAME ${hex(f0)}
#define EV_TUBE_FF_STARS9 ${hex(ff9)}
#define EV_TUBE_GMODE ${hex(sym("GMODE"))}
#define EV_TUBE_P_FLIP ${hex(sym("p_flip"))}
#define EV_TUBE_F_ZSV ${hex(sym("f_zsv"))}
#define EV_TUBE_ZSV_KEPT ${hex(Number(mask), 13)}ull
// the stick's readings (the host never sends them: the machine puts them there), and the first title screen's bytes
#define EV_TUBE_JOPOS ${hex(sym("JOPOS"))}
#define EV_TUBE_QQ11 ${hex(sym("QQ11"))}
#define EV_TUBE_QQ12 ${hex(sym("QQ12"))}
#define EV_TUBE_DISTAWAY ${hex(sym("distaway"))}
`;
}
writeFileSync(out, txt);
console.log(`${out}: ${fields.length} addresses${commit ? ` (${commit})` : ""}; signatures at &${sig[0].at.toString(16)} and &${sig[1].at.toString(16)}`);
