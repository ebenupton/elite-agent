// This folder's Electron (elkh) against clkh, the headless Electron on Clock Signal's core that Solid Elite for the
// Electron is developed on: both boot the same disc, and at each of the first N opcode fetches of PC both report the
// 2 MHz cycles at the end of the fetch; then main RAM and the sideways RAM slots are compared.
//   node web/electron/compare.mjs [--media X.ssd] [--shiftbreak N] [--every K] PC N
// clkh: $CLKH, or ~/elite-electron/6-harness/clkh/build/clkh, driven through that repository's clkh.mjs ($CLKH_MJS);
// ROMs: $ELK_ROMS (os.rom, basic.rom, DFS-1770-2.20.rom).
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { homedir } from "node:os";

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const media = opt("--media", `${homedir()}/elite-electron/solid-elite-acorn-electron.ssd`);
const shiftbreak = opt("--shiftbreak", "-1"), every = +opt("--every", "1");
const [pcHex, nStr] = args, pc = parseInt(pcHex, 16), n = +nStr;
const ROMS = process.env.ELK_ROMS || `${homedir()}/elite-electron-roms`;
const ELKH = process.env.ELKH || new URL("build/elkh", import.meta.url).pathname;
const { Clkh } = await import(process.env.CLKH_MJS || `${homedir()}/elite-electron/6-harness/clkh.mjs`);

// elkh: the hits' cycles, then the memory
const eArgs = ["--rompath", ROMS, "--media", media, "--shiftbreak", shiftbreak, "trace-pc", pcHex, String(n), "hash", "0", "32768", "regs"];
for (let s = 0; s < 16; s++) eArgs.push("peekrom", String(s), "8000", "16384");
const out = execFileSync(ELKH, eArgs, { maxBuffer: 1 << 28 }).toString().split("\n");
const eHits = out.filter((l) => l.startsWith("hit ")).map((l) => +l.split(" ")[2]);
const eRam = out.find((l) => /^[0-9a-f]{8}$/.test(l));
const eRegs = out.find((l) => l.startsWith("pc "));
const hexLines = out.filter((l) => /^([0-9a-f]{2} ){15}[0-9a-f]{2}$/.test(l));
const eSlots = [];
for (let s = 0; s < 16; s++) eSlots.push(Buffer.from(hexLines.slice(s * 1024, (s + 1) * 1024).join(" ").replace(/ /g, ""), "hex"));

// clkh: a breakpoint at PC
const c = new Clkh({ disc: media, roms: ROMS, shiftbreak: +shiftbreak < 0 ? (media ? 1000000 : 0) : +shiftbreak });
c.bp(1, pc);
const cHits = [];
while (cHits.length < n) {
    const ev = c.run(1e9);
    if (ev.kind !== "B") break;
    cHits.push(ev.cycles);
}
const fnv = (b) => { let h = 2166136261; for (const x of b) h = Math.imul(h ^ x, 16777619) >>> 0; return h.toString(16).padStart(8, "0"); };
const cRam = c.read(0, 32768);
const cSlots = [];
for (let s = 0; s < 16; s++) cSlots.push(c.read(0x8000, 16384, s));
const r = c.regs();
c.kill();

let worst = 0;
for (let i = 0; i < Math.max(eHits.length, cHits.length); i++) {
    const d = (eHits[i] ?? NaN) - (cHits[i] ?? NaN);
    worst = Math.max(worst, Math.abs(d) || 0);
    if (i % every === 0 || i === n - 1 || Number.isNaN(d)) console.log(`hit ${i + 1}: clkh ${cHits[i]} elk ${eHits[i]} (${d >= 0 ? "+" : ""}${d})`);
}
console.log(`${pcHex} x${n}: largest difference ${worst} cycles`);
{
    // (the intervals between hits: once both machines are past the disc loads, whose lengths differ, the game's own
    // timing shows here)
    const di = []; for (let i = 1; i < Math.min(eHits.length, cHits.length); i++) di.push((eHits[i] - eHits[i - 1]) - (cHits[i] - cHits[i - 1]));
    const same = di.filter((d) => d === 0).length, sum = (a) => a.reduce((x, y) => x + y, 0);
    const ce = eHits.length > 1 ? eHits[eHits.length - 1] - eHits[0] : 0, cc = cHits.length > 1 ? cHits[cHits.length - 1] - cHits[0] : 0;
    console.log(`intervals: ${same} of ${di.length} the same; largest difference ${Math.max(0, ...di.map(Math.abs))}; first hit to last: clkh ${cc}, elk ${ce} (${((ce / cc - 1) * 100).toFixed(4)}%)`);
    void sum;
}
console.log(`main RAM: clkh ${fnv(cRam)} elk ${eRam}${fnv(cRam) === eRam ? " (same)" : " DIFFERENT"}`);
console.log(`clkh regs: pc ${r.pc.toString(16)} a ${r.a.toString(16)} x ${r.x.toString(16)} y ${r.y.toString(16)} s ${r.s.toString(16)}; elk: ${eRegs}`);
for (let s = 0; s < 16; s++) {
    if (Buffer.compare(cSlots[s], eSlots[s]) === 0) continue;
    let first = -1, count = 0;
    for (let i = 0; i < 16384; i++) if (cSlots[s][i] !== eSlots[s][i]) { if (first < 0) first = i; count++; }
    console.log(`slot ${s}: ${count} bytes differ, the first at &${(0x8000 + first).toString(16)}`);
}
if (process.env.RAMDIFF) {
    const eRamBytes = Buffer.from(execFileSync(ELKH, [...eArgs.slice(0, 6), "trace-pc", pcHex, String(n), "peek", "0", "32768"], { maxBuffer: 1 << 28 }).toString().split("\n").filter((l) => /^([0-9a-f]{2} ){15}[0-9a-f]{2}$/.test(l)).join(" ").replace(/ /g, ""), "hex");
    const d = []; for (let i = 0; i < 32768; i++) if (eRamBytes[i] !== cRam[i]) d.push(`&${i.toString(16)} clkh ${cRam[i].toString(16)} elk ${eRamBytes[i].toString(16)}`);
    console.log(`${d.length} RAM bytes differ: ${d.slice(0, 40).join(", ")}`);
}
void createHash;
