#!/usr/bin/env python3
"""Regenerate the docs' NASA-vs-C side-by-side figures (docs/img/nasa/).

The doc's "how the moon looks vs NASA" figures are ONE row of three equal
400x400 panels per figure:
  [NASA SVS 5415]  [C runtime, 5-bit RGB565]  [residual |C - NASA| x 8]
The diff uses the SAME sub-pixel registration (phase cross-correlation)
the gate uses for its MAE, so the third panel shows the measured residual,
not a raw-pixel one.

Run it with the host venv (numpy/PIL + the moon package) AFTER a fresh
gate run, whose 32-frame output is still on disk:

    host_sim/.venv/bin/python host_sim/check_gate.py \
        --sim host_sim/build/moon_sim --out /tmp/moon-sim-gate
    host_sim/.venv/bin/python docs/nasa_figs.py

Inputs (all 400x400 grayscale):
  /tmp/moon-sim-gate/sim.000K.png        C runtime frame, gate-set position K
  assets/images/moon/final_400x400/moon.NNNN.png   NASA SVS 5415 reference
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "assets", "images", "moon"))

GATE = "/tmp/moon-sim-gate"
REF = os.path.join(ROOT, "assets", "images", "moon", "final_400x400")
OUT = os.path.join(ROOT, "docs", "img", "nasa")

from moon.register import register_shift  # noqa: E402

# (gate-set position k, NASA series frame number, phase, out label)
# The gate selects the same 32 frames from the fetched mooninfo_2025.json
# (assets/images/moon/mooninfo_2025.json — get_assets.py --refs; legacy
# checkouts keep theirs in /tmp/moon-research) via
# moon.validate.select_frames; the positions/numbers below are from the
# 2026-11-01 run that produced the committed figures.
PICKS = [
    (2, 1993, "25.0", "p025"),
    (4, 2259, "50.0", "p050"),
    (6, 905, "75.0", "p075"),
    (7, 1817, "90.0", "p090"),
]


def load_gray(path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("L"), dtype=np.float64)


def main():
    os.makedirs(OUT, exist_ok=True)  # media is build output — a clean tree has no dir
    for k, nasaid, phase, label in PICKS:
        sim = load_gray(f"{GATE}/sim.{k:04d}.png")
        ref = load_gray(f"{REF}/moon.{nasaid:04d}.png")
        sim_reg, _shift = register_shift(sim, ref)
        diff = np.clip(np.abs(sim_reg - ref) * 8.0, 0, 255).astype(np.uint8)
        sim_u8 = np.clip(sim, 0, 255).astype(np.uint8)
        ref_u8 = np.clip(ref, 0, 255).astype(np.uint8)

        w, h = sim.shape[1], sim.shape[0]
        gap, margin, cap = 16, 16, 34
        cw = 3 * w + 2 * gap + 2 * margin
        chh = h + cap + 2 * margin
        canvas = Image.new("L", (cw, chh), 0)
        x = margin
        for u8 in (ref_u8, sim_u8, diff):
            canvas.paste(Image.fromarray(u8), (x, margin))
            x += w + gap
        dr = ImageDraw.Draw(canvas)
        dr.text((margin, margin + h + 8),
                f"NASA SVS 5415 (frame {nasaid}, phase {phase})", fill=210)
        dr.text((margin + w + gap, margin + h + 8),
                "C runtime, 5-bit RGB565", fill=210)
        dr.text((margin + 2 * w + 2 * gap, margin + h + 8),
                "residual x 8 (registered, gate MAE<=12)", fill=210)
        out = f"{OUT}/nasa_vs_c_{label}.png"
        canvas.save(out)
        print(f"wrote {out} ({cw}x{chh})")


if __name__ == "__main__":
    main()
