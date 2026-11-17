#!/usr/bin/env python
"""Retune the terminator band width + weight style against the 32-frame
NASA gate set. Scores candidate (band, weight) configs with the same
metrics the gate uses (per-frame MAE <= 12, mean SSIM >= 0.89) and picks
the one that minimizes MAE. The winning config is then ported to
constants.py + moon_bake.c."""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from moon import assets as A
from moon import constants as C
from moon.astro import geometry
from moon.metrics import disk_metrics
from moon.register import register_shift
from moon.validate import _iso, select_frames
from moon.sphere import equirect_bilinear, disk_grid
from moon.assets import relief_lns

REF_DIR = os.path.join(HERE, "final_400x400")
JSON = "/tmp/moon-research/mooninfo_2025.json"


def q565(u8):
    q = (u8.astype(np.uint32) * 31 + 127) // 255
    return ((q * 255 + 15) // 31).astype(np.uint8)


def relief_unclipped(r_body, g, dem, mps):
    """Unclipped displaced-normal Lambert (d - G.S)/sqrt(1+|G|^2). The
    clipped relief_lns floors this at 0; the unclipped value goes negative
    on slopes facing away from the sun, which is what makes a mountain
    shadow go fully dark at the terminator."""
    W, H = dem.shape[1], dem.shape[0]
    rx, ry, rz = r_body[..., 0], r_body[..., 1], r_body[..., 2]
    theta = np.degrees(np.arctan2(ry, rx)) % 360
    colf = ((theta + C.TEXTURE_LON_OFFSET_DEG) % 360) / 360 * W
    rowf = (90 - np.degrees(np.arcsin(np.clip(rz, -1, 1)))) / 180 * H
    c = np.clip(np.floor(colf + 0.5).astype(int), 0, W - 1)
    r = np.clip(np.floor(rowf + 0.5).astype(int), 0, H - 1)
    cp, cm = (c + 1) % W, (c - 1) % W
    rn, rs = np.clip(r - 1, 0, H - 1), np.clip(r + 1, 0, H - 1)
    # glon = EAST-WEST (longitude) diff, glat = NORTH-SOUTH (latitude) diff.
    glon = (dem[r, cp].astype(float) - dem[r, cm].astype(float)) * mps \
        / (2 * C.MOON_RADIUS_M * np.maximum(np.sqrt(rx * rx + ry * ry), 0.05)
           * (2 * np.pi / W))
    glat = (dem[rn, c].astype(float) - dem[rs, c].astype(float)) * mps \
        / (2 * C.MOON_RADIUS_M * (np.pi / H))
    S = g.S_body
    rho = np.maximum(np.sqrt(rx * rx + ry * ry), 1e-4)
    inv = 1 / rho
    gs = glon * ((-ry * S[0] + rx * S[1]) * inv) \
        + glat * ((-rz * rx * S[0] - rz * ry * S[1] + rho * S[2]) * inv)
    d = r_body @ S
    return (d - gs) / np.sqrt(1 + glon * glon + glat * glat)


def render(g, alb, dem, mps, band, wstyle, size=400, radius=200.0):
    """raw with a parametric terminator model. wstyle:
       'linear' : L = d + (lns-d)*wgt, wgt=(band-|d|)/band  (current)
       'hard'   : L = clip(lns_raw) where lns_raw = (d-gs)/sqrt(1+|G|^2)
                  is the UNCLIPPED displaced-normal Lambert; used wherever
                  |d|<band, else d.  (relief makes the lit/dark call)
       'full'   : L = clip(lns_raw) over the whole disk (band=inf)."""
    u, v, w, mask = disk_grid(size, radius, 200.0, 200.0)
    r_body = (u[..., None] * g.right_body + v[..., None] * g.up_body
              + w[..., None] * g.E_body)
    lon_body = np.degrees(np.arctan2(r_body[..., 1], r_body[..., 0])) % 360
    lat_body = np.degrees(np.arcsin(np.clip(r_body[..., 2], -1, 1)))
    A = equirect_bilinear(alb.astype(np.float64),
                          (lon_body + C.TEXTURE_LON_OFFSET_DEG) % 360, lat_body)
    d = r_body @ g.S_body
    ad = np.abs(d)
    if wstyle in ("hard", "full"):
        # Displaced normal makes the lit/dark call (no blend): inside the
        # band L = unclipped relief Lambert, outside L = base Lambert.
        lns_raw = relief_unclipped(r_body, g, dem, mps)
        L = np.where(ad < band, lns_raw, d)
    else:
        # 'linear': current model (clipped relief dot, linear weight).
        # 'soft'  : UNCLIPPED displaced-normal Lambert with the same linear
        #          weight -- continuous at the band edge (weight 0 there),
        #          and slopes facing away from the sun go fully dark, which
        #          is what produces the sharp jagged shadow the NASA frames
        #          show at the terminator.
        rel = relief_unclipped(r_body, g, dem, mps) if wstyle == "soft" \
            else relief_lns(r_body, g, dem, mps)
        wgt = np.clip((band - ad) / band, 0, 1)
        L = np.where(ad > band, d, d + (rel - d) * wgt)
    L = np.clip(L, 0, None)
    raw = A * L
    out = np.zeros((size, size)); out[mask] = raw[mask]
    d_full = np.zeros((size, size)); d_full[mask] = d[mask]
    return out, d_full


def score(band, wstyle, alb, dem, mps, rec, frames, lut):
    maes, ssims = [], []
    worst = 0.0
    for i, _ph in frames:
        p = os.path.join(REF_DIR, "moon.%04d.png" % (i + 1))
        ref = np.asarray(Image.open(p).convert("L"), dtype=np.float64)
        ref_rgba = np.asarray(Image.open(p))
        g = geometry(_iso(rec[i]["time"]))
        raw, d = render(g, alb, dem, mps, band, wstyle)
        img8 = q565(A.to_uint8(A.tonemap(raw, d, lut)))
        shifted, _ = register_shift(img8.astype(np.float64), ref)
        m = disk_metrics(None, ref_rgba, shifted=shifted)
        maes.append(m["mae"]); ssims.append(m["ssim"])
        worst = max(worst, m["mae"])
    return float(np.mean(maes)), float(np.mean(ssims)), worst


def main() -> int:
    rec = json_load = __import__("json").load(open(JSON))
    frames = select_frames(rec, 32)
    alb = A.bake_albedo(A.load_albedo())
    dem, lo, hi = A.bake_dem(A.load_height())
    mps = (hi - lo) / A.DEM_STEPS
    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)

    configs = [
        (0.02, "linear"),   # current model (clipped, narrow band)
        (0.10, "linear"),
        (0.05, "soft"),     # unclipped, continuous blend
        (0.10, "soft"),
        (0.20, "soft"),
        (0.35, "soft"),
        (0.10, "hard"),     # unclipped, displaced-normal decision
        (0.35, "hard"),
        (9.0, "hard"),      # full-disk displaced normal
    ]
    print("  band    style   meanMAE  meanSSIM  worstMAE  (gate <=12 / >=0.89)")
    best = None
    for band, style in configs:
        mae, ssim, worst = score(band, style, alb, dem, mps, rec, frames, lut)
        ok = (worst <= C.MAE_MAX) and (ssim >= C.SSIM_MIN)
        tag = "OK " if ok else "fail"
        print(f"  {band:<6} {style:<7} {mae:8.2f} {ssim:9.3f} {worst:9.2f}  {tag}")
        if ok and (best is None or mae < best[0]):
            best = (mae, band, style)
    if best:
        print("\nbest passing config: band=%.2f style=%s (mean MAE %.2f)"
              % (best[1], best[2], best[0]))
    else:
        print("\nno config passed both gates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
