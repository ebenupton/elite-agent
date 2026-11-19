// Plays a key log from native_run (KEYLOG=file: the cycle, the Electron key, down or up) into clkh, so that a career
// flown on this folder's Electron can be flown again on Clock Signal's: then shows the same bytes as native_run's DUMP
// at the given cycles, and writes clkh's screen there.
//   node web/electron/replay.mjs KEYLOG CYCLE:HEXADDR:LEN[,...] [--media X.ssd] [--shot PREFIX]
import { readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); if (i < 0) return d; const v = args[i + 1]; args.splice(i, 2); return v; };
const media = opt("--media", `${homedir()}/elite-electron/solid-elite-acorn-electron.ssd`), shot = opt("--shot", null);
const ROMS = process.env.ELK_ROMS || `${homedir()}/elite-electron-roms`;
const { Clkh } = await import(process.env.CLKH_MJS || `${homedir()}/elite-electron/6-harness/clkh.mjs`);
const keys = readFileSync(args[0], "utf8").trim().split("\n").map((l) => l.split(" ").map(Number));
const dumps = (args[1] || "").split(",").filter(Boolean).map((d) => { const [c, a, n] = d.split(":"); return { c: +c, a: parseInt(a, 16), n: +n }; });
const ev = [...keys.map(([c, k, d]) => ({ c, k, d })), ...dumps.map((d) => ({ c: d.c, dump: d }))].sort((x, y) => x.c - y.c);
const m = new Clkh({ disc: media, roms: ROMS, shiftbreak: 1000000 });
let t0 = Date.now();
for (const e of ev) {
    while (m.cycles < e.c) { const r = m.run(e.c - m.cycles); if (r.kind === "E" && m.cycles >= e.c) break; }
    if (e.dump) {
        const b = m.read(e.dump.a, e.dump.n);
        console.log(`@${m.cycles} ${e.dump.a.toString(16).padStart(4, "0")}: ${[...b].map((x) => x.toString(16).padStart(2, "0")).join(" ")}`);
        if (shot) writeFileSync(`${shot}-${e.c}.png`, m.shot());
    } else m.key(e.k, e.d);
}
console.log(`replayed ${keys.length} key changes to cycle ${m.cycles} in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
m.kill();
