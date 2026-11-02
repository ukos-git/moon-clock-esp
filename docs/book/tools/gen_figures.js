#!/usr/bin/env node
/* gen_figures.js — render the book's moon figures from the same JS bake the
 * interactive chapter runs (isomorphic pipeline: no separate "figure engine"
 * that can drift from the reader-facing sim).
 *
 * Writes PPM frames (P6) into the out dir; assemble_figures.py composes the
 * montages with PIL (labels, grids).
 *
 * Usage:  node docs/book/tools/gen_figures.js <out_dir>
 */
"use strict";
const fs = require("fs");
const path = require("path");
const MOONLAB = require("../_static/moonjs/moonlab.js");

const DATA_JS = path.join(__dirname, "..", "_static", "moonjs", "moonlab_data.js");
const OUT = process.argv[2];
if (!OUT) { console.error("usage: gen_figures.js <out_dir>"); process.exit(1); }
fs.mkdirSync(OUT, { recursive: true });

const lab = new MOONLAB.MoonLab(eval(fs.readFileSync(DATA_JS, "utf8").replace("window.MOONLAB_DATA", "globalThis.__X") + ";__X;"));
const SIZE = 466;

function rgbFrame(ms, params, warm) {
  const gray = lab.bake(ms, SIZE, params);
  const lut = warm ? lab._warm565 : lab._neutral565;
  const out = Buffer.alloc(SIZE * SIZE * 3);
  for (let i = 0; i < gray.length; i++) {
    const l = gray[i] * 3;
    out[i * 3] = lut[l];
    out[i * 3 + 1] = lut[l + 1];
    out[i * 3 + 2] = lut[l + 2];
  }
  return out;
}
function writePpm(name, rgb) {
  const head = Buffer.from(`P6\n${SIZE} ${SIZE}\n255\n`);
  fs.writeFileSync(path.join(OUT, name), Buffer.concat([head, rgb]));
}
const T = (s) => Date.parse(s + (s.length <= 10 ? "T12:00:00Z" : "Z"));

// --- 1. phase strip: ~4-day steps across one synodic month (8 cells) -------
// 2026 new moon ~ Jan 17-18, first quarter ~ Jan 25, full ~ Feb 2, last
// quarter ~ Feb 10 (from the validated ephemeris table).
const phaseDates = ["2026-01-17", "2026-01-21", "2026-01-25", "2026-01-29",
                    "2026-02-02", "2026-02-06", "2026-02-10", "2026-02-14"];
phaseDates.forEach((d, i) => {
  const p = lab.phaseLabel(T(d));
  writePpm(`phase_${String(i).padStart(2, "0")}.ppm`, rgbFrame(T(d), { K: 0.2, soft: 0.03, relief: true, backlit: true }, true));
  console.log("phase_", String(i).padStart(2, "0"), " ", d, " ", p.name, "  illum ", (100 * p.illum).toFixed(1), "%");
});

// --- 2. K A/B: backlit dark-side level, waxing crescent 2026-04-15T12:00 ----
[0.05, 0.10, 0.20, 0.35].forEach((k, i) => {
  writePpm(`k_${String(i).padStart(2, "0")}.ppm`, rgbFrame(T("2026-04-15T12:00"), { K: k, soft: 0.03, relief: true, backlit: true }, true));
});

// --- 3. art pass A/B: earthshine vs backlit, neutral vs warm ----------------
writePpm("art_0.ppm", rgbFrame(T("2026-04-15T12:00"), { K: 0.2, soft: 0, relief: true, backlit: false }, false));
writePpm("art_1.ppm", rgbFrame(T("2026-04-15T12:00"), { K: 0.2, soft: 0, relief: true, backlit: false }, true));
writePpm("art_2.ppm", rgbFrame(T("2026-04-15T12:00"), { K: 0.2, soft: 0, relief: true, backlit: true }, false));
writePpm("art_3.ppm", rgbFrame(T("2026-04-15T12:00"), { K: 0.2, soft: 0.03, relief: true, backlit: true }, true));

// --- 4. relief A/B at a half-ish phase (terminator shadows) ------------------
writePpm("relief_on.ppm", rgbFrame(T("2026-07-08"), { K: 0.2, soft: 0.03, relief: true, backlit: true }, true));
writePpm("relief_off.ppm", rgbFrame(T("2026-07-08"), { K: 0.2, soft: 0.03, relief: false, backlit: true }, true));

// --- 5. neutral vs warm tint, full-ish phase --------------------------------
writePpm("tint_neutral.ppm", rgbFrame(T("2026-10-15"), { K: 0.2, soft: 0.03, relief: true, backlit: true }, false));
writePpm("tint_warm.ppm", rgbFrame(T("2026-10-15"), { K: 0.2, soft: 0.03, relief: true, backlit: true }, true));

console.log("wrote 20 PPM frames to " + OUT);
