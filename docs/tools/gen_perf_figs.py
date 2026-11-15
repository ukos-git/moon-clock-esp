#!/usr/bin/env python3
"""Generate the figures for docs/verification/performance.rst — the plots
that replaced the measurement tables.

Drawn from ``perf_data.json`` (written by ``perf_measure.py``, committed
next to it so the figures are reproducible without re-running the bench):

  perf_engines.png  bake time vs disk size, all engines (log-log) — the
                    four live engine curves + the accepted-capture device
                    points on the same ms axis, with the measured log-log
                    slope (= the empirical O(size^k)) annotated per curve.
  perf_vs_pixels.png
                    bake time vs PIXEL COUNT (linear, origin-anchored) —
                    the O(N) proof: a straight line through the origin is
                    what "cost scales with input count" looks like.
  perf_steps.png    the host per-step breakdown vs size (log-log): bake /
                    rotate / re-project track size^2; the shade-map build
                    is flat (slope 0 = size-independent O(1)).
  perf_ab.png       the A/B settings trade-off (host C): relief on/off and
                    the display path, per size — grouped bars.

Palette (validated with the dataviz six-checks validator, light surface):
blue #2b5fb0, vermilion #c2410c, cyan #0891b2, amber #b07d10; gray dashed
guides are reference lines, not entities. Every figure carries a legend
and direct labels (identity is never color-alone).

Run:  docs/.venv/bin/python docs/tools/gen_perf_figs.py
"""
from __future__ import annotations

import json
import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "_static", "figs"))
DATA = json.loads(open(os.path.join(HERE, "perf_data.json")).read())

# --- house style (matches gen_lighting_figs.py) -----------------------------
INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # C host / S3 entity
VERM = "#c2410c"     # P4 entity
CYAN = "#0891b2"     # Python reference
AMBER = "#b07d10"    # JS port
VIOLET = "#7c3aed"   # knob entity (hollow diamonds — identity never color alone)

plt.rcParams.update({
    "font.size": 9.5,
    "font.family": "DejaVu Sans",
    "text.color": INK,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "axes.linewidth": 0.8,
    "lines.linewidth": 2.0,
    "axes.grid": True,
    "grid.color": HAIR,
    "grid.linewidth": 0.6,
    "figure.dpi": 150,
    "savefig.dpi": 150,
})

MS = DATA["sizes"]
HOST_S3 = DATA["host"]["s3"]["default"]
HOST_P4 = DATA["host"]["p4"]["default"]
PY = DATA["python"]
JS = DATA["js"]["bake"]
DEV = DATA["device"]["points"]


def slope(xs, ys):
    """Measured log-log slope (the empirical exponent k in time ~ size^k)."""
    k, _ = np.polyfit(np.log(xs), np.log(ys), 1)
    return k


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", name)


def end_label(ax, x, y, text, color, dx=8, dy=0, ha="left"):
    ax.annotate(text, (x, y), textcoords="offset points", xytext=(dx, dy),
                ha=ha, va="center", fontsize=8.5, color=INK)


