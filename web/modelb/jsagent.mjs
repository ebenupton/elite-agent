// Solid Elite for the BBC Micro flown by the same player on two machines at once: this folder's Model B in core.wasm
// (as the page and run_core.mjs fly it) and jsbeeb, the game's own emulator (6-harness), as the same machine (its "B1770"
// without the ADFS: MOS 1.20, BASIC II and the 1770 DFS 2.26, sideways RAM in slots 0-7). jsbeeb's player is a second
// instance of core.wasm whose machine is never booted: each frame it is given jsbeeb's RAM as modelb.c gives the page's
// (main RAM, and the windows of sideways RAM that cplayer/elite_ver_mb.h names), and its keys go to jsbeeb's keyboard
// 2,000 cycles into the frame, as modelb.c queues them. Both boot, wait and start as modelb.c's mb_core_start does.
// Each frame the game's state in both views is compared (zero page to &8F, page 3 to &3CF, K% and the line heaps
// &0900-&0CFF, WP &0E00-&0FD1), and the journals; SHOT=f1,f2,... writes both screens' memory and pictures at frames.
//   node web/modelb/jsagent.mjs DISC FRAMES [MODE] [SKEW]       (MB_ROMS, JSBEEB as compare.mjs; OUT=dir for the shots)
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { homedir } from "node:os";
import { Core } from "../core.js";
import { wasiShim } from "../wasi_shim.js";

const [discPath, framesStr, modeStr = "curious", skewStr = "13"] = process.argv.slice(2);
const FRAMES = +framesStr, MODE = { optimal: 0, fair: 1, curious: 2 }[modeStr] ?? +modeStr, SKEW = +skewStr;
const here = new URL(".", import.meta.url).pathname;
const ROMS = process.env.MB_ROMS || `${homedir()}/elite-agent-electron/rp2350-bbc-micro/roms/modelb`;
const JS = process.env.JSBEEB || `${homedir()}/elite-modelb-dev/6-harness/node_modules/jsbeeb`;
const SHOTS = new Set((process.env.SHOT || "").split(",").filter(Boolean).map(Number)), OUT = process.env.OUT || ".";
const wasmBytes = readFileSync(here + "../core.wasm"), disc = readFileSync(discPath);
const roms = ["os12", "basic2", "dfs226"].map((n) => readFileSync(`${ROMS}/${n}.rom`));

// the generated table: the addresses, the windows and the signatures
const H = readFileSync(here + "../../cplayer/elite_ver_mb.h", "utf8");
const A = (n) => parseInt(H.match(new RegExp(`\\.A_${n} = 0x([0-9A-F]+)`))[1], 16);
const WINDOWS = [...H.slice(H.indexOf("EV_MB_WINDOWS")).matchAll(/\{0x([0-9A-F]+), 0x([0-9A-F]+), (\d+), 0x([0-9A-F]+)\}/g)].map((m) => m.slice(1).map((v, i) => i === 2 ? +v : parseInt(v, 16)));
const SIG = ["D", "F"].map((k) => [parseInt(H.match(new RegExp(`EV_MB_SIG_${k}_AT 0x([0-9A-F]+)`))[1], 16), parseInt(H.match(new RegExp(`EV_MB_SIG_${k} 0x([0-9A-F]+)u`))[1], 16)]);
const fnv = (b) => { let h = 2166136261; for (const x of b) h = Math.imul(h ^ x, 16777619) >>> 0; return h >>> 0; };
const STATE = [[0x00, 0x90], [0x300, 0xD0], [0x900, 0x400], [0xE00, 0x1D2]];
const stateOf = (v) => STATE.map(([a, n]) => v.subarray(a, a + n));

// ---- the page's machine
const jA = [], jB = [];
const core = await Core.create(wasmBytes, roms, disc, { modelb: true, note: (j) => jA.push(`${core.frameNo} ${j}`) });
core.name = "BITKICK";
if (!core.start(SKEW, MODE)) throw new Error("core.wasm: no Elite the player knows");

// ---- jsbeeb, and its player
const { MachineSession } = await import(`${JS}/src/machine-session.js`);
const { allModels, findModel } = await import(`${JS}/src/models.js`);
if (!findModel("B1770-DFS")) {
    const b = findModel("B1770");
    allModels.push(Object.assign(Object.create(Object.getPrototypeOf(b)), b, { name: "BBC B with 1770 (DFS only)", synonyms: ["B1770-DFS"], os: ["os.rom", "BASIC.ROM", "b1770/dfs1770.rom"] }));
}
const s = new MachineSession("B1770-DFS");
await s.initialise();
const cpu = s._machine.processor;
s.loadDisc(discPath);
let mem = null, x = null, frameNo = 0;
const pending = [];
const env = { key: (code, down) => pending.push([code, down]), analogue: () => {}, log: () => {},
    note: (p, n) => jB.push(`${frameNo} ${new TextDecoder().decode(new Uint8Array(mem.buffer, p, n))}`) };
