// tests/games.mjs: the page's five games on web/core.wasm, pinned. Each game's disc and ROMs are fetched from the commits in
// web/pins.json (kept in $PINCACHE, default $TMPDIR/elite-agent-pins), and a 10-game-minute career (BITKICK, skew 13, from
// Jameson) is flown in Node in each mode (Curious, the page's default, and Fair), and in Curious again drawing every 7th
// frame (drawing must change nothing). Each journal's sha256 and the disc's must be pins.json's; --update writes them there
// (after a re-pin: web/README.md, "Re-pinning"). JOBS=n runs n careers at once (default 1); GAMES=modelb,tube for some.
//   node tests/games.mjs [--update]
import { execFile } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { promisify } from "node:util";

const TOP = new URL("..", import.meta.url).pathname, PINS_FILE = `${TOP}web/pins.json`;
const pins = JSON.parse(readFileSync(PINS_FILE, "utf8"));
const UPDATE = process.argv.includes("--update"), JOBS = +(process.env.JOBS || 1);
const CACHE = process.env.PINCACHE || `${tmpdir()}/elite-agent-pins`;
const GAMES = (process.env.GAMES || Object.keys(pins.games).join(",")).split(",");
const MODES = ["curious", "fair"];
const sha = (b) => createHash("sha256").update(b).digest("hex");
const RAW = (p, path) => `https://raw.githubusercontent.com/${p.repo}/${p.commit}/${path}`;

async function fetchTo(url, file) {
    if (existsSync(file)) return readFileSync(file);
    const r = await fetch(url); if (!r.ok) throw new Error(`${url}: ${r.status}`);
    const b = Buffer.from(await r.arrayBuffer()); writeFileSync(file, b); return b;
}
// the ROMs, under the names run_core.mjs reads, a folder each
const ROMDIR = { master: "master", tube: "master", electron: "electron", modelb: "modelb", flicker: "master", solid: "master" };
const NAMES = { master: ["mos", "dfs", "viewsht", "edit", "basic4", "adfs", "view", "terminal"], electron: ["os", "basic"], modelb: ["os12", "basic2", "dfs226"] };
for (const m of ["master", "electron", "modelb"]) {
    mkdirSync(`${CACHE}/${m}`, { recursive: true });
    await Promise.all(pins.roms[m].map((u, i) => fetchTo(u, `${CACHE}/${m}/${NAMES[m][i]}.rom`)));
}
await fetchTo(pins.roms.tube[0], `${CACHE}/6502Tube.rom`);
await fetchTo(RAW(pins.games.electron, pins.games.electron.dfs), `${CACHE}/electron/DFS-1770-2.20.rom`);
const discs = {};
for (const g of GAMES) {
    const p = pins.games[g]; mkdirSync(`${CACHE}/${g}`, { recursive: true });
    const file = `${CACHE}/${g}/${p.commit.slice(0, 12)}.ssd`;
    discs[g] = { file, sha: sha(await fetchTo(RAW(p, p.disc), file)) };
}

// the careers
const run = promisify(execFile);
const jobs = [];
for (const g of GAMES) for (const mode of MODES) for (const draw of mode === "curious" ? [0, 7] : [0]) jobs.push({ g, mode, draw });
const results = {};
async function fly({ g, mode, draw }) {
    const env = { ...process.env, ROMS: `${CACHE}/${ROMDIR[g]}`, DISC: discs[g].file, MODE: mode, NAME: "BITKICK", SKEW: "13", DRAW: String(draw),
        ...(g === "tube" ? { TUBEROM: `${CACHE}/6502Tube.rom` } : {}), ...(g === "electron" ? { ELK: "1" } : {}), ...(g === "modelb" ? { MB: "1" } : {}) };
    const { stdout } = await run("node", [`${TOP}web/run_core.mjs`, "10"], { env, maxBuffer: 1 << 26 });
    const h = stdout.match(/journal sha256 ([0-9a-f]{64})/)[1];
    const lines = stdout.split("\n").filter((l) => /^\s+\d+\.\d /.test(l));
    results[`${g} ${mode} ${draw}`] = { h, log: lines };
    console.log(`${g.padEnd(8)} ${mode.padEnd(7)} draw ${draw}: ${h}  (${lines.length} log lines; last: ${(lines.at(-1) || "").trim()})`);
}
const queue = jobs.slice();
await Promise.all(Array.from({ length: JOBS }, async () => { while (queue.length) await fly(queue.shift()); }));

let bad = 0;
for (const g of GAMES) {
    const p = pins.games[g], j = Object.fromEntries(MODES.map((m) => [m, results[`${g} ${m} 0`].h]));
    if (results[`${g} curious 7`].h !== j.curious) { console.log(`${g}: drawing changed the career`); bad++; }
    if (UPDATE) { p.sha256 = discs[g].sha; p.journal = j; continue; }
    if (p.sha256 !== discs[g].sha) { console.log(`${g}: not the pinned disc (sha256 ${discs[g].sha}): regenerate (web/README.md, "Re-pinning") and --update`); bad++; }
    for (const m of MODES) if (!p.journal || p.journal[m] !== j[m]) { console.log(`${g} ${m}: the journal differs (this machine's ${j[m]}, pinned ${p.journal?.[m]})`); bad++; }
}
if (UPDATE) { writeFileSync(PINS_FILE, JSON.stringify(pins, null, 2) + "\n"); console.log(`updated ${PINS_FILE}`); }
else console.log(bad ? `${bad} failures` : "all identical");
process.exit(bad ? 1 : 0);
