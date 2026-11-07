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

    python3 docs/tools/fit_device.py                      # S3 hero -> fit.json
    python3 docs/tools/fit_device.py P4.waveshare-p4-hero.webp -o fit_p4.json
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent


# Kasa circle fit, two stages (drop radius outliers after the first pass).
def kasa(pts):
    x, y = pts[:, 0], pts[:, 1]
    M = np.column_stack([x, y, np.ones_like(x)])
    b = x * x + y * y
    sol, *_ = np.linalg.lstsq(M, b, rcond=None)
    cx, cy = sol[0] / 2, sol[1] / 2
    R = np.sqrt(sol[2] + cx * cx + cy * cy)
    return cx, cy, R


def fit(hero, bright=False):
    """Fit the glass disc in a top-down Waveshare product photo.

    The boundary is the edge of the display disc: for a DARK display (the
    S3 AMOLED hero, screen off) the disc is the dark region; for a BRIGHT
    display (the P4 4C photo, a colorful wallpaper) the disc is the bright
    region and the black bezel ring is excluded. Either way the Kasa fit
    converges on the one sharp circle (the glass / content edge).
    """
    img = Image.open(hero).convert("RGB")
    a = np.asarray(img, dtype=np.float32)
    h, w, _ = a.shape
    lum = a @ np.array([0.299, 0.587, 0.114])
    disc = lum > 128 if bright else lum < 128

    pad = np.pad(disc, 1, mode="constant", constant_values=False)
    edge = (pad[1:-1, 1:-1] & ~pad[:-2, 1:-1]) | (pad[1:-1, 1:-1] & ~pad[2:, 1:-1]) \
         | (pad[1:-1, 1:-1] & ~pad[1:-1, :-2]) | (pad[1:-1, 1:-1] & ~pad[1:-1, 2:])
    ys, xs = np.nonzero(edge)
    pts = np.stack([xs, ys], axis=1).astype(np.float64)

    cx, cy, r_content = kasa(pts)
    r = np.hypot(pts[:, 0] - cx, pts[:, 1] - cy)
    keep = np.abs(r - np.median(r)) < 0.08 * np.median(r)
    cx, cy, r_content = kasa(pts[keep])
    resid = np.hypot(pts[keep, 0] - cx, pts[keep, 1] - cy) - r_content
    print(
        f"content fit: cx={cx:.2f} cy={cy:.2f} R={r_content:.2f} "
        f"resid p50={np.abs(np.median(resid)):.2f} p99={np.percentile(np.abs(resid), 99):.2f} "
        f"(n={len(pts[keep])})"
    )
    return {
        "cx": cx, "cy": cy,
        "R_content": r_content,
        "img_w": w, "img_h": h,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("hero", nargs="?", default="waveshare-hero.webp",
                    help="the top-down product photo (default: the S3 hero)")
    ap.add_argument("-o", default="fit.json", help="output fit JSON (default: fit.json)")
    ap.add_argument("--bright", action="store_true",
                    help="the display in the photo is bright (colorful); "
                         "fit the bright disc, not the dark bezel")
    # active window / outer, from the dimension drawing (mm, radius). The
    # bezel is ~2.6 mm each side on the 1.75; the 4C's is thicker, so the
    # RIM ratio is a fit-time input (default the 1.75 ratio).
    ap.add_argument("--content-mm", type=float, default=43.76 / 2)
    ap.add_argument("--rim-mm", type=float, default=48.96 / 2)
    args = ap.parse_args()

    f = fit(HERE / args.hero, bright=args.bright)
    f["R_rim_outer"] = f["R_content"] * args.rim_mm / args.content_mm
    f["R_content_mm"] = args.content_mm
    f["R_rim_mm"] = args.rim_mm
    print(f"R_rim_outer = {f['R_rim_outer']:.2f} "
          f"(ratio {args.rim_mm / args.content_mm:.4f})")

    out = HERE / args.o
    json.dump(f, out.open("w"), indent=1)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