({ instance: { exports: x } } = await WebAssembly.instantiate(wasmBytes, { env, wasi_snapshot_preview1: wasiShim(() => new Uint8Array(mem.buffer)) }));
mem = x.memory; x._initialize();
{ const b = new Uint8Array(mem.buffer, x.wasm_name(), 8); b.fill(0); b.set(new TextEncoder().encode("BITKICK")); }
const ram = () => new Uint8Array(mem.buffer, x.wasm_ram(), 0x8000);
const slot = (n) => cpu.ramRomOs.subarray(cpu.romOffset + (n & 15) * 16384, cpu.romOffset + ((n & 15) + 1) * 16384);
const view = () => {
    const v = ram();
    v.set(cpu.ramRomOs.subarray(0, 0x8000)); v.fill(0, 0x3000, 0x8000);
    for (const [at, src, len, slotAt] of WINDOWS) v.set(slot(cpu.ramRomOs[slotAt]).subarray(src - 0x8000, src - 0x8000 + len), at);
    return v;
};
// (the boot: SHIFT held from power-on to cycle 3,000,000, as the Master core's autoboot)
let t = 0;
const now = () => cpu.cycleSeconds * 2000000 + cpu.currentCycles;
const runTo = async (c) => { if (c > now()) await s.runFor(c - now()); };
s.keyDown(16); await runTo(3000000); s.keyUp(16);
const frames = async (n) => { t += 40000 * n; await runTo(t); };
t = 0; await frames(700);
let found = false;
for (let i = 0; i < 40 && !found; i++) { const v = view(); if (SIG.some(([at, h]) => fnv(v.subarray(at, at + 32)) === h)) found = true; else await frames(50); }
if (!found) throw new Error("jsbeeb: the game wasn't recognised");
for (let i = 0; i < 200; i++) { const r = cpu.ramRomOs; if (r[A("QQ12")] && !r[A("QQ11")] && r[A("TYPE")] === 11) break; await frames(5); }
await frames(SKEW);
view();
if (!x.wasm_init(MODE, 1)) throw new Error("jsbeeb's player: no Elite");

