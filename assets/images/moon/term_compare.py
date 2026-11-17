#!/usr/bin/env python
"""Terminator shadow comparison: magnified crops of the dark<->light
boundary, C simulator vs NASA SVS frames.

Usage: term_compare.py [frame_index ...]
(defaults to a crescent / 25% / quarter / 65% / 14% set)

Writes /tmp/term_compare.png: per frame, a row [NASA | SIM] of 3x
nearest-neighbour crops centred on the sharpest terminator edge. The
sim output is cross-correlation registered to the NASA frame first, so
the comparison shows shadow structure, not registration error.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
MOON_PKG = HERE
sys.path.insert(0, MOON_PKG)

from moon.register import register_shift  # noqa: E402
from moon.validate import _iso  # noqa: E402

REF_DIR = os.path.join(MOON_PKG, "final_400x400")
JSON = "/tmp/moon-research/mooninfo_2025.json"
SIM = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(MOON_PKG))),
                   "host_sim", "build", "moon_sim")
OUT = "/tmp/term"
MAG = 3
HALF = 50  # crop half-width in px

DEFAULT_FRAMES = [715, 1993, 2258, 3000, 5000]


def sharpest_terminator(gray, mask):
    """Pixel of the steepest dark<->bright transition whose both sides are
    inside the disk (so the disk limb -- also a strong edge -- is excluded)."""
    interior = mask > 0.9
    g = np.abs(np.diff(gray, axis=1))  # (H, W-1): g[i,j] = |gray[i,j+1]-gray[i,j]|
    valid = interior[:, :-1] & interior[:, 1:]  # (H, W-1): both transition pixels interior
    weight = g * valid  # the disk limb (one side outside) drops out
    iy, ix = np.unravel_index(np.argmax(weight), weight.shape)
    return ix + 1, iy


def main() -> int:
    import json

    frames = [int(a) for a in sys.argv[1:]] or DEFAULT_FRAMES
    rec = json.load(open(JSON))
    os.makedirs(OUT, exist_ok=True)
    list_path = os.path.join(OUT, "frames.txt")
    with open(list_path, "w") as f:
        for i in frames:
            f.write(_iso(rec[i]["time"]) + "\n")
    subprocess.run([SIM, "--list", list_path, "--size", "400", "--out", OUT],
                   check=True, capture_output=True)

    tiles = []
    for k, i in enumerate(frames):
        ref_rgba = np.asarray(Image.open(os.path.join(REF_DIR, "moon.%04d.png" % (i + 1))))
        ref = np.asarray(Image.open(os.path.join(REF_DIR, "moon.%04d.png" % (i + 1))).convert("L"),
                          dtype=np.float64)
        sim = np.asarray(Image.open(os.path.join(OUT, "sim.%04d.png" % k)).convert("L"),
                          dtype=np.float64)
        shifted, _sh = register_shift(sim, ref)
        alpha = ref_rgba[..., 3]
        tx, ty = sharpest_terminator(ref, alpha / 255.0)
        x0, x1 = max(0, tx - HALF), min(400, tx + HALF)
        y0, y1 = max(0, ty - HALF), min(400, ty + HALF)
        crops = [
            Image.fromarray(ref.astype(np.uint8)[y0:y1, x0:x1])
            .resize(((x1 - x0) * MAG, (y1 - y0) * MAG), Image.NEAREST),
            Image.fromarray(np.clip(shifted, 0, 255).astype(np.uint8)[y0:y1, x0:x1])
            .resize(((x1 - x0) * MAG, (y1 - y0) * MAG), Image.NEAREST),
        ]
        label = "frame %d  illum %s%%" % (i + 1, "%.1f" % rec[i]["phase"])
        tiles.append((label, crops))

    W = 2 * (2 * HALF * MAG) + 10
    H = 2 * HALF * MAG
    rows_px = []
    for label, crops in tiles:
        row = Image.new("L", (W, H + 24), 32)
        d = ImageDraw.Draw(row)
        d.text((6, 6), label, fill=255)
        d.text((2 * HALF * MAG + 12, 6), "SIM (3x, registered)", fill=200)
        row.paste(crops[0], (0, 24))
        row.paste(crops[1], (2 * HALF * MAG + 10, 24))
        d.text((6, 26), "NASA", fill=0)
        rows_px.append(row)

    sheet = Image.new("L", (W, sum(r.height + 8 for r in rows_px)), 0)
    y = 0
    for r in rows_px:
        sheet.paste(r, (0, y))
        y += r.height + 8
    sheet.save("/tmp/term_compare.png")
    print("wrote /tmp/term_compare.png (%dx%d, %d frames)" % (sheet.width, sheet.height, len(tiles)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
