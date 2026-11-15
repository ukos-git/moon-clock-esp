#!/usr/bin/env python3
"""Generate the boot-path ladder comparison figure for
docs/verification/performance.rst — what each boot path costs the panel.

The boot replay (UI_MOON_BOOT_REPLAY, 1c90a64) gave boot four outcomes
the product pays for; perf_hourly covers the STEADY state (the hourly
refresh), this figure covers the BOOT ladder, on the same visual
language (log ms axis, hatched = revealed leg, dashed = the device's
own budget constant).

The pacing constants are PARSED from components/ui/Kconfig (the live
sources, so figure and Kconfig cannot drift apart):

  UI_MOON_REPLAY_FPS        default 8      -> pacing floor 1000/8 = 125 ms/frame
  UI_MOON_REPLAY_BUDGET_MS  default 240000 -> whole-animation budget 240 s

The rest are accepted captures / documented constants (provenance:
:doc:`on-device verification <device-verify>`, the 2026-11-14 bench,
and the cache policy + Kconfig help quoted in
docs/implementation/animations.rst):

  card-less mount probe         ~1 s      (SD probe timeout, Kconfig help)
  replay observed pace          ~6 fps on the S3 (the sweep-era TF
                              replay throughput the Kconfig quotes)
  cache cap                     709 hourly keys = one synodic month
  boot-replay final leg         2009-2615 ms (S3, revealed, 20 band states)
  plain static bake (ref)       1300 ms (S3 native 400 px floor)
  knob revealed boot bake       2028-2059 ms (vs its 991 ms baseline;
                              no SD socket -> always the black path)

Left panel: the per-frame pacing arithmetic (floor vs observed).
Right panel: the whole-animation / first-live-content ladder against
the 240 s budget. The Kconfig's own claim — "a full cache at the
default pacing lands well inside the default 240 s" — becomes a drawn
fact: 709 x 125 ms = 88.6 s floor, 709 x ~167 ms ~ 118 s observed.

Colors reuse the perf identities (gen_perf_figs.py): S3 blue, knob
violet, muted gray for the probe, red for the budget.

Run:  docs/.venv/bin/python docs/tools/gen_perf_boot.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
KCONFIG = os.path.join(HERE, "..", "..", "components", "ui", "Kconfig")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_boot.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # S3 identity
VIOLET = "#7c3aed"  # knob identity
RED = "#b91c1c"     # the budget line

# Accepted captures / documented constants (quoted, never projected —
# provenance in the module docstring and performance.rst).
PROBE_MS = 1000.0        # card-less SD mount probe (~1 s, Kconfig help)
OBS_FPS = 6.0            # observed ~6 fps on the S3 (Kconfig-quoted)
HOURS_CAP = 709          # cache cap: one synodic month of hourly keys
FINAL_LO, FINAL_HI = 2009.0, 2615.0   # S3 replay final leg (revealed)
PLAIN_BAKE_MS = 1300.0   # S3 native 400 px non-revealed floor
KNOB_LO, KNOB_HI = 2028.0, 2059.0     # knob revealed boot bake
KNOB_PLAIN_MS = 991.0    # knob non-revealed baseline


def kconfig_default(symbol: str) -> int:
    """The first ``default <int>`` under ``config <symbol>`` in the live
    components/ui/Kconfig — the figure reads the device's own source."""
    text = open(KCONFIG, encoding="utf-8").read()
    m = re.search(rf"config {symbol}\s*\n(?:[ \t].*\n)*?[ \t]+default (\d+)",
                  text)
    if not m:
        raise SystemExit(f"{symbol}: no integer default found in ui/Kconfig")
    return int(m.group(1))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    fps = kconfig_default("UI_MOON_REPLAY_FPS")
    budget = float(kconfig_default("UI_MOON_REPLAY_BUDGET_MS"))
    floor_ms = 1000.0 / fps
    obs_ms = 1000.0 / OBS_FPS
    anim_floor_ms = HOURS_CAP * floor_ms
    anim_obs_ms = HOURS_CAP * obs_ms

    fig, axes = plt.subplots(1, 2, figsize=(11.0, 3.4))

    # ---- (a) per-frame pacing: floor vs observed ------------------------
    ax = axes[0]
    rows = [(f"pacing floor\n1000 / UI_MOON_REPLAY_FPS\n1000/{fps} = {floor_ms:.0f} ms",
             floor_ms, BLUE, 0.45),
            (f"observed pace (~6 fps, S3)\n1000/{OBS_FPS:.0f} ≈ {obs_ms:.0f} ms",
             obs_ms, BLUE, 0.9)]
    ys = range(len(rows))
    for y, (label, val, color, alpha) in enumerate(rows):
        ax.barh(y, val, color=color, alpha=alpha, height=0.55,
                edgecolor=color)
        ax.text(val * 1.05, y, f"{val:.0f} ms/frame", va="center",
                fontsize=7, color=INK)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_xlim(0, 240)
    ax.set_xlabel("ms per shown frame (linear)")
    ax.set_title("replay pacing: the floor the Kconfig sets,\nthe rate the panel actually drains at")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the boot ladder vs the budget ------------------------------
    ax = axes[1]
    rows = [
        ("card-less SD probe  ~1 s", PROBE_MS, PROBE_MS, MUTED, "hit"),
        ("BOOT_REPLAY=n / demo:\nstatic bake  1300 ms", PLAIN_BAKE_MS,
         PLAIN_BAKE_MS, BLUE, "bake"),
        ("card-less: revealed now-bake\n2009–2615 ms (first ink at ~1 s)",
         FINAL_LO, FINAL_HI, BLUE, "reveal"),
        (f"full cache, floor pacing\n{HOURS_CAP} × {floor_ms:.0f} ms ≈ {anim_floor_ms/1000:.0f} s",
         anim_floor_ms, anim_floor_ms, BLUE, "anim_lo"),
        (f"full cache, observed ~6 fps\n{HOURS_CAP} × {obs_ms:.0f} ms ≈ {anim_obs_ms/1000:.0f} s",
         anim_obs_ms, anim_obs_ms, BLUE, "anim"),
        ("knob (no SD socket):\nblack → revealed bake 2028–2059",
         KNOB_LO, KNOB_HI, VIOLET, "reveal"),
        ("knob plain bake (ref)  991 ms", KNOB_PLAIN_MS, KNOB_PLAIN_MS,
         VIOLET, "bake"),
    ]
    ys = range(len(rows))
    for i, (label, lo, hi, color, kind) in enumerate(rows):
        if kind == "hit":
            ax.barh(i, hi, color=color, alpha=0.5, height=0.6, edgecolor=color)
        elif kind == "bake":
            ax.barh(i, hi, color=color, alpha=0.55, height=0.6, edgecolor=color)
        elif kind in ("anim", "anim_lo"):
            ax.barh(i, hi, color=color, alpha=0.9 if kind == "anim" else 0.45,
                    height=0.6, edgecolor=color)
        else:  # reveal: accepted window
            ax.barh(i, hi, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.2)
            if hi > lo:
                ax.errorbar((lo + hi) / 2, i, xerr=(hi - lo) / 2, fmt="none",
                            ecolor=color, capsize=3, linewidth=1.0)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=7)
    ax.axvline(budget, color=RED, linestyle="--", linewidth=1.2)
    ax.text(budget * 0.90, -0.45,
            f"replay budget\n{budget/1000:.0f} s\nUI_MOON_REPLAY_BUDGET_MS —\ncuts the animation,\nnever the moon",
            color=RED, fontsize=6.4, ha="right", va="top")
    ax.set_xscale("log")
    ax.set_xlim(400, 400000)
    ax.set_xlabel("wall ms (log) — boot outcomes, S3 466 (knob rows tagged)")
    ax.set_title("what boot costs the panel — every path vs the animation budget",
                 fontsize=8.5)
    ax.grid(axis="x", color=HAIR, linewidth=0.5)
    ax.set_axisbelow(True)
    ax.set_ylim(len(rows) - 0.4, -0.6)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(fps={fps} floor={floor_ms:.0f}ms budget={budget:.0f}ms)")


if __name__ == "__main__":
    main()
