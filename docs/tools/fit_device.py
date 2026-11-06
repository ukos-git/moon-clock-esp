#!/usr/bin/env python3
"""Fit the device circles in the Waveshare hero photo.

Input:  waveshare-hero.webp (vendored next to this script)
Output: fit.json next to this script, with the glass geometry the
        compositing scripts read:
          cx, cy        centre of the glass disc (photo px)
          R_content     the inner bezel boundary — the one sharp circle
                        in the photo (the lit-content edge the Kasa fit
                        converges on; the rim's outer edge is a soft
                        shadow gradient)
          R_rim_outer   the rim outer edge, derived from R_content with
                        the 48.96/43.76 mm ratio of the official
                        dimension drawing (2.6 mm bezel each side)
          R_content_mm, R_rim_mm, img_w, img_h  provenance

Run (from the repo root) only when the photo is swapped:

    python3 docs/tools/fit_device.py
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
HERO = HERE / "waveshare-hero.webp"
IMG = Image.open(HERO).convert("RGB")
A = np.asarray(IMG, dtype=np.float32)
H, W, _ = A.shape

# Dark mask (bezel ring + any dark content). The soft photo shadow is
# light (>~200) so it is excluded.
lum = A @ np.array([0.299, 0.587, 0.114])
dark = lum < 128

# Boundary: dark pixels with at least one 4-neighbor that is not dark.
pad = np.pad(dark, 1, mode="constant", constant_values=False)
n_dark = (pad[1:-1, 1:-1] & ~pad[:-2, 1:-1]) | (pad[1:-1, 1:-1] & ~pad[2:, 1:-1]) \
       | (pad[1:-1, 1:-1] & ~pad[1:-1, :-2]) | (pad[1:-1, 1:-1] & ~pad[1:-1, 2:])
ys, xs = np.nonzero(n_dark)
pts = np.stack([xs, ys], axis=1).astype(np.float64)


# Kasa circle fit, two stages (drop radius outliers after the first pass).
def kasa(pts):
    x, y = pts[:, 0], pts[:, 1]
    M = np.column_stack([x, y, np.ones_like(x)])
    b = x * x + y * y
    sol, *_ = np.linalg.lstsq(M, b, rcond=None)
    cx, cy = sol[0] / 2, sol[1] / 2
    R = np.sqrt(sol[2] + cx * cx + cy * cy)
    return cx, cy, R


cx, cy, R_content = kasa(pts)
r = np.hypot(pts[:, 0] - cx, pts[:, 1] - cy)
keep = np.abs(r - np.median(r)) < 0.08 * np.median(r)
cx, cy, R_content = kasa(pts[keep])
resid = np.hypot(pts[keep, 0] - cx, pts[keep, 1] - cy) - R_content
print(
    f"content fit: cx={cx:.2f} cy={cy:.2f} R={R_content:.2f} "
    f"resid p50={np.abs(np.median(resid)):.2f} p99={np.percentile(np.abs(resid), 99):.2f} "
    f"(n={len(pts[keep])})"
)

# active window / outer, from the dimension drawing (mm, radius).
CONTENT_MM, RIM_MM = 43.76 / 2, 48.96 / 2
R_rim = R_content * RIM_MM / CONTENT_MM
print(f"R_rim_outer = {R_rim:.2f} (ratio {RIM_MM / CONTENT_MM:.4f})")

json.dump(
    {
        "cx": cx,
        "cy": cy,
        "R_content": R_content,
        "R_rim_outer": R_rim,
        "R_content_mm": CONTENT_MM,
        "R_rim_mm": RIM_MM,
        "img_w": W,
        "img_h": H,
    },
    (HERE / "fit.json").open("w"),
    indent=1,
)
print(f"wrote {HERE / 'fit.json'}")