// (RUPTS=1: count jsbeeb's entries to the rupture (RUPT, &7700) and to the MOS's T1 service with RUPT skipped)
let rupts = 0, ruptLast = 0;
if (process.env.RUPTS) cpu.debugInstruction.add((a) => { if (a === 0x7700) rupts++; return false; });
// ---- side by side
const dump = (name, a) => writeFileSync(`${OUT}/${name}`, a);
if (SHOTS.size) mkdirSync(OUT, { recursive: true });
let first = -1, same = 0;
// (CMP=from:to: the page's machine draws every frame from..to; at each frame there whose game state is the same on both,
// the two screen buffers (&1000-&3FFF, &4000-&6FFF) and the dashboard (&7000-&7FFF) are compared, and the pictures: each
// is 2 x 2 canvas pixels a pixel of the 2 MHz screen, so the Elite screen (its yellow frame's corner found in each, then the best
// registration within a few canvas pixels, reported) is compared pixel for pixel, as colour numbers, less an 8-pixel margin)
const [cmpFrom, cmpTo] = (process.env.CMP || "0:0").split(":").map(Number);
const cmp = { frames: 0, bufA: 0, bufB: 0, dash: 0, pics: 0, picsSame: 0, picsNear: 0, worst: 0, saved: 0, offsets: {} };
const toIdx = (r, g, b) => (r > 127 ? 1 : 0) | (g > 127 ? 2 : 0) | (b > 127 ? 4 : 0);
const corner = (get, W, H) => { for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (get(x, y) === 3) return [x, y]; return null; };
// (the game's speed on each: its main loop counter MCNT, which every iteration of either program's main loop counts down)
const MCNT = A("MCNT"); let itA = 0, itB = 0, lastA = -1, lastB = -1;
for (let f = 0; f < FRAMES; f++) {
    // core.wasm: its frame (the view, the player, the machine); its view is what the player saw
    const BRK = +(process.env.BROKEN || 0);
    core.frame(SHOTS.has(f) || (f >= cmpFrom && f < cmpTo) || (BRK && f % BRK === 0));
    const vA = core.ram(0, 0x8000);
    // jsbeeb
    frameNo = x.wasm_frame_no();
    const vB = view();
    x.wasm_frame();
    t += 2000; await runTo(t);
    for (const [code, down] of pending.splice(0)) (down ? cpu.sysvia.keyDownRaw : cpu.sysvia.keyUpRaw).call(cpu.sysvia, [code & 15, (code >> 4) & 7]);
    t += 38000; await runTo(t);
    if (lastA >= 0) { itA += (lastA - vA[MCNT]) & 255; itB += (lastB - vB[MCNT]) & 255; }
    lastA = vA[MCNT]; lastB = vB[MCNT];
    const sa = stateOf(vA), sb = stateOf(vB);
    const eq = sa.every((p, i) => Buffer.compare(Buffer.from(p), Buffer.from(sb[i])) === 0);
    if (eq) same++; else if (first < 0) {
        first = f;
        const d = []; STATE.forEach(([a], i) => { for (let k = 0; k < sa[i].length; k++) if (sa[i][k] !== sb[i][k]) d.push(`&${(a + k).toString(16)} ${sb[i][k].toString(16)}/${sa[i][k].toString(16)}`); });
        console.log(`frame ${f}: the game state differs first (jsbeeb/core.wasm): ${d.slice(0, 20).join(", ")}${d.length > 20 ? ` and ${d.length - 20} more` : ""}`);
    }
    if (eq && f >= cmpFrom && f < cmpTo && vA[A("QQ11")] === 0) {
        cmp.frames++;
        const mB = cpu.ramRomOs, ramA = new Uint8Array(core.x.memory.buffer, core.x.core_mb_ram(), 0x8000);   // (the machines' main RAM)
        const eqr = (a, n) => Buffer.compare(Buffer.from(ramA.subarray(a, a + n)), Buffer.from(mB.subarray(a, a + n))) === 0;
        if (eqr(0x1000, 0x3000)) cmp.bufA++;
        if (eqr(0x4000, 0x3000)) cmp.bufB++;
        if (eqr(0x7000, 0x1000)) cmp.dash++;
        else if (!cmp.dashShown) { cmp.dashShown = 1; const d = []; for (let a = 0x7000; a < 0x8000; a++) if (ramA[a] !== mB[a]) d.push(`&${a.toString(16)} ${mB[a].toString(16)}/${ramA[a].toString(16)}`); console.log(`  frame ${f}: &7000-&7FFF differs (jsbeeb/core.wasm) at ${d.length}: ${d.slice(0, 16).join(", ")}`); }
        const px = core.rgba(), fb = s._completeFb8;
        const gA = (x, y) => { const v = px[y * core.width + x]; return toIdx(v & 255, (v >> 8) & 255, (v >> 16) & 255); };
        const gB = (x, y) => { const o = (y * 1024 + x) * 4; return toIdx(fb[o], fb[o + 1], fb[o + 2]); };
        const cA = corner(gA, core.width, core.height), cB = corner(gB, 1024, 625);
        if (cA && cB) {
            cmp.pics++;
            // (registered by the frame's corner, then the best of a few canvas pixels either way, which is reported)
            let bad = Infinity, at = null;
            for (let dy = -6; dy <= 6; dy++) for (let dx = -4; dx <= 4; dx++) {
                let n = 0;
                for (let y = 8; y < 488 && n < bad; y++) for (let x = 8; x < 504; x++) if (gA(cA[0] + x + dx, cA[1] + y + dy) !== gB(cB[0] + x, cB[1] + y)) n++;
                if (n < bad) { bad = n; at = [dx, dy]; }
            }
            cmp.offsets[at.join(",")] = (cmp.offsets[at.join(",")] || 0) + 1;
            if (!bad) cmp.picsSame++; else cmp.worst = Math.max(cmp.worst, bad);
            if (bad < 2460) cmp.picsNear++;
            if (cmp.saved < 2 && (bad || cmp.saved === 0)) {
                cmp.saved++; mkdirSync(OUT, { recursive: true });
                dump(`cmp${f}-core.rgba`, Buffer.from(px.buffer, px.byteOffset, px.byteLength)); dump(`cmp${f}-jsbeeb.rgba`, Buffer.from(fb));
                console.log(`  frame ${f}: pictures ${bad ? `differ in ${bad} of 246,016 canvas pixels` : "the same"}, the page's machine's shifted by ${at} (saved cmp${f}-*.rgba)`);
            }
        }
    }
    // (BROKEN=n: every nth frame, whether each picture is whole: a broken split screen shows the frame's yellow side in
    // its middle)
    if (BRK && f % BRK === 0) {
        const px = core.rgba(), fb = s._completeFb8;
        let ya = 0, yb = 0;
        for (let r = 150; r < 450; r++) { if ((px[r * core.width + 384] & 0xffffff) === 0x00ffff) ya++; const o = (r * 1024 + 520) * 4; if (fb[o] > 127 && fb[o + 1] > 127 && fb[o + 2] < 128) yb++; }
        if (ya > 100 || yb > 100) console.log(`  frame ${f}: picture broken on ${[ya > 100 ? "core.wasm" : "", yb > 100 ? "jsbeeb" : ""].filter(Boolean).join(" and ")}`);
    }
    if (SHOTS.has(f)) {
        dump(`f${f}-core.mem`, Buffer.from(core.ram(0, 0x8000).buffer));
        dump(`f${f}-jsbeeb.mem`, Buffer.from(cpu.ramRomOs.subarray(0, 0x8000)));
        const px = core.rgba(); dump(`f${f}-core.rgba`, Buffer.from(px.buffer, px.byteOffset, px.byteLength));
        dump(`f${f}-jsbeeb.rgba`, Buffer.from(s._completeFb8));
    }
    if (process.env.EVERY && f % +process.env.EVERY === 0) console.log(`  ${f}: it ${itA}/${itB} QQ11 ${vA[A("QQ11")]}/${vB[A("QQ11")]} QQ12 ${vA[A("QQ12")]}/${vB[A("QQ12")]} DELTA ${vA[A("DELTA")]}/${vB[A("DELTA")]} ALTIT ${vA[A("ALTIT")]}/${vB[A("ALTIT")]} MJ ${vA[A("MJ")]}/${vB[A("MJ")]} QQ22 ${vA[A("QQ22")]}/${vB[A("QQ22")]} ENERGY ${vA[A("ENERGY")]}/${vB[A("ENERGY")]}`);
    if (process.env.RUPTS && f % 3000 === 2999) { console.log(`  jsbeeb RUPT entries in the last 3,000 frames: ${rupts - ruptLast} (every field twice: ${(2 * 3000 * 40000 / 39936).toFixed(0)})`); ruptLast = rupts; }
    if (f % 3000 === 2999) { console.log(`frame ${f + 1}: state the same in ${same}; journals ${jA.length}/${jB.length} lines; main loop iterations core.wasm ${itA}, jsbeeb ${itB}`); itA = itB = 0; }
}
if (cmpTo) console.log(`CMP ${cmpFrom}-${cmpTo}: ${cmp.frames} frames in a space view with the same game state on both: screen buffer &1000 the same in ${cmp.bufA}, &4000 in ${cmp.bufB}, the dashboard in ${cmp.dash}; pictures compared in ${cmp.pics}, the same in ${cmp.picsSame}, under 1% different in ${cmp.picsNear}${cmp.worst ? `, at worst ${cmp.worst} canvas pixels differ` : ""}; the page's machine's picture shifted by (canvas pixels: count) ${JSON.stringify(cmp.offsets)}`);
// (the journals: line by line, the events themselves, and the frames they came on)
const ev = (l) => l.slice(l.indexOf(" ") + 1), fr = (l) => +l.slice(0, l.indexOf(" "));
let jsame = 0; while (jsame < Math.min(jA.length, jB.length) && ev(jA[jsame]) === ev(jB[jsame])) jsame++;
const lag = jA.slice(0, jsame).map((l, i) => fr(l) - fr(jB[i]));
console.log(`${FRAMES} frames: the game state the same in ${same} (first difference at ${first < 0 ? "none" : first}); journals: core.wasm ${jA.length} lines, jsbeeb ${jB.length}, the first ${jsame} the same events, ${lag.filter((d) => d === 0).length} of them on the same frame, the rest within ${Math.max(0, ...lag.map(Math.abs))} frames`);
if (jsame < Math.max(jA.length, jB.length)) console.log(`  first different line: core.wasm ${jA[jsame]}\n                        jsbeeb    ${jB[jsame]}`);
writeFileSync(`${OUT}/journal-core.txt`, jA.join("\n") + "\n"); writeFileSync(`${OUT}/journal-jsbeeb.txt`, jB.join("\n") + "\n");