# --- fig 1: engines vs disk size (log-log) -----------------------------------
def fig_engines():
    fig, ax = plt.subplots(figsize=(6.6, 4.3))
    series = [
        ("C core (host, S3 assets)", [HOST_S3[str(s)]["bake"] for s in MS], BLUE, "-", "o"),
        ("C core (host, P4 assets)", [HOST_P4[str(s)]["bake"] for s in MS], VERM, "-", "s"),
        ("Python (numpy, float64)", [PY[str(s)] for s in MS], CYAN, "-", "^"),
        ("JS (V8, scalar)", [JS[str(s)] for s in MS], AMBER, "-", "v"),
    ]
    handles = []
    slopes = []
    for name, ys, color, ls, mk in series:
        k = slope(MS, ys)
        slopes.append(k)
        (ln,) = ax.plot(MS, ys, ls, color=color, marker=mk, markersize=5,
                        markerfacecolor=color, markeredgecolor="white",
                        markeredgewidth=0.8)
        handles.append((ln, f"{name} — slope {k:.2f}"))
        if not name.startswith("C core"):     # the two host curves overlap;
            end_label(ax, MS[-1], ys[-1], name.split(" (")[0], color, dx=14)
                                              # the legend carries them
    # the accepted-capture device points, same ms axis (no second scale).
    # The legend is assembled in the validated adjacent-hue order (blue,
    # vermilion, cyan, amber) by pairing each device series with its
    # host-line hue; marker SHAPE + direct labels carry identity too.
    dev_handles = {}
    for board, pts, color, mk in (("S3", DEV["s3"], BLUE, "o"),
                                  ("P4", DEV["p4"], VERM, "s"),
                                  ("Knob", DEV["knob"], VIOLET, "D")):
        xs = [p["size"] for p in pts]
        ys = [p["ms"] for p in pts]
        (ln,) = ax.plot(xs, ys, "", color=color, marker=mk, markersize=8,
                        markerfacecolor="white", markeredgecolor=color,
                        markeredgewidth=1.6)
        dev_handles[board] = (ln, f"{board} device (accepted capture)")
    # O(size^2) guide through the host C curve at 200 px
    k2 = [HOST_S3["200"]["bake"] * (s / 200) ** 2 for s in MS]
    (ref,) = ax.plot(MS, k2, "--", color=MUTED, lw=1.2, dashes=(5, 4))
    order = [handles[0], dev_handles["S3"], handles[1], dev_handles["P4"],
             dev_handles["Knob"], handles[2], handles[3],
             (ref, "reference: time ∝ size² (O(N) in pixels)")]
    # the legend sits ABOVE the axes, not inside them: the knob's single
    # accepted point (360 px / 991 ms) lands at the top of the field and an
    # in-plot legend box swallowed it (framealpha 0.95 = effectively opaque)
    ax.legend([h for h, _ in order], [t for _, t in order],
              loc="lower left", bbox_to_anchor=(0, 1.02), ncol=3,
              frameon=True, framealpha=0.95,
              edgecolor=HAIR, fontsize=7.5)
    ax.set_xscale("log")
    ax.set_yscale("log")
    # 360 omitted from the ticks: the knob's point is labelled by the
    # legend, and the tick text collided with 320/400 on the log axis
    ax.set_xticks([200, 240, 320, 400, 466, 650])
    ax.set_xticklabels([200, 240, 320, 400, 466, 650])
    ax.set_xlim(180, 1100)
    ax.minorticks_off()
    ax.set_xlabel("bake disk size (px)")
    ax.set_ylabel("single-frame bake time (ms)")
    ax.set_title(f"One bake, six engines — every engine is quadratic in size "
                 f"(measured slopes {min(slopes):.1f}–{max(slopes):.1f})",
                 fontsize=10, color=INK, pad=46)
    fig.tight_layout()
    save(fig, "perf_engines.png")


# --- fig 2: time vs input count (linear, O(N) proof) -------------------------
def fig_pixels():
    fig, ax = plt.subplots(figsize=(6.6, 4.1))
    n = [s * s for s in MS]
    series = [
        ("C core (host)", [HOST_S3[str(s)]["bake"] for s in MS], BLUE, "o"),
        ("Python (numpy)", [PY[str(s)] for s in MS], CYAN, "^"),
        ("JS (V8)", [JS[str(s)] for s in MS], AMBER, "v"),
    ]
    handles = []
    for name, ys, color, mk in series:
        (ln,) = ax.plot(n, ys, color=color, marker=mk, markersize=5,
                        markerfacecolor=color, markeredgecolor="white",
                        markeredgewidth=0.8)
        handles.append((ln, name))
        if name != "C core (host)":
            # no end label for the host C here: it hugs the x-axis and its
            # label overprinted the JS one; the legend + the linear-fit
            # entry already name that band
            end_label(ax, n[-1], ys[-1], name, color, dx=10)
    # device points on the same axis
    dev = {}
    for board, pts, color, mk in (("S3 device", DEV["s3"], BLUE, "o"),
                                  ("P4 device", DEV["p4"], VERM, "s"),
                                  ("Knob device", DEV["knob"], VIOLET, "D")):
        xs = [p["size"] ** 2 for p in pts]
        ys = [p["ms"] for p in pts]
        (ln,) = ax.plot(xs, ys, "", marker=mk, markersize=8, markerfacecolor="white",
                        markeredgecolor=color, markeredgewidth=1.6)
        dev[board] = (ln, board)
    # origin-anchored linear fit for the host C (the O(N) claim, fitted)
    ys = np.array([HOST_S3[str(s)]["bake"] for s in MS])
    k = float(np.polyfit(n, ys, 1)[0])
    (fit,) = ax.plot([0, n[-1]], [0, k * n[-1]], "--", color=MUTED, lw=1.2,
                     dashes=(5, 4))
    order = [handles[0], dev["S3 device"], dev["P4 device"], dev["Knob device"],
             handles[1],
             handles[2],
             (fit, f"host C linear fit ({k * 1e6:.1f} ns/px)")]
    # above the axes (same reason as fig 1: the device points live in the
    # upper field, where an in-plot legend box hides them)
    ax.legend([h for h, _ in order], [t for _, t in order],
              loc="lower left", bbox_to_anchor=(0, 1.02), ncol=3,
              frameon=True, framealpha=0.95,
              edgecolor=HAIR, fontsize=8)
    ax.set_xlabel("bake input count (pixels = size²)")
    ax.set_ylabel("bake time (ms)")
    ax.set_title("Time vs input count: every engine is linear in pixels — O(N)",
                 fontsize=10, color=INK, pad=48)
    ax.set_ylim(bottom=0)
    fig.tight_layout()
    save(fig, "perf_vs_pixels.png")


