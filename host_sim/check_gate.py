"""C-simulator gate (Phase 3, step 3 of the approved plan):
"host_sim PNGs vs final_400x400 with the same metrics, within ~2 MAE of the
Python full model."

This runs two diffs over the 32-frame set, both at 5-bit panel fidelity
(the device shows a 466x466 RGB565 panel; gray -> 5-bit is exact 1:1):

  1. C  vs NASA  -- the runtime path (eraEpv00+eraMoon98 ephemeris,
     fixed-point rotation, gray->RGB565) against the NASA SVS 5415 frames.
     Gate: per-frame MAE <= 12 (same metric as the prototype gate).
     Mean content SSIM is reported for context.

  2. C  vs Python model -- porting fidelity. The Python prototype (float
     astropy ephemeris, the same banded relief-DEM lighting as the C core,
     5-bit quantized) is the validated reference; the C core must reproduce
     it within ~2 MAE per frame. Both are pixel-aligned (identical disk
     geometry, no registration), so this is a direct pixel diff of the two
     panels' renders. Gate: per-frame MAE <= 2.

If the C port is faithful (check 2), check 1 inherits the prototype's
NASA agreement -- the residual between the two is exactly the porting
error. Running both keeps the fidelity claim explicit instead of implicit.

Run:  python check_gate.py [--sim build/moon_sim] [--out /tmp/moon-sim-gate]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MOON_PKG = os.path.normpath(
    os.path.join(HERE, "..", "assets", "images", "moon"))
sys.path.insert(0, MOON_PKG)

from moon import assets as A                 # noqa: E402
from moon import constants as C              # noqa: E402
from moon.astro import geometry              # noqa: E402
from moon.metrics import to_gray             # noqa: E402
from moon.metrics import disk_metrics        # noqa: E402
from moon.register import register_shift     # noqa: E402
from moon.validate import _iso, select_frames  # noqa: E402

REF_DIR = os.path.join(MOON_PKG, "final_400x400")

# Porting-fidelity gate (plan: "within ~2 MAE of the Python full model").
MAE_C_VS_PY = 2.0


def q565(u8: np.ndarray) -> np.ndarray:
    """Exact 1:1 gray -> 5-bit -> gray (the panel's quantization)."""
    q = (u8.astype(np.uint32) * 31 + 127) // 255
    return ((q * 255 + 15) // 31).astype(np.uint8)


def load_gray(path: str) -> np.ndarray:
    im = Image.open(path).convert("L")
    return np.asarray(im, dtype=np.float64)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default=os.path.join(HERE, "build", "moon_sim"))
    ap.add_argument("--out", default="/tmp/moon-sim-gate")
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--json", default="/tmp/moon-research/mooninfo_2025.json")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    rec = json.load(open(args.json))
    frames = select_frames(rec, args.frames)
    alb = A.bake_albedo(A.load_albedo())
    dem, dem_lo, dem_hi = A.bake_dem(A.load_height())
    mps = (dem_hi - dem_lo) / A.DEM_STEPS
    lut = np.asarray(C.TONE_LUT, dtype=np.uint8)

    # --- C output: run the simulator over the frame list -------------------
    list_path = os.path.join(args.out, "frames.txt")
    with open(list_path, "w") as f:
        for i, _ph in frames:
            f.write(_iso(rec[i]["time"]) + "\n")
    r = subprocess.run([args.sim, "--list", list_path, "--size", "400",
                        "--out", args.out], capture_output=True, text=True)
    if r.returncode != 0:
        print("simulator failed:\n" + r.stdout + r.stderr)
        return 2

    # --- Python model: 5-bit render_from_assets per frame ------------------
    py_dir = os.path.join(args.out, "py")
    os.makedirs(py_dir, exist_ok=True)
    for k, (i, _ph) in enumerate(frames):
        p = A._ref_path(i, MOON_PKG)
        if not os.path.exists(p):
            continue
        g = geometry(_iso(rec[i]["time"]))
        raw, d = A.render_from_assets(g, alb, dem, mps)
        out = q565(A.to_uint8(A.tonemap(raw, d, lut)))
        Image.fromarray(out, mode="L").save(os.path.join(py_dir, "py.%04d.png" % k))

    # --- diff both ways ----------------------------------------------------
    rows = []
    for k, (i, phase) in enumerate(frames):
        sp = os.path.join(args.out, "sim.%04d.png" % k)
        pp = os.path.join(py_dir, "py.%04d.png" % k)
        rp = os.path.join(REF_DIR, "moon.%04d.png" % (i + 1))
        if not (os.path.exists(sp) and os.path.exists(pp) and os.path.exists(rp)):
            print("  missing %s / %s / %s" % (sp, pp, rp))
            continue
        sim = load_gray(sp)
        py = load_gray(pp)
        ref_rgba = np.asarray(Image.open(rp))
        ref = np.asarray(Image.open(rp).convert("L"), dtype=np.float64)

        # 1. C vs NASA (registered, disk metrics -- the prototype's metric).
        shifted, _sh = register_shift(sim, ref)
        m = disk_metrics(None, ref_rgba, shifted=shifted)
        # 2. C vs Python model (direct pixel diff, aligned).
        c_vs_py = float(np.mean(np.abs(sim - py)))
        rows.append((i + 1, phase, m["mae"], m["ssim"], c_vs_py))

    print("\n== C-simulator gate (32-frame set) ==")
    print("  frame  phase    C-NASA   SSIM   C-Python")
    for i, phase, mae, ssim, cp in rows:
        flags = (" *" if mae > C.MAE_MAX else "") + (" ~" if cp > MAE_C_VS_PY else "")
        print("  %4d  %6.2f  %6.2f %6.3f   %5.2f%s" % (i, phase, mae, ssim, cp, flags))

    mae_nasa = np.mean([r_[2] for r_ in rows])
    ssim_nasa = np.mean([r_[3] for r_ in rows])
    mae_py = np.mean([r_[4] for r_ in rows])
    n_nasa = sum(1 for r_ in rows if r_[2] > C.MAE_MAX)
    n_py = sum(1 for r_ in rows if r_[4] > MAE_C_VS_PY)
    print("  C vs NASA : mean MAE %.2f (max %.2f, %d frames > %.0f)  "
          "mean content SSIM %.3f"
          % (mae_nasa, max(r_[2] for r_ in rows), n_nasa, C.MAE_MAX, ssim_nasa))
    print("  C vs Python: mean MAE %.2f (max %.2f, %d frames > %.0f)"
          % (mae_py, max(r_[4] for r_ in rows), n_py, MAE_C_VS_PY))

    ok = (n_nasa == 0) and (n_py == 0)
    print("  gate: C-NASA per-frame MAE <= %.0f  AND  C-Python per-frame MAE <= %.0f"
          % (C.MAE_MAX, MAE_C_VS_PY))
    print("  ->  %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
