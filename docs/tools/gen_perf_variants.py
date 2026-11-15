#!/usr/bin/env python3
"""Generate the per-board-variant performance comparison figure for
docs/verification/performance.rst.

Three comparisons, one per panel, all at each board's NATIVE bake size:

(a) throughput — microseconds per baked pixel. The host C row is read
    from the committed perf_data.json (median of 5, same epoch as every
    other figure); the device rows are the accepted captures quoted on
    the page (S3 2026-11-08 ~1.3 s @400 px, P4 M3/M4 0.96 s @650 px,
    knob 2026-11-14 reveal-bench 991 ms @360 px, non-revealed).
(b) the one-time relief build vs the DEM grid (the O(N_dem) class):
    S3 ~1.3 s on the 2048x1024 DEM, knob 727 ms on its halved
    1024x512 DEM, host ~16 ms; the P4 leg is not captured (hatched).
(c) the revealed-bake tax (band flushes consume bake throughput):
    baseline vs the revealed window per board (S3 boot-replay final leg
    2009-2615 ms, knob 2028-2059 ms; the P4 leg deferred with the
    board off the bench).

Colors reuse the perf figure identities (gen_perf_figs.py): S3 blue,
P4 vermillion, knob violet, host neutral; hatched gray = not captured.

Run:  docs/.venv/bin/python docs/tools/gen_perf_variants.py
"""
from __future__ import annotations

import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_variants.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity
VERM = "#c2410c"      # P4 identity
AMBER = "#b07d10"     # knob identity
VIOLET = "#7c3aed"    # knob identity (perf_engines diamonds)

plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})

# Accepted captures (the on-device-verification standard). Quoted, not
# re-measured — see performance.rst / device-verify.rst for provenance.
DEVICES = {
    #  board    size wall_ms relief_ms baseline_ms reveal_lo reveal_hi
    "S3":   dict(px=400, wall=1300.0, relief=1300.0, base=1300.0,
                 rlo=2009.0, rhi=2615.0, color=BLUE,
                 dem="2048×1024 DEM"),
    "P4":   dict(px=650, wall=960.0, relief=None, base=960.0,
                 rlo=None, rhi=None, color=VERM, dem="2048×1024 DEM"),
    "knob": dict(px=360, wall=991.0, relief=727.0, base=991.0,
                 rlo=2028.0, rhi=2059.0, color=VIOLET, dem="1024×512 DEM"),
}


def main() -> None:
    with open(os.path.join(HERE, "perf_data.json")) as fh:
        perf = json.load(fh)
    host_ms = perf["host"]["s3"]["default"]["400"]["bake"]  # median of 5

    fig, axes = plt.subplots(1, 3, figsize=(11.0, 3.6))

    # ---- (a) microseconds per pixel at the native size (log) ----------
    ax = axes[0]
    rows = [("host C\n(400 px, same epoch)", host_ms * 1e3 / 400**2, MUTED)]
    for board, d in DEVICES.items():
        rows.append((f"{board}\n({d['px']} px)", d["wall"] * 1e3 / d["px"] ** 2,
                     d["color"]))
    ys = range(len(rows))
    ax.barh(list(ys), [r[1] for r in rows], color=[r[2] for r in rows],
            height=0.62)
    for y, (_, v, _) in enumerate(rows):
        ax.text(v * 1.15, y, f"{v:.2f} µs/px" if v >= 0.05 else f"{v*1000:.0f} ns/px",
                va="center", fontsize=7, color=INK)
    ax.set_xscale("log")
    ax.set_xlim(0.005, 40)
    ax.set_yticks(list(ys), [r[0] for r in rows])
    ax.invert_yaxis()
    ax.set_xlabel("µs per baked pixel (log)")
    ax.set_title("throughput per pixel at native size")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) one-time relief build vs the DEM grid ---------------------
    ax = axes[1]
    items = [("host C\n(2048×1024)", 16.0, MUTED),
             ("S3\n(2048×1024)", 1300.0, BLUE),
             ("knob\n(1024×512)", 727.0, VIOLET),
             ("P4\n(2048×1024)", 0.0, None)]
    xs = range(len(items))
    for x, (name, val, color) in enumerate(items):
        if color is None:
            ax.bar(x, 1300, color="none", edgecolor=MUTED, hatch="//",
                   linewidth=0.8, width=0.62)
            ax.text(x, 1300 * 0.5, "not\ncaptured", ha="center",
                    va="center", fontsize=6.5, color=MUTED)
        else:
            ax.bar(x, val, color=color, width=0.62)
            ax.text(x, val * 1.05, f"{val:.0f} ms", ha="center", fontsize=7,
                    color=INK)
    ax.set_xticks(list(xs), [i[0] for i in items], fontsize=7)
    ax.set_ylabel("relief build (ms)")
    ax.set_ylim(0, 1600)
    ax.set_title("one-time relief build = O(N_dem):\nthe knob's halved DEM halves the build")
    ax.grid(axis="y", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (c) the revealed-bake tax -------------------------------------
    ax = axes[2]
    xs = range(len(DEVICES))
    width = 0.36
    for i, (board, d) in enumerate(DEVICES.items()):
        ax.bar(i - width / 2, d["base"], width=width, color=d["color"],
               label=f"{board} baseline")
        ax.text(i - width / 2, d["base"] * 1.03, f"{d['base']:.0f}", ha="center",
                fontsize=6.5, color=INK)
        if d["rlo"] is not None:
            mid = (d["rlo"] + d["rhi"]) / 2
            ax.bar(i + width / 2, mid, width=width, color=d["color"],
                   alpha=0.35, edgecolor=d["color"], hatch="..")
            ax.errorbar(i + width / 2, mid,
                        yerr=[[mid - d["rlo"]], [d["rhi"] - mid]],
                        fmt="none", ecolor=INK, elinewidth=0.9, capsize=2.5)
            ax.text(i + width / 2, d["rhi"] * 1.04,
                    f"{d['rlo']:.0f}–{d['rhi']:.0f}", ha="center", fontsize=6.5,
                    color=INK)
        else:
            ax.bar(i + width / 2, 2300, width=width, color="none",
                   edgecolor=MUTED, hatch="//", linewidth=0.8)
            ax.text(i + width / 2, 2300 * 1.04, "P4 leg\ndeferred", ha="center",
                    fontsize=6.5, color=MUTED)
    ax.set_xticks(list(xs), list(DEVICES), fontsize=8)
    ax.set_ylabel("bake wall (ms)")
    ax.set_ylim(0, 2900)
    ax.set_title("plain bake vs the revealed bake\n(dotted = the accepted window)")
    ax.grid(axis="y", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