# --- fig 3: per-step breakdown vs size ---------------------------------------
def fig_steps():
    fig, ax = plt.subplots(figsize=(6.6, 4.1))
    steps = [
        ("bake", "bake", BLUE, "o"),
        ("rotate (spin gather)", "rotate", VERM, "s"),
        ("shade-map build (once)", "mapbuild", CYAN, "^"),
        ("re-project (dusk frame)", "reproj", AMBER, "v"),
    ]
    for name, key, color, mk in steps:
        ys = [HOST_S3[str(s)][key] for s in MS]
        k = slope(MS, ys)
        ax.plot(MS, ys, color=color, marker=mk, markersize=5,
                markerfacecolor=color, markeredgecolor="white",
                markeredgewidth=0.8,
                label=f"{name} — measured slope {k:.2f}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks([200, 240, 320, 400, 466, 650])
    ax.set_xticklabels([200, 240, 320, 400, 466, 650])
    ax.set_xlim(180, 1100)
    ax.minorticks_off()
    ax.set_xlabel("disk size (px)")
    ax.set_ylabel("step time (ms)")
    ax.set_title("Host C per-step cost vs size: per-pixel steps scale size²; "
                 "the one-time shade-map build is flat — O(1) in display size",
                 fontsize=10, color=INK)
    ax.legend(loc="lower right", frameon=True, framealpha=0.95,
              edgecolor=HAIR, fontsize=8)
    fig.tight_layout()
    save(fig, "perf_steps.png")


# --- fig 4: A/B settings trade-off (grouped bars) ----------------------------
def fig_ab():
    fig, ax = plt.subplots(figsize=(6.8, 4.1))
    variants = [
        ("gate path (relief on)", DATA["host"]["s3"]["default"], BLUE),
        ("relief off", DATA["host"]["s3"]["no_relief"], AMBER),
        ("display path (dark_zero + neutral)", DATA["host"]["s3"]["display"], CYAN),
    ]
    w = 0.26
    xs = np.arange(len(MS))
    for i, (name, data, color) in enumerate(variants):
        ys = [data[str(s)]["bake"] for s in MS]
        ax.bar(xs + (i - 1) * w, ys, w * 0.94, color=color, label=name,
               edgecolor="white", linewidth=1.0)
        for x, y in zip(xs + (i - 1) * w, ys):
            ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points",
                        xytext=(0, 2), ha="center", fontsize=6.8, color=MUTED)
    # the 400-px product size gets the ratio callouts
    d, off, disp = (v[1]["400"]["bake"] for v in variants)
    ax.annotate(f"relief off = {off / d - 1:+.0%}\ndisplay path = {disp / d - 1:+.0%}",
                (xs[MS.index(400)] + 0.55, 4.4), fontsize=8, color=INK,
                ha="center", va="bottom",
                bbox=dict(boxstyle="round,pad=0.3", fc="#f6f7f9", ec=HAIR, lw=0.8))
    ax.set_xticks(xs)
    ax.set_xticklabels(MS)
    ax.set_xlabel("bake disk size (px)")
    ax.set_ylabel("bake time (ms)")
    ax.set_title(f"Setting trade-offs (host C, same epoch, @400 px): relief off "
                 f"saves {1 - off / d:.0%}, the display path costs {disp / d - 1:+.0%}",
                 fontsize=10, color=INK)
    ax.set_ylim(0, 8.6)
    ax.grid(axis="x", visible=False)
    ax.legend(loc="upper left", frameon=True, framealpha=0.95,
              edgecolor=HAIR, fontsize=8)
    fig.tight_layout()
    save(fig, "perf_ab.png")


