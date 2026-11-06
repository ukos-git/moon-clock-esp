"""Generate the data figures for docs/science/lighting.rst (the lighting model).

Two figures — the pixel data no native diagram can express — saved to
docs/_static/figs/:

  earthshine.png        the fitted earthshine cubic (the device constants)
  tone_lut.png          the real 256-entry tone LUT (assets/images/moon/tone_lut.bin)

The three explanatory sketches (ortho_view, terminator_ridge,
displaced_normal) are hand-authored SVG sources under docs/_graphs/
(lighting_*.svg) — a text source in the repo, rendered as native browser
vector, no generation step.

Run:  docs/.venv/bin/python docs/tools/gen_lighting_figs.py
(needs matplotlib; docs/.venv carries the site's pinned requirements)
"""
from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.abspath(os.path.join(HERE, "..", "_static", "figs"))

# --- house style -----------------------------------------------------------
INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
AMBER = "#b07d10"   # lit / the light's role
BLUE = "#2b5fb0"    # camera / geometry
RED = "#a63d2a"     # the flat model's error

plt.rcParams.update({
    "font.size": 9.5,
    "font.family": "DejaVu Sans",
    "text.color": INK,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.linewidth": 0.8,
    "lines.linewidth": 1.8,
    "figure.dpi": 150,
    "savefig.dpi": 150,
})


def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", path)


# --- 1. earthshine ----------------------------------------------------------
def fig_earthshine():
    fig, ax = plt.subplots(figsize=(5.6, 3.3))
    A, B, C = 30.455, -11.733, -2.615   # moon_consts.h
    x = np.linspace(0, 1, 200)
    y = A * x + B * x * x + C * x * x * x
    ax.fill_between(x, y, color=AMBER, alpha=0.18)
    ax.plot(x, y, color=AMBER, lw=2.2)
    ax.plot([1], [A + B + C], "o", ms=5, color=AMBER)
    ax.annotate(f"peak ≈ {A + B + C:.0f} grey levels\n(at x = 1, the dark-side centre)",
                xy=(1.0, A + B + C), xytext=(0.52, 22.5),
                fontsize=9, color=INK,
                arrowprops=dict(arrowstyle="->", color=MUTED, lw=0.9))
    ax.text(0.01, 26.0, r"$\mathrm{earthshine}(d) = 30.455\,x - 11.733\,x^2 - 2.615\,x^3$,   $x = \max(0,-d)$",
            fontsize=9.5, ha="left")
    ax.set_xlim(0, 1.02)
    ax.set_ylim(0, 28)
    ax.set_xlabel("x = max(0, −d):  0 at the terminator  →  1 at the dark-side centre", fontsize=8.8)
    ax.set_ylabel("brightness, 8-bit (0–255)", fontsize=8.8)
    ax.grid(axis="y", color=HAIR, lw=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_title("the validated dark-side gradient (dark_zero = 0)", fontsize=9.5)
    fig.tight_layout()
    save(fig, "earthshine.png")


# --- 2. the tone LUT --------------------------------------------------------
def fig_tonlut():
    lut = np.fromfile(os.path.join(REPO, "assets", "images", "moon", "tone_lut.bin"), dtype=np.uint8)
    fig, ax = plt.subplots(figsize=(5.6, 3.3))
    idx = np.arange(len(lut))
    ax.plot(idx, lut, color=INK, lw=1.8)
    ax.plot([0], [lut[0]], "o", ms=5, color=BLUE)
    ax.annotate("bin 0 = 0:\nno sunlight → the LUT adds nothing\n(only earthshine remains)",
                xy=(0, 0), xytext=(22, 60), fontsize=8.8, color=BLUE,
                arrowprops=dict(arrowstyle="->", color=BLUE, lw=0.9))
    # highlight rolloff: find the last index where the curve is strictly rising
    rise = np.where(np.diff(lut) > 0)[0]
    k = int(rise[-1]) if len(rise) else 220
    ax.plot([k, 255], [lut[k], lut[-1]], color=RED, lw=2.4)
    ax.annotate("tail is not monotonic past ~220:\nthe brightest floors/mare compress\nand can dip rather than pin at 255",
                xy=(k, lut[k]), xytext=(128, 178), fontsize=8.8, color=RED,
                arrowprops=dict(arrowstyle="->", color=RED, lw=0.9))
    ax.set_xlim(0, 255)
    ax.set_ylim(0, 255)
    ax.set_xlabel("raw index = round(albedo × L)   (0–255)", fontsize=8.8)
    ax.set_ylabel("display brightness, 8-bit", fontsize=8.8)
    ax.grid(color=HAIR, lw=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_title("the fitted 256-entry tone LUT (tone_lut.bin, as shipped)", fontsize=9.5)
    fig.tight_layout()
    save(fig, "tone_lut.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    fig_earthshine()
    fig_tonlut()
