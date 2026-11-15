#!/usr/bin/env python3
"""Generate the hourly-refresh ladder comparison figure for
docs/verification/performance.rst — the live pipeline's cost ladder.

The once-per-hour refresh has exactly four outcomes the product pays
for, and the bake-reveal bench (2026-11-14, CONFIG_TEST_HOURLY_FORCE_S
cold/warm captures) measured all of them. This figure puts them on one
log millisecond axis beside the two source constants they are judged
against, so the ladder reads as the producer's actual decision space:

  warm TF hour-cache hit   185–235 ms   (S3, after the
                           new_moon_epoch_cached memoization)
  + the two fade legs      2 x 240 ms   (UI_MOON_HOURLY_FADE_MS, the
                           hardware brightness ramp - writes no pixels)
  plain static bake (ref)  ~1300 ms     (S3 native 400 px floor, the
                           same capture as gen_perf_variants)
  boot-replay final leg    2009–2615 ms (S3, 20 band states)
  cold-cycle revealed bake 2687 ms       (internals; whole cycle 3083)
  knob revealed boot bake  2028–2059 ms  (vs its 991 ms baseline)

  producer budget          5000 ms      (HOURLY_BUDGET_MS,
                           components/ui/moon_ui.c:1006)
  pre-fix hourly cycle     ~8200 ms     (cache-policy leg re-ran the
                           ~8.2 s ephemeris scan every cycle — the
                           bench-#44 catch that blew the budget every
                           hour before the memoization)

Bars carry their accepted min–max as an error bar (the bench-#44
capture windows), hatched = revealed (computing-visible) leg, solid =
non-revealed. Colors reuse the perf identities (gen_perf_figs.py): S3
blue, knob violet, fade vermilion, budget red.

All values quoted from the accepted captures on the page (provenance:
:doc:`on-device verification <device-verify>`, 2026-11-14 bench);
the fade/budget lines are the device's own constants. Nothing here is
a projection.

Run:  docs/.venv/bin/python docs/tools/gen_perf_hourly.py
"""
from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_hourly.png")

INK = "#1c2128"
MUTED = "#5c6675"
BLUE = "#2b5fb0"    # S3 identity
VIOLET = "#7c3aed"  # knob identity
VERM = "#c2410c"    # the brightness-ramp fade (perf_device convention)
RED = "#b91c1c"     # the budget line

# (label, lo_ms, hi_ms, kind) kind: hit/fade/bake solid, reveal hatched,
# pre = the pre-fix over-budget marker
ROWS = [
    ("hourly warm TF hit  185–235 ms", 185, 235, "hit"),
    ("+ 2 fade legs  2×240 ms", 480, 480, "fade"),
    ("plain bake (ref)  1300 ms", 1300, 1300, "bake"),
    ("boot-replay final leg  2009–2615 ms", 2009, 2615, "reveal"),
    ("cold revealed bake  2687 ms (cycle 3083)", 2687, 2687, "reveal"),
    ("knob revealed bake  2028–2059", 2028, 2059, "reveal"),
    ("knob plain bake (ref)  991 ms", 991, 991, "bake"),
]
PRE_FIX_MS = 8200.0
BUDGET_MS = 5000.0

plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    fig, ax = plt.subplots(figsize=(8.6, 3.3))
    ys = range(len(ROWS))
    for i, (label, lo, hi, kind) in enumerate(ROWS):
        color = VERM if kind == "fade" else VIOLET if "knob" in label else BLUE
        if kind == "bake":
            ax.barh(i, hi, color=color, height=0.62, alpha=0.55,
                    edgecolor=color)
        elif kind == "hit":
            ax.barh(i, hi, left=lo, color=color, height=0.62, alpha=0.9,
                    edgecolor=color)
            ax.barh(i, lo, color=color, height=0.62, alpha=0.45,
                    edgecolor=color)
        else:  # reveal: hatched accepted window
            ax.barh(i, hi, color="white", height=0.62, edgecolor=color,
                    hatch="..", linewidth=1.2)
            if hi > lo:
                ax.errorbar((lo + hi) / 2, i, xerr=(hi - lo) / 2, fmt="none",
                            ecolor=color, capsize=3, linewidth=1.0)
    ax.set_yticks(list(ys))
    ax.set_yticklabels([r[0] for r in ROWS], fontsize=7.5)
    ax.axvline(BUDGET_MS, color=RED, linestyle="--", linewidth=1.2)
    ax.text(BUDGET_MS, -1.35, "producer budget 5000 ms\nHOURLY_BUDGET_MS (moon_ui.c)",
            color=RED, fontsize=7, ha="center", va="center")
    ax.scatter([PRE_FIX_MS], [3.0], marker="x", s=70, color=RED, zorder=5)
    ax.text(PRE_FIX_MS * 1.07, 3.0, "pre-fix hourly cycle ~8200 ms:\nthe cache-policy leg\nblew the budget (bench #44)",
            color=RED, fontsize=6.6, ha="left", va="center")
    ax.set_xscale("log")
    ax.set_xlim(80, 20000)
    ax.set_xlabel("wall ms (log) — hourly refresh ladder, S3 466 (knob rows tagged)")
    ax.set_title("what the most frequent refresh actually costs — every outcome vs the budget",
                 fontsize=8.5)
    ax.grid(axis="x", color="#d9dde3", linewidth=0.5)
    ax.set_axisbelow(True)
    ax.set_ylim(len(ROWS) - 0.4, -2.1)
    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