# --- fig 5: device pipelines (accepted captures, log horizontal bars) --------
def fig_device():
    fig, ax = plt.subplots(figsize=(6.8, 4.3))
    cats = {"bake": BLUE, "display": CYAN, "build": AMBER, "fade": VERM}
    rows = DATA["device"]["pipelines"]
    ys = np.arange(len(rows))
    for y, r in zip(ys, rows):
        ax.barh(y, r["ms"], 0.72, color=cats[r["cat"]], edgecolor="white",
                linewidth=1.0)
        ax.annotate(f"{r['ms']:.0f} ms" if r["ms"] >= 10 else f"{r['ms']:.1f} ms",
                    (r["ms"], y), textcoords="offset points", xytext=(4, 0),
                    va="center", fontsize=7.5, color=MUTED)
    ax.set_yticks(ys)
    ax.set_yticklabels([f"[{r['board']}] {r['name']}" for r in rows], fontsize=8)
    ax.invert_yaxis()                       # list order reads top-down
    ax.set_xscale("log")
    ax.set_xlim(30, 130000)
    ax.set_xlabel("wall time per frame / step (ms, log)")
    ax.set_title("Device pipelines (accepted captures): the spin/dusk frame "
                 "costs tens of ms, a bake costs hundreds", fontsize=10, color=INK)
    # legend in the validated adjacent-hue order (blue, vermilion, cyan,
    # amber — the same set fig 1 passes the six-checks validator with)
    handles = [plt.Rectangle((0, 0), 1, 1, color=cats[k], ec="white")
               for k in ("bake", "fade", "display", "build")]
    ax.legend(handles, ["bake (per frame)", "fade (brightness ramp)",
                        "display-paced (per frame)", "one-time build"],
              loc="lower right", frameon=True, framealpha=0.95,
              edgecolor=HAIR, fontsize=8)
    fig.tight_layout()
    save(fig, "perf_device.png")


# --- fig 6: device CPU one-core shares ---------------------------------------
def fig_cpu():
    fig, ax = plt.subplots(figsize=(6.6, 2.1))
    rows = DATA["device"]["cpu"]
    ys = np.arange(len(rows))[::-1]
    for y, r in zip(ys, rows):
        ax.barh(y, r["pct"], 0.6, color=BLUE, edgecolor="white", linewidth=1.0)
        if r["hi"] > r["lo"]:               # measured range whisker
            ax.plot([r["lo"], r["hi"]], [y, y], color=INK, lw=2,
                    solid_capstyle="butt")
        ax.annotate(f"{r['lo']:.0f}–{r['hi']:.0f} %" if r["hi"] > r["lo"]
                    else f"{r['pct']:.0f} %", (r["pct"], y),
                    textcoords="offset points", xytext=(5, 0), va="center",
                    fontsize=8.5, color=MUTED)
    ax.set_yticks(ys)
    ax.set_yticklabels([r["name"] for r in rows], fontsize=8.5)
    ax.invert_yaxis()
    ax.set_xlim(0, 60)
    ax.set_xlabel("CPU share, one core (%)")
    ax.grid(axis="y", visible=False)
    fig.tight_layout()
    save(fig, "perf_cpu.png")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    fig_engines()
    fig_pixels()
    fig_steps()
    fig_ab()
    fig_device()
    fig_cpu()
