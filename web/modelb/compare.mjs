// This folder's Model B (mbh) against jsbeeb, the emulator Solid Elite for the BBC Micro is developed and gated on (its
// 6-harness), as the same machine: jsbeeb's "B1770" without its ADFS (MOS 1.20, BASIC II, Acorn's 1770 DFS 2.26, the same
// three ROMs), both powered on with SHIFT held until cycle 3,000,000 and the same disc in. At each of the first N opcode
// fetches of PC both report the cycle the instruction starts on; then main RAM and sideways RAM slots 4-7 are compared.
//   node web/modelb/compare.mjs [--media X.ssd] [--noboot] [--every K] PC N   (--noboot: no SHIFT, BASIC's prompt)
// jsbeeb: $JSBEEB (a jsbeeb 1.6 checkout or package), or ~/elite-modelb-dev/6-harness/node_modules/jsbeeb;
// ROMs: $MB_ROMS (os12.rom, basic2.rom, dfs226.rom: jsbeeb's own os.rom, BASIC.ROM and b1770/dfs1770.rom).
import { execFileSync } from "node:child_process";
import { homedir } from "node:os";

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const noboot = args.includes("--noboot"); if (noboot) args.splice(args.indexOf("--noboot"), 1);
const media = opt("--media", null), every = +opt("--every", "1");
const [pcHex, nStr] = args, pc = parseInt(pcHex, 16), n = +nStr;
const ROMS = process.env.MB_ROMS || `${homedir()}/elite-agent-electron/rp2350-bbc-micro/roms/modelb`;
const MBH = process.env.MBH || new URL("build/mbh", import.meta.url).pathname;
const JS = process.env.JSBEEB || `${homedir()}/elite-modelb-dev/6-harness/node_modules/jsbeeb`;

// mbh: the hits' cycles, then the memory
const mArgs = ["--rompath", ROMS, ...(media ? ["--media", media] : []), ...(noboot ? ["--noboot"] : []), "trace-pc", pcHex, String(n), "peek", "0", "32768", "regs"];
for (let s = 4; s < 8; s++) mArgs.push("peekrom", String(s), "8000", "16384");
const out = execFileSync(MBH, mArgs, { maxBuffer: 1 << 28 }).toString().split("\n");
const mHits = out.filter((l) => l.startsWith("hit ")).map((l) => +l.split(" ")[2]);
const mRegs = out.find((l) => l.startsWith("pc "));
const hex = out.filter((l) => /^([0-9a-f]{2} )*[0-9a-f]{2}$/.test(l)).join(" ").replace(/ /g, "");
const mMem = Buffer.from(hex, "hex");          // main RAM, then slots 4-7
const mRam = mMem.subarray(0, 32768), mSlots = [4, 5, 6, 7].map((s, i) => mMem.subarray(32768 + i * 16384, 32768 + (i + 1) * 16384));

// jsbeeb
const { MachineSession } = await import(`${JS}/src/machine-session.js`);
const { allModels, findModel } = await import(`${JS}/src/models.js`);
if (!findModel("B1770-DFS")) {
    const b = findModel("B1770");
    allModels.push(Object.assign(Object.create(Object.getPrototypeOf(b)), b,
        { name: "BBC B with 1770 (DFS only)", synonyms: ["B1770-DFS"], os: ["os.rom", "BASIC.ROM", "b1770/dfs1770.rom"] }));
}
const s = new MachineSession("B1770-DFS");
await s.initialise();
const cpu = s._machine.processor;
if (media) s.loadDisc(media);
const now = () => cpu.cycleSeconds * 2000000 + cpu.currentCycles;
if (!noboot) s.keyDown(16);
const jHits = [];
let shiftUp = noboot;
cpu.debugInstruction.add((addr) => {
    if (!shiftUp && now() >= 3000000) { shiftUp = true; s.keyUp(16); }
    if (addr === pc && jHits.length < n) jHits.push(now());
    return false;
});
while (jHits.length < n) await s.runFor(100000);
const jRam = Buffer.from(cpu.ramRomOs.subarray(0, 32768));
const jSlots = [4, 5, 6, 7].map((sl) => Buffer.from(cpu.ramRomOs.subarray(cpu.romOffset + sl * 16384, cpu.romOffset + (sl + 1) * 16384)));

let worst = 0;
for (let i = 0; i < Math.max(mHits.length, jHits.length); i++) {
    const d = (mHits[i] ?? NaN) - (jHits[i] ?? NaN);
    worst = Math.max(worst, Math.abs(d) || 0);
    if (i % every === 0 || i === n - 1 || Number.isNaN(d)) console.log(`hit ${i + 1}: jsbeeb ${jHits[i]} mbh ${mHits[i]} (${d >= 0 ? "+" : ""}${d})`);
}
console.log(`${pcHex} x${n}: largest difference ${worst} cycles`);
{
    const di = []; for (let i = 1; i < Math.min(mHits.length, jHits.length); i++) di.push((mHits[i] - mHits[i - 1]) - (jHits[i] - jHits[i - 1]));
    const same = di.filter((d) => d === 0).length;
    console.log(`intervals: ${same} of ${di.length} the same; largest difference ${Math.max(0, ...di.map(Math.abs))}`);
}
const fnv = (b) => { let h = 2166136261; for (const x of b) h = Math.imul(h ^ x, 16777619) >>> 0; return h.toString(16).padStart(8, "0"); };
const diffs = (a, b, base) => { const d = []; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) d.push(`&${(base + i).toString(16)} jsbeeb ${b[i].toString(16)} mbh ${a[i].toString(16)}`); return d; };
const rd = diffs(mRam, jRam, 0);
console.log(`main RAM: jsbeeb ${fnv(jRam)} mbh ${fnv(mRam)}${rd.length ? `: ${rd.length} bytes differ: ${rd.slice(0, 24).join(", ")}` : " (same)"}`);
console.log(`jsbeeb regs: pc ${cpu.pc.toString(16)} a ${cpu.a.toString(16)} x ${cpu.x.toString(16)} y ${cpu.y.toString(16)} s ${cpu.s.toString(16)}; mbh: ${mRegs}`);
for (let i = 0; i < 4; i++) {
    const d = diffs(mSlots[i], jSlots[i], 0x8000);
    console.log(`slot ${i + 4}: ${d.length ? `${d.length} bytes differ, the first ${d.slice(0, 4).join(", ")}` : "the same"}`);
}
