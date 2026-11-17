#!/usr/bin/env python
"""Terminator-local fidelity: for candidate terminator models, measure the
metrics that describe the DARK<->BRIGHT shadow region (not whole-disk MAE):
  - 10-90 edge width at the sharpest terminator (NASA ~ 8 px)
  - mean |grad| in a box around the terminator (NASA ~ 27: hard shadows)
  - local contrast in a +/-6 px band around the terminator (NASA ~ 14)
and write a side-by-side crop sheet (NASA vs each candidate)."""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from moon import assets as A
from moon import constants as C
from moon.astro import geometry
from moon.register import register_shift
from moon.validate import _iso
from term_tune import render, q565

REF_DIR = os.path.join(HERE, "final_400x400")
JSON = "/tmp/moon-research/mooninfo_2025.json"
SIMBIN = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))),
                      "host_sim", "build", "moon_sim")
FRAMES = [3000, 2258, 1993]  # 64%, quarter, 25%
HALF = 50
MAG = 3


def term_metrics(ref, img, alpha):
    """img: 400x400 float64 (aligned/registered to ref). Returns (edge_w,
    grad_box, grad_band)."""
    interior = alpha > 0.9
    g = np.abs(np.diff(ref, axis=1))
    w = g * (interior[:, :-1] & interior[:, 1:])
    iy, ix = np.unravel_index(np.argmax(w), w.shape)
    col = ix + 1

    def edge1090(im, row, c):
        prof = im[row, max(0, c - 8):c + 9]
        lo, hi = float(prof.min()), float(prof.max())
        if hi - lo < 10:
            return None
        asc = prof[-1] > prof[0]
        a, b = (lo + 0.1 * (hi - lo), lo + 0.9 * (hi - lo)) if asc \
            else (lo + 0.9 * (hi - lo), lo + 0.1 * (hi - lo))
        j = np.where(prof >= a)[0][0] if asc else len(prof) - 1 - np.where(prof[::-1] >= a)[0][0]
        k = np.where(prof >= b)[0][0] if asc else len(prof) - 1 - np.where(prof[::-1] >= b)[0][0]
        return abs(k - j)

    def gmag(im, c0, c1, r0, r1):
        box = im[r0:r1, c0:c1]; am = interior[r0:r1, c0:c1]
        gy = np.abs(np.diff(box, axis=0)); gx = np.abs(np.diff(box, axis=1))
        gm = np.hypot(gy[:, :-1], gx[:-1, :]); return float(gm[am[:-1, :-1]].mean())

    ew = edge1090(ref, iy, col)
    gbox = gmag(img, max(0, col - 24), min(400, col + 24),
                max(0, iy - 24), min(400, iy + 24))
    gband = gmag(img, max(0, col - 6), min(400, col + 7), 0, 400)
    return ew, gbox, gband, (col, iy)


def main() -> int:
    rec = json.load(open(JSON))
    alb = A.bake_albedo(A.load_albedo())
    dem, lo, hi = A.bake_dem(A.load_height())
    mps = (hi - lo) / A.DEM_STEPS
    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)

    cands = [("cur (0.02 linear)", 0.02, "linear"),
             ("soft 0.20", 0.20, "soft"),
             ("soft 0.35", 0.35, "soft"),
             ("hard 0.35", 0.35, "hard"),
             ("hard full", 9.0, "hard")]

    rows = []
    sheet_rows = []
    for fi in FRAMES:
        ref_rgba = np.asarray(Image.open(os.path.join(REF_DIR, "moon.%04d.png" % (fi + 1))))
        ref = np.asarray(Image.open(os.path.join(REF_DIR, "moon.%04d.png" % (fi + 1))).convert("L"),
                          dtype=np.float64)
        alpha = ref_rgba[..., 3] / 255.
        g = geometry(_iso(rec[fi]["time"]))
        ew_n, gbox_n, gband_n, (tx, ty) = term_metrics(ref, ref, alpha)
        tiles = ["NASA  edge=%s gbox=%.1f gband=%.1f" %
                 (ew_n, gbox_n, gband_n)]
        sheet_rows.append(("NASA", ref, ref, tx, ty))
        for label, band, style in cands:
            raw, d = render(g, alb, dem, mps, band, style)
            img8 = q565(A.to_uint8(A.tonemap(raw, d, lut)))
            shifted, _ = register_shift(img8.astype(np.float64), ref)
            ew, gbox, gband, _ = term_metrics(ref, shifted, alpha)
            tiles.append("%s  edge=%s gbox=%.1f gband=%.1f" % (label, ew, gbox, gband))
            rows.append((fi, label, ew, gbox, gband))
            sheet_rows.append((label, ref, shifted, tx, ty))
        print("frame %d (term x=%d,y=%d)" % (fi + 1, tx, ty))
        for t in tiles:
            print("   " + t)

    # Sheet: rows = frames, columns = [NASA, *cands].
    ncol = len(cands) + 1
    cw = ch = 2 * HALF * MAG
    lab_h = 40
    W = ncol * (cw + 8) + 8
    H = len(FRAMES) * (ch + lab_h) + 8
    sheet = Image.new("L", (W, H), 16)
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    headers = ["NASA"] + [c[0] for c in cands]
    for fc in range(len(FRAMES)):
        base = fc * ncol  # sheet_rows index for this frame
        for cc in range(ncol):
            x = 8 + cc * (cw + 8)
            y = 8 + fc * (ch + lab_h)
            dr.text((x + 2, y + 4), headers[cc], fill=255)
            _label, nasa, sim, tx, ty = sheet_rows[base + cc]
            x0, y0 = max(0, tx - HALF), max(0, ty - HALF)
            arr = (nasa[y0:y0 + 2 * HALF, x0:x0 + 2 * HALF] if cc == 0 else
                   np.clip(sim, 0, 255)[y0:y0 + 2 * HALF, x0:x0 + 2 * HALF])
            sheet.paste(Image.fromarray(arr.astype(np.uint8))
                        .resize((cw, ch), Image.NEAREST), (x, y + lab_h))
    sheet.save("/tmp/term_local.png")
    print("\nwrote /tmp/term_local.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
