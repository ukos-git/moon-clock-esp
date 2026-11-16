#!/usr/bin/env python3
"""Assemble the site's A/B moon figures from gen_figures.js's PPM frames.

This is step 3 of the isomorphic pipeline documented in
docs/science/interactive.rst ("Isomorphic figures"):

  # 1. The geometry table + textures (needs the gate venv; run when a
  #    constant in the C core changed — moonlab_data.js is tracked)
  host_sim/.venv/bin/python docs/tools/gen_moonjs.py
  # 2. The A/B frames (needs node; writes PPM to a scratch dir)
  node docs/tools/gen_figures.js /tmp/bookfigs
  # 3. The montages (this script, gate venv has PIL)
  host_sim/.venv/bin/python docs/tools/assemble_figures.py /tmp/bookfigs

It composes the four figures the pages embed (docs/_static/figs/):
k_ab.png, art_ab.png, tint_ab.png, phase_strip.png — labeled grids on
the site-dark backdrop, the same bake the reader-facing simulator runs
(no separate figure engine, so a figure can never drift from the sim).

Geometry is pinned to the committed figures: bg #0a0a0e, label band
28 px above every frame (extra 8 px pad on top of the grids), 8 px
gutters, grid cells 340 px, strip cells 240 px, labels 7 px in from
the cell corner.
"""
from __future__ import annotations

import argparse
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.abspath(os.path.join(REPO, "docs", "_static", "figs"))

BG = (10, 10, 14)
TEXT = (227, 227, 232)
GAP = 8
BAND = 28            # label band height above every frame
PAD_TOP = 8          # extra top pad on the 2x2 / 1x2 grids
GRID_CELL = 340      # k_ab / art_ab / tint_ab cell edge (from 466)
STRIP_CELL = 240     # phase_strip cell edge (from 466)
FONT_SIZE = 12

K_VALUES = (0.05, 0.10, 0.20, 0.35)
ART_LABELS = (
    "earthshine, neutral",
    "earthshine, warm",
    "backlit, neutral",
    "backlit + soft terminator (device)",
)
TINT_LABELS = ("neutral LUT", "warm LUT (device)")
# Canonical cycle labels for the 8 evenly spaced dates gen_figures.js walks
# across one synodic month (its phaseLabel names classify the instants —
# the strip labels name the cycle positions the caption promises).
STRIP_LABELS = ("new", "wax cres", "first qtr", "wax gibb",
                "full", "wan gibb", "last qtr", "wan cres")


def _load(scratch: str, name: str, cell: int) -> Image.Image:
    im = Image.open(os.path.join(scratch, name)).convert("RGB")
    return im.resize((cell, cell), Image.LANCZOS)


def _grid(scratch: str, frames: list, labels: list, cols: int,
          cell: int, pad_top: bool, text_dy: int) -> Image.Image:
    rows = (len(frames) + cols - 1) // cols
    w = cols * cell + (cols - 1) * GAP
    pad = PAD_TOP if pad_top else 0
    h = pad + rows * (cell + BAND)   # row gap lives inside the label band
    canvas = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default(size=FONT_SIZE)
    for i, (frame, text) in enumerate(zip(frames, labels)):
        r, c = divmod(i, cols)
        x = c * (cell + GAP)
        frame_top = pad + r * (cell + BAND) + BAND
        canvas.paste(_load(scratch, frame, cell), (x, frame_top))
        draw.text((x + 7, frame_top + text_dy), text, font=font, fill=TEXT)
    return canvas


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("scratch", help="dir holding gen_figures.js's PPM frames")
    ap.add_argument("--out", default=OUT, help="output dir (default: _static/figs)")
    args = ap.parse_args()

    for f in ([f"k_{i:02d}.ppm" for i in range(4)],
              [f"art_{i}.ppm" for i in range(4)],
              ["tint_neutral.ppm", "tint_warm.ppm"],
              [f"phase_{i:02d}.ppm" for i in range(8)]):
        p = os.path.join(args.scratch, f[0])
        if not os.path.isfile(p):
            raise SystemExit(f"missing {p} — run: node docs/tools/gen_figures.js {args.scratch}")

    os.makedirs(args.out, exist_ok=True)
    figs = {
        "k_ab.png": _grid(args.scratch, [f"k_{i:02d}.ppm" for i in range(4)],
                          [f"K={k:.2f}" for k in K_VALUES], 2, GRID_CELL, True, -22),
        "art_ab.png": _grid(args.scratch, [f"art_{i}.ppm" for i in range(4)],
                            list(ART_LABELS), 2, GRID_CELL, True, -22),
        "tint_ab.png": _grid(args.scratch, ["tint_neutral.ppm", "tint_warm.ppm"],
                             list(TINT_LABELS), 2, GRID_CELL, True, -22),
        "phase_strip.png": _grid(args.scratch, [f"phase_{i:02d}.ppm" for i in range(8)],
                                 list(STRIP_LABELS), 8, STRIP_CELL, False, -18),
    }
    for name, im in figs.items():
        im.save(os.path.join(args.out, name))
        print(f"wrote {os.path.relpath(os.path.join(args.out, name), REPO)} {im.size}")


if __name__ == "__main__":
    main()
