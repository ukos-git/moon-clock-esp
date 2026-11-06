"""Compose the bake-pipeline intermediates into one labeled strip figure.

Reads the 8 raw buffers gen_bake_intermediates.js wrote (d, lns, L, A, idx,
lutv, es, out) and lays them out left-to-right in pipeline order with a
caption line, saved to docs/book/_static/figs/bake_pipeline.png.

Run:  docs/book/.venv/bin/python docs/book/tools/gen_bake_strip.py <inddir>
"""
from __future__ import annotations

import json
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "_static", "figs", "bake_pipeline.png"))

# (key, title, note, display transform)
COLS = [
    ("d",    r"$d$",                "sub-solar cosine",        lambda v: (v + 1) / 2.0, "vmin/vmax -1..1"),
    ("lns",  r"$\mathit{lns}$",     "displaced-normal",        lambda v: (v + 1) / 2.0, "vmin/vmax -1..1"),
    ("L",    r"$L$",                "clip(lns, 0, 1)",         lambda v: v,             "0..1"),
    ("A",    r"$A$",                "albedo / 255",            lambda v: v / 255.0,     "0..1"),
    ("idx",  r"$\mathrm{idx}$",     r"$\mathrm{round}(A\,L)$", lambda v: v / 255.0,     "0..1"),
    ("lutv", r"$\mathrm{LUT}$",     "tone map",                lambda v: v / 255.0,     "0..1"),
    ("es",   r"$\mathrm{es}$",      "earthshine (×8)",         lambda v: np.clip(v * 8.0, 0, 255) / 255.0, "0..8"),
    ("out",  r"$\mathrm{out}$",     "final pixel",             lambda v: v / 255.0,     "0..1"),
]


def load(indir, key, size):
    a = np.fromfile(os.path.join(indir, key + ".raw"),
                    dtype=np.uint8 if key == "out" else np.float32)
    assert a.size == size * size, (key, a.size)
    return a.reshape(size, size)


def main(indir):
    meta = json.load(open(os.path.join(indir, "meta.json")))
    size = meta["size"]

    fig, axes = plt.subplots(1, len(COLS), figsize=(0.78 * len(COLS), 3.3))
    for ax, (key, title, note, xform, _rng) in zip(axes, COLS):
        img = xform(load(indir, key, size))
        ax.imshow(img, cmap="gray", vmin=0.0, vmax=1.0, aspect="equal")
        ax.set_title(title, fontsize=9.5)
        for s in ax.spines.values():
            s.set_visible(False)
        ax.set_xticks([])
        ax.set_yticks([])
    fig.suptitle(
        f"one frame through the bake pipeline — {meta['iso'][:16]} UTC "
        f"({meta['phase']['name']}, {100*meta['phase']['illum']:.0f}% lit)",
        fontsize=9.5, y=1.0)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    fig.savefig(OUT, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", OUT)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/bake_inter")
