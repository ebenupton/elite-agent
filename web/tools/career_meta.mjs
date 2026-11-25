#!/usr/bin/env node
// What produced a career, as JSON beside its journal: enough to fly it again.
//   career_meta.mjs --game flicker --skew 13 --disc d.ssd --wasm core.wasm --minutes 2400 > career.meta.json
// Stops if the disc isn't the pinned one for that game.
import { execSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import os from "node:os";

const o = {};
{ const a = process.argv.slice(2); for (let i = 0; i < a.length; i += 2) o[a[i].slice(2)] = a[i + 1]; }
const sha = (f) => createHash("sha256").update(readFileSync(f)).digest("hex");
const git = (dir, args) => { try { return execSync(`git ${args}`, { cwd: dir, stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); } catch { return null; } };
const here = new URL(".", import.meta.url).pathname;

// (the discs at the page's pins: web/pins.json, its sha256 written there by tests/games.mjs)
const PINS = JSON.parse(readFileSync(`${here}../pins.json`, "utf8")).games;
const NAMES = { flicker: "Flicker-free Elite (SNG47)", solid: "Solid Elite" };
const DISCS = Object.fromEntries(Object.entries(NAMES).map(([k, name]) => [k, { name, repo: PINS[k].repo, commit: PINS[k].commit, path: PINS[k].disc, sha256: PINS[k].sha256 }]));
const disc = DISCS[o.game];
if (!disc) { console.error(`unknown game ${o.game}`); process.exit(2); }
const discSha = sha(o.disc);
if (discSha !== disc.sha256) { console.error(`${o.disc} isn't the pinned ${o.game} disc (sha256 ${discSha})`); process.exit(1); }

console.log(JSON.stringify({
    repo: "github.com/8bitkick/elite-agent",
    commit: git(here, "rev-parse HEAD"),
    uncommitted_changes: git(here, "status --porcelain") !== "",
    game: o.game, skew: +o.skew,
    params: { mode: "Fair", commander: "BITKICK", machine: "Master", minutes_allowed: +o.minutes, frames_per_game_minute: 3000 },
    core_wasm: { sha256: sha(o.wasm), source: o["wasm-source"] || "web/core.wasm at the commit above" },
    disc,
    emulator: { name: "Kieran Connell's Master core", repo: "8bitkick/rp2350-bbc-micro", commit: "b8232984d0e02350c6f1434374e1d9a0fad09dbf" },
    roms: { repo: "scarybeasts/beebjit", commit: "db16aaf09df1fa0fd7ea686bc8d09b13f5674a42", path: "roms/mos3.20" },
    host: { platform: `${os.platform()} ${os.arch()}`, cpu: os.cpus()[0]?.model, node: process.version },
    started: new Date().toISOString(),
}, null, 2));
