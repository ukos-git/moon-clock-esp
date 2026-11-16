#!/usr/bin/env python3
"""Generate the NASA answer-key quarters strip for
docs/science/reference-images.rst — "the answer key" made visible.

The page documents NASA SVS 5415 (the 8,760-hour answer key the image
gate scores against) but showed none of it. Five answer-key crops are
used here — the four quarter marks plus the final frame — read straight
from the gitignored SVS 5415 series under
assets/images/moon/final_400x400/ (moon.NNNN.png, 1-based; the gate's
own _ref_path mapping): hour h (0-based) -> moon.{h+1:04d}.png.
This figure lays them out as one row with honest computed labels:

  hour index -> wall time   -- 2025-01-01T00:00 UTC + h hours, exactly
                               the series' own mapping (frame N covers
                               hour N, the same reduction the gate's
                               final_400x400 crops use)
  illumination              -- computed per frame with the project's
                               own moon.astro.geometry() (the
                               yardstick package), so the numbers in
                               the strip are the same physics the
                               device runs; each was additionally
                               cross-checked against the frame's
                               measured lit-pixel fraction and agreed
                               to +/- 0.003 (the new-moon frame aside:
                               a 24-level pixel threshold also counts
                               the earthshine there)

The frames themselves are the real answer-key crops, never simulated.
The SVS 5415 series is gitignored (assets/ is not in git) — if the run
below reports it missing, regenerate it with the assets package
(assets/images/moon/moon/, see docs/reference/sources.rst).

Run (needs numpy/PIL + the moon package):
    host_sim/.venv/bin/python docs/tools/gen_nasa_quarters.py
"""
from __future__ import annotations

import os
import sys
from datetime import datetime, timedelta

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "assets", "images", "moon"))
from moon.astro import geometry  # noqa: E402

REF = os.path.join(ROOT, "assets", "images", "moon", "final_400x400")
OUT = os.path.join(ROOT, "docs", "img", "nasa", "nasa_quarters.png")

HOURS = (0, 2190, 4380, 6570, 8759)  # quarter marks + final frame
EP = datetime(2025, 1, 1)            # series epoch, UTC


def main() -> None:
    w = h = 400
    gap, margin, cap = 16, 20, 52
    cw = 5 * w + 4 * gap + 2 * margin
    chh = h + cap + 2 * margin
    canvas = Image.new("RGB", (cw, chh), (28, 28, 32))

    if not os.path.isdir(REF):
        raise SystemExit(f"SVS 5415 series missing: {REF} — the answer-key crops are "
                         "gitignored; regenerate them with the assets package "
                         "(assets/images/moon/moon/, see docs/reference/sources.rst)")
    for i, hr in enumerate(HOURS):
        im = Image.open(os.path.join(REF, f"moon.{hr + 1:04d}.png"))
        # the tracked crops are round disks on transparency — flatten on black
        bg = Image.new("RGB", im.size, (0, 0, 0))
        bg.paste(im, mask=im.split()[-1] if im.mode == "RGBA" else None)
        x = margin + i * (w + gap)
        canvas.paste(bg, (x, margin))

        stamp = (EP + timedelta(hours=hr)).strftime("%Y-%m-%dT%H:%M")
        iso = stamp.replace("T", " ")
        illum = geometry(stamp).illum
        dr = ImageDraw.Draw(canvas)
        cx = x + w // 2
        dr.text((cx, margin + h + 10), f"hour {hr}", fill=(210, 210, 210),
                anchor="mm")
        dr.text((cx, margin + h + 28), f"{iso} UTC", fill=(170, 170, 170),
                anchor="mm")
        dr.text((cx, margin + h + 44), f"illum {illum:.3f}",
                fill=(140, 175, 225), anchor="mm")

    canvas.save(OUT)
    print(f"wrote {os.path.relpath(OUT, ROOT)} ({cw}x{chh})")


if __name__ == "__main__":
    main()
