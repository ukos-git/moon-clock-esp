#!/usr/bin/env node
/* perf_bench_moonlab.js — time the browser sim's JS bake (V8, scalar,
 * float64) across disk sizes, for docs/verification/performance.rst.
 *
 * The third comparator next to the host C bench (bench.c) and the Python
 * prototype: the *same* line-faithful port the reader drives in
 * docs/science/interactive (moonlab.js), run under Node. The constructor
 * cost (geometry table + relief build + LUTs) is measured once, apart.
 *
 * Emits one line per size on stdout, for perf_measure.py to parse:
 *   js <size> <median_ms>
 * plus   js relief <median_ms>   (the one-time relief build)
 *
 * Usage:  node docs/tools/perf_bench_moonlab.js --utc 2026-04-15T12:00 \
 *             --iters 5 --sizes 200 240 320 400 466 618 650
 */
"use strict";
const fs = require("fs");
const path = require("path");
const MOONLAB = require("../_static/moonjs/moonlab.js");
const DATA_JS = path.join(__dirname, "..", "_static", "moonjs", "moonlab_data.js");

function arg(name, dflt) {
  const i = process.argv.indexOf("--" + name);
  return i > 0 ? process.argv[i + 1] : dflt;
}
function argList(name) {
  const i = process.argv.indexOf("--" + name);
  if (i < 0) return null;
  const out = [];
  for (let j = i + 1; j < process.argv.length && !process.argv[j].startsWith("--"); j++)
    out.push(process.argv[j]);
  return out;
}
const UTC = arg("utc", "2026-04-15T12:00");
const ITERS = parseInt(arg("iters", "5"), 10);
const SIZES = argList("sizes").map(Number);

// the device display path, as the interactive sim runs it (the old table's
// protocol): K 0.20 art pass, soft terminator, computed backlit night side,
// relief on.
const PARAMS = { K: 0.20, soft: 0.03, relief: true, backlit: true };

const data = eval(fs.readFileSync(DATA_JS, "utf8").replace("window.MOONLAB_DATA", "globalThis.__X") + ";__X;");

function median(a) {
  const s = [...a].sort((x, y) => x - y);
  return s[s.length >> 1];
}

// relief build (the constructor's _buildRelief) timed on its own
{
  const t = [];
  for (let i = 0; i < 3; i++) {
    const t0 = performance.now();
    const lab = new MOONLAB.MoonLab(data);
    void lab;
    t.push(performance.now() - t0);
  }
  console.log(`js relief ${median(t).toFixed(3)}`);
}

const lab = new MOONLAB.MoonLab(data);
const ms = Date.parse(UTC + (UTC.length <= 10 ? "T12:00:00Z" : "Z"));
for (const size of SIZES) {
  const t = [];
  for (let i = 0; i <= ITERS; i++) {           // one warm-up iteration
    const t0 = performance.now();
    lab.bake(ms, size, PARAMS);
    const dt = performance.now() - t0;
    if (i > 0) t.push(dt);
  }
  console.log(`js ${size} ${median(t).toFixed(3)}`);
}
