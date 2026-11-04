#!/usr/bin/env node
/* gen_bake_intermediates.js — dump the bake pipeline's intermediate maps.
 *
 * Replays the exact per-pixel loop of moonlab.js (the line-faithful JS port
 * of moon_bake.c :: bake_px), but instead of returning only the final 8-bit
 * frame it also writes out each named intermediate as a raw size*size buffer:
 *
 *   d      base-sphere sub-solar cosine            [-1, 1]
 *   lns    unclipped displaced-normal Lambert      ~[-1, 1]
 *   L      clip(lns, 0, 1)                         [0, 1]
 *   A      albedo (nearest-neighbor)               [0, 255]
 *   idx    round(A * L)                            [0, 255]
 *   lutv   MOON_TONE_LUT[idx]                      [0, 255]
 *   es     earthshine(d)                           [0, ~30]
 *   out    the final pixel (dark_zero = 0, gate)   [0, 255]
 *
 * The maps are written as raw bytes (size*size per file, big-endian order
 * preserved = row-major) to the output dir; a companion Python script
 * (gen_bake_strip.py) reads them with numpy and lays them out as a labeled
 * strip figure. This is the "intermediate results of the calculation" the
 * lighting chapter describes one step at a time, made visible.
 *
 * Run: node docs/book/tools/gen_bake_intermediates.js <outdir> [isoDate]
 */
"use strict";
const fs = require("fs");
const path = require("path");

const MOONLAB_DIR = __dirname + "/../_static/moonjs";
globalThis.window = {};
require(MOONLAB_DIR + "/moonlab_data.js");
const { MoonLab } = require(MOONLAB_DIR + "/moonlab.js");

const lab = new MoonLab(globalThis.window.MOONLAB_DATA);
const outdir = process.argv[2] || "/tmp/bake_inter";
const iso = process.argv[3] || "2026-01-10T16:00:00.000Z";
const ms = Date.parse(iso);
fs.mkdirSync(outdir, { recursive: true });

const SIZE = 466; // the book's figure resolution (matches Moon Lab)

/* The per-pixel loop — a copy of MoonLab.prototype.bake with relief on and
 * backlit off (the dark_zero = 0 validated gate model), but capturing every
 * named intermediate. Identical math, same LUT / earthshine / relief cache. */
const D = lab.D;
const g = lab.geometryAt(ms);
const S = g.S, E = g.E, R = g.right, U = g.up;
const alb = lab.alb, lut = lab.lut, pack = lab.pack;
const albW = D.ALB_W, albH = D.ALB_H, demW = D.DEM_W, demH = D.DEM_H;
const perDeg = albW / 360.0;
const RAD2DEG = 57.29577951308232;

const n = SIZE * SIZE;
const d = new Float32Array(n);
const lns = new Float32Array(n);
const L = new Float32Array(n);
const A = new Float32Array(n);
const idx = new Float32Array(n);
const lutv = new Float32Array(n);
const es = new Float32Array(n);
const out = new Uint8Array(n);

function earthshine(dd) {
  const x = -dd > 0 ? -dd : 0;
  return D.ES_A * x + D.ES_B * x * x + D.ES_C * x * x * x;
}
function atan2deg(x, y) {
  let a = Math.atan2(y, x) * RAD2DEG;
  if (a < 0) a += 360;
  return a;
}

for (let y = 0; y < SIZE; y++) {
  const v = (SIZE / 2 - y) / (SIZE / 2);
  const rowOff = y * SIZE;
  for (let x = 0; x < SIZE; x++) {
    const i = rowOff + x;
    const u = (x - SIZE / 2) / (SIZE / 2);
    const r2 = u * u + v * v;
    if (r2 > 1) { d[i] = 1.0; continue; } // off disk: mark as +1 (white) so the disk edge is visible
    const w = Math.sqrt(1 - r2);
    const rx = u * R[0] + v * U[0] + w * E[0];
    const ry = u * R[1] + v * U[1] + w * E[1];
    const rz = u * R[2] + v * U[2] + w * E[2];
    d[i] = rx * S[0] + ry * S[1] + rz * S[2];

    const theta = atan2deg(rx, ry);
    const rowFrac = (90 - Math.asin(rz) * RAD2DEG) / 180;

    // full-disk displaced-normal Lambert (relief on)
    let dcf = (theta + D.TEX_LON_OFFSET) * (demW / 360);
    if (dcf >= demW) dcf -= demW;
    let dc = Math.round(dcf); if (dc >= demW) dc = demW - 1;
    let dr = Math.round(rowFrac * demH); if (dr >= demH) dr = demH - 1;
    const gp = pack[dr * demW + dc];
    const glon = (((gp & 0xff) | ((gp & 0x80) ? -256 : 0)) * 0.003);
    const glat = ((((gp >>> 8) & 0xff) | ((gp & 0x8000) ? -256 : 0)) * 0.003);
    let rho = Math.sqrt(rx * rx + ry * ry); if (rho < 1e-4) rho = 1e-4;
    const inv = 1 / rho;
    const eastS = (-ry * S[0] + rx * S[1]) * inv;
    const northS = (-rz * rx * S[0] - rz * ry * S[1] + rho * S[2]) * inv;
    lns[i] = (d[i] - (glon * eastS + glat * northS)) / Math.sqrt(1 + glon * glon + glat * glat);
    L[i] = lns[i] > 1 ? 1 : lns[i] < 0 ? 0 : lns[i];

    let colf = (theta + D.TEX_LON_OFFSET) * perDeg;
    if (colf >= albW) colf -= albW;
    let col = Math.round(colf); if (col >= albW) col = albW - 1;
    let r = Math.round(rowFrac * albH);
    if (r < 0) r = 0; else if (r >= albH) r = albH - 1;
    A[i] = alb[r * albW + col];

    const ii = Math.round(A[i] * L[i]);
    idx[i] = ii < 0 ? 0 : ii > 255 ? 255 : ii;
    lutv[i] = lut[Math.round(idx[i])];
    es[i] = earthshine(d[i]);

    // final pixel, dark_zero = 0 (the validated gate model)
    const o = (lns[i] <= 0) ? lut[0] + es[i] : lutv[i] + es[i];
    out[i] = o < 0 ? 0 : o > 255 ? 255 : Math.round(o);
  }
}

const write = (name, buf) => fs.writeFileSync(path.join(outdir, name + ".raw"), buf);
write("d", d);

write("lns", lns);
write("L", L);
write("A", A);
write("idx", idx);
write("lutv", lutv);
write("es", es);
write("out", out);
fs.writeFileSync(path.join(outdir, "meta.json"),
  JSON.stringify({ iso: iso, size: SIZE, phase: lab.phaseLabel(ms) }));
console.log("wrote 8 intermediate maps of " + SIZE + "x" + SIZE + " to " + outdir +
            "  (" + lab.phaseLabel(ms).name + ", illum " +
            (100 * lab.phaseLabel(ms).illum).toFixed(1) + "%)");
