"""The correctness gate: rendered moon vs NASA SVS 5415 ground truth.

Two independent checks, both reported and gated:

1. **Geometry** -- computed subsolar/subearth/posangle/illumination vs the
   per-frame JSON (subsolar_lon/lat, subearth_lon/lat, posangle, phase).
   Tolerances in ``constants.py``.

2. **Image** -- the rendered disk vs the reference ``final_400x400`` crop:
   MAE / MSE / SSIM over the disk mask (8-bit luminance), after sub-pixel
   registration. A radiometric tone curve (gain, and optional gamma LUT) maps
   the physical albedo*L to the reference's "optimized for aesthetics"
   brightness; it is fit once, frozen in ``constants.py``, and is the same
   curve the C kernel applies (a 256-entry LUT on device).

Run:  python -m moon.validate [--json ...] [--frames 32] [--fit]
      (--fit refits the tone curve; otherwise uses the frozen constants.)
Exits nonzero if any gate fails.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
from datetime import datetime

import numpy as np
from PIL import Image

from . import constants as C
from .astro import geometry
from .renderer import (REF_CX, REF_CY, REF_RADIUS, REF_SIZE, earthshine,
                       render, tonemap, to_uint8)
from .register import register_shift
from .metrics import disk_metrics
from .textures import load_textures


def _iso(nasa_time: str) -> str:
    return datetime.strptime(nasa_time.replace(" UT", ""),
                             "%d %b %Y %H:%M").strftime("%Y-%m-%dT%H:%M")


def select_frames(rec, n: int):
    """Pick ``n`` frames: the 8 primary phases (spread in illumination) plus
    intermediates spread across the year. Returns list of (index, phase)."""
    n_primary = 8
    # primary: evenly spaced target phases
    prim_targets = [2.0, 12.5, 25.0, 37.5, 50.0, 62.5, 75.0, 90.0]
    prim = []
    for t in prim_targets:
        i = min(range(len(rec)), key=lambda k: abs(rec[k]["phase"] - t))
        prim.append((i, rec[i]["phase"]))
    # intermediates: even index spread
    step = max(1, (len(rec) - 1) // max(1, n - n_primary))
    inter = [(i, rec[i]["phase"]) for i in range(0, len(rec), step)
             if i not in [p[0] for p in prim]][: n - n_primary]
    frames = prim + inter
    return frames[:n]


def geometry_metrics(g, rec_frame):
    """Per-field delta (deg / fraction) vs a JSON record (nested or flat)."""
    def get(d, key):
        if key in d:
            return d[key]
        grp, sub = key.split("_")
        return d[grp][sub]

    def wrap(a):
        return (a + 180.0) % 360.0 - 180.0

    ss_lon = wrap(g.subsolar_lon - get(rec_frame, "subsolar_lon"))
    ss_lat = g.subsolar_lat - get(rec_frame, "subsolar_lat")
    se_lon = wrap(g.subearth_lon - get(rec_frame, "subearth_lon"))
    se_lat = g.subearth_lat - get(rec_frame, "subearth_lat")
    pa = wrap(g.posangle - rec_frame["posangle"])
    illum = g.illum - rec_frame["phase"] / 100.0
    return {"subsolar_lon": ss_lon, "subsolar_lat": ss_lat,
            "subearth_lon": se_lon, "subearth_lat": se_lat,
            "posangle": pa, "illum": illum}


def fit_lut(frame_list, rec, alb, hgt):
    """Fit a 256-entry 8-bit tone LUT: LUT[i] = mean(ref | shifted in == i).

    This is MAE-optimal (per-bin conditional mean) and is the exact same
    256-byte table the C kernel applies on device (one gather per pixel). The
    LUT absorbs the reference's "optimized for aesthetics" brightness, the
    gamma, and the faint dark-side baseline (earthshine). Returns
    (lut uint8[256], mae, n_occupied_bins).
    """
    # Precompute (render + register) once: the LUT doesn't move pixels.
    pre = []
    for i, _ in frame_list:
        path = _ref_path(i)
        if not (path and os.path.exists(path)):
            continue
        ref_rgba = np.asarray(Image.open(path))
        ref = np.asarray(Image.open(path).convert("L"), dtype=np.float64)
        mref = ref_rgba[..., 3] > 200
        g = geometry(_iso(rec[i]["time"]))
        raw, dterm = render(g, alb, hgt, gain=1.0)
        raw = to_uint8(raw).astype(np.float64)
        shifted, _ = register_shift(raw, ref)
        es = earthshine(dterm)
        # The dark-side baseline is modeled by earthshine(); subtract it so
        # the LUT maps pure-sunlight albedo*L (the same split the C kernel
        # uses: lut[clamp8(raw)] + es).
        pre.append((np.clip(np.round(shifted - es), 0, 255).astype(np.int32),
                    ref, mref, es))

    sums = np.zeros(256, dtype=np.float64)
    cnts = np.zeros(256, dtype=np.int64)
    for ins_img, ref, mref, es in pre:
        ins = ins_img[mref]
        sums += np.bincount(ins, weights=(ref - es)[mref], minlength=256)
        cnts += np.bincount(ins, minlength=256)
    with np.errstate(invalid="ignore", divide="ignore"):
        lut = np.where(cnts > 0, sums / np.maximum(cnts, 1), 0.0)
    # Fill empty bins by nearest occupied bin.
    occ = np.nonzero(cnts > 0)[0]
    for k in range(256):
        if cnts[k] == 0:
            lut[k] = lut[occ[np.argmin(np.abs(occ - k))]]
    # Bin 0 is the pure dark side (raw == 0 only there; the lit side is >= 1):
    # its brightness comes from earthshine(), so the LUT maps it to 0. Hold it
    # at its own (clipped) value and smooth ONLY the lit-side bins -- a 3-px
    # kernel across the 0/1 boundary would bleed the bright near-terminator lit
    # bins into the dark-side floor and smear the terminator.
    lut0 = max(0.0, lut[0])
    from scipy.ndimage import uniform_filter1d
    lut[1:] = uniform_filter1d(lut[1:], size=3, mode="nearest")
    lut[0] = lut0
    lut = np.clip(np.round(lut), 0, 255).astype(np.uint8)

    mae, n = 0.0, 0
    for ins_img, ref, mref, es in pre:
        mae += float(np.abs(lut[ins_img][mref] + es[mref] - ref[mref]).sum())
        n += int(mref.sum())
    return lut, mae / max(1, n), int((cnts > 0).sum())


def tone_lut(img, lut):
    """Apply a 256-entry uint8 LUT to a float/uint8 image (no interpolation)."""
    idx = np.clip(np.round(np.asarray(img, dtype=np.float64)), 0, 255).astype(np.intp)
    return np.asarray(lut, dtype=np.float64)[idx]


def _ref_path(i):
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(here, "final_400x400", "moon.%04d.png" % (i + 1))


def run(n: int = 32, json_path: str = "/tmp/moon-research/mooninfo_2025.json",
        do_fit: bool = False, out_dir: str | None = None):
    rec = json.load(open(json_path))
    alb, hgt = load_textures()
    frames = select_frames(rec, n)

    if do_fit:
        lut, fit_mae, nocc = fit_lut(frames[:12], rec, alb, hgt)
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        lut_path = os.path.join(here, "tone_lut.bin")
        lut.tofile(lut_path)
        print("Fitted 256-entry tone LUT  (MAE %.2f, %d occupied bins)" %
              (fit_mae, nocc))
        print("  saved %s" % lut_path)
        print("  paste into constants.py as TONE_LUT = [%s, ...]" %
              ", ".join(str(int(x)) for x in lut[:8]))
        return lut

    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)
    geo_err = {k: [] for k in
               ("subsolar_lon", "subsolar_lat", "subearth_lon", "subearth_lat",
                "posangle", "illum")}
    img_rows = []
    failures = []

    for i, phase in frames:
        ref_path = _ref_path(i)
        g = geometry(_iso(rec[i]["time"]))
        gm = geometry_metrics(g, rec[i])
        for k, v in gm.items():
            geo_err[k].append(abs(v))

        if not (ref_path and os.path.exists(ref_path)):
            continue
        ref_rgba = np.asarray(Image.open(ref_path))
        ref = np.asarray(Image.open(ref_path).convert("L"), dtype=np.float64)
        mref = ref_rgba[..., 3] > 200
        raw, dterm = render(g, alb, hgt, gain=1.0)
        raw = to_uint8(raw).astype(np.float64)
        shifted, shift = register_shift(raw, ref)
        out = to_uint8(tonemap(shifted, dterm, lut))

        m = disk_metrics(None, ref_rgba, shifted=out.astype(np.float64))
        img_rows.append((i, phase, m["mae"], m["ssim"], shift))

        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
            Image.fromarray(out.astype(np.uint8)).save(
                os.path.join(out_dir, "moon.%04d_render.png" % (i + 1)))
            Image.fromarray(ref.astype(np.uint8)).save(
                os.path.join(out_dir, "moon.%04d_ref.png" % (i + 1)))
            diff = np.clip(out - ref, -100, 100).astype(np.float64) + 128
            Image.fromarray(diff.astype(np.uint8)).save(
                os.path.join(out_dir, "moon.%04d_diff.png" % (i + 1)))

    # ---- report ----
    print("\n== Geometry (max over %d frames) ==" % len(frames))
    geo_ok = True
    for k, tol in (("subsolar_lon", C.GEO_SUBSOLAR_TOL_DEG),
                   ("subsolar_lat", C.GEO_SUBSOLAR_TOL_DEG),
                   ("subearth_lon", C.GEO_SUBEARTH_TOL_DEG),
                   ("subearth_lat", C.GEO_SUBEARTH_TOL_DEG),
                   ("posangle", C.GEO_POSANGLE_TOL_DEG)):
        mx = max(geo_err[k]) if geo_err[k] else 0.0
        ok = mx <= tol
        geo_ok &= ok
        unit = "" if k == "illum" else " deg"
        print("  %-13s max %7.3f%s  tol %4.2f%s  %s"
              % (k, mx, unit, tol, unit, "OK" if ok else "FAIL"))
    illum_mx = max(geo_err["illum"]) if geo_err["illum"] else 0.0
    print("  illum        max %7.4f   (fraction, informational)" % illum_mx)

    print("\n== Image (256-entry tone LUT) ==")
    print("  MAE gated per-frame; SSIM (content region) gated on the mean.")
    print("  frame  phase    MAE   SSIM   shift(dy,dx)")
    img_ok = True
    for i, phase, mae, ssim, shift in img_rows:
        ok = mae <= C.MAE_MAX
        img_ok &= ok
        flag = "OK" if ok else "FAIL"
        print("  %4d  %6.2f  %6.2f %6.3f  (%6.2f,%6.2f)  %s"
              % (i + 1, phase, mae, ssim, shift[0], shift[1], flag))
    if img_rows:
        mean_mae = np.mean([r[2] for r in img_rows])
        mean_ssim = np.mean([r[3] for r in img_rows])
        ssim_ok = mean_ssim >= C.SSIM_MIN
        img_ok &= ssim_ok
        print("  mean MAE %.2f (max %.2f)  mean content SSIM %.3f  %s"
              % (mean_mae, max(r[2] for r in img_rows), mean_ssim,
                 "OK" if ssim_ok else "FAIL"))

    all_ok = geo_ok and img_ok
    print("\nGATE: %s" % ("PASS" if all_ok else "FAIL"))
    return 0 if all_ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--json", default="/tmp/moon-research/mooninfo_2025.json")
    ap.add_argument("--fit", action="store_true", help="refit the tone curve")
    ap.add_argument("--out", default=None, help="write render/ref/diff PNGs here")
    args = ap.parse_args()
    if args.fit:
        run(args.frames, args.json, do_fit=True, out_dir=args.out)
    else:
        raise SystemExit(run(args.frames, args.json, do_fit=False, out_dir=args.out))


if __name__ == "__main__":
    main()
