#!/usr/bin/env python3
"""Generate the boot-path ladder comparison figure for
docs/verification/performance.rst — what each boot path costs the panel.

Since the 2026-11-25 boot re-source (docs/implementation/moon-movie)
the presentation (UI_MOON_BOOT_REPLAY) plays a SOURCE LADDER: the
flash-resident movie loops (UNBOUNDED — the bake is its stopper) while
the live now-bake runs in parallel, the catch-up leg plays forward to
the current phase (budget-bounded), and the card's stored hours play
only as the FALLBACK source (budget-bounded; the SD bus is touched on
that leg only). perf_hourly covers the STEADY state (the hourly
refresh); this figure covers the BOOT ladder, on the same visual
language (log ms axis, hatched = DATED pre-repurpose leg (the
2026-11-14 strip-reveal windows, retired by the 2026-11-24 compute
repurpose, docs/implementation/compute-progress), dashed = the device's
own budget constant.

The pacing constants are PARSED from the live Kconfigs (so figure and
Kconfig cannot drift apart) and the frame counts from the baked
manifests (the same sources the storage ladder re-derives):

  UI_MOON_REPLAY_FPS        default 8      -> pacing floor 1000/8 = 125 ms/frame
  UI_MOON_REPLAY_BUDGET_MS  default 240000 -> the bounded legs' budget 240 s
  KNOB_UI_MOVIE_FPS         default 8      -> the knob's loop floor
  assets/images/moon/out/{s3,knob}/moon_movie_manifest.json
                              frame_count — the S3's / the knob's cycle length

The rest are accepted captures / documented constants (provenance:
:doc:`on-device verification <device-verify>`, the 2026-11-14 bench,
and the cache policy + Kconfig help quoted in
docs/implementation/animations.rst):

  card-fallback mount probe   ~1 s      (SD probe timeout, Kconfig help —
                              the movie path never touches the bus)
  replay observed pace        ~6 fps on the S3 (the sweep-era TF
                              replay throughput the Kconfig quotes)
  cache cap                   709 hourly keys = one synodic month
  parallel now-bake (live)    1300 ms (S3 native 400 px plain bake; the
                              movie shows + the arc sweeps during it)
  plain static bake (ref)     1300 ms (S3 native 400 px floor)
  knob opening bake (live)    991 ms (its plain baseline; the movie
                              reveals over it — no SD socket, flash is
                              the knob's ONLY movie source)
  boot-replay final leg       2009-2615 ms (DATED 2026-11-14, revealed,
                              20 band states — the repurpose's before)
  knob revealed boot bake     2028-2059 ms (DATED 2026-11-14, vs its
                              991 ms baseline)

Left panel: the per-frame pacing arithmetic (floor vs observed).
Right panel: the boot paths against the 240 s budget — the probe (the
card-fallback leg alone), the static bake (BOOT_REPLAY=n), the
parallel now-bake (the loop's stopper), the movie catch-up worst case
(n/4 frames at the observed rate — REPLAY_CATCHUP_QUARTER), the
card-fallback full-cache play at both paces, and the knob's movie
cycle (the no-clock exit's one full loop) + bake pair. The movie LOOP
itself has no bar — it is unbounded; the bake is its stopper. The
Kconfig's own claim about the bounded legs — "a full cache at the
default pacing lands well inside the default 240 s" — stays a drawn
fact: 709 x 125 ms = 88.6 s floor, 709 x ~167 ms ~ 118 s observed.

Colors reuse the perf identities (gen_perf_figs.py): S3 blue, knob
violet, muted gray for the probe, red for the budget.

Run:  docs/.venv/bin/python docs/tools/gen_perf_boot.py
"""
from __future__ import annotations

import json
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
KCONFIG = os.path.join(HERE, "..", "..", "components", "ui", "Kconfig")
KNOB_KCONFIG = os.path.join(HERE, "..", "..", "knob",
                            "components", "knob_ui", "Kconfig")
MOVIE_MANIFEST = os.path.join(HERE, "..", "..",
                              "assets", "images", "moon", "out")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_boot.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # S3 identity
VIOLET = "#7c3aed"  # knob identity
RED = "#b91c1c"     # the budget line

# Accepted captures / documented constants (quoted, never projected —
# provenance in the module docstring and performance.rst).
PROBE_MS = 1000.0        # card-fallback SD mount probe (~1 s, Kconfig help)
OBS_FPS = 6.0            # observed ~6 fps on the S3 (Kconfig-quoted)
HOURS_CAP = 709          # cache cap: one synodic month of hourly keys
FINAL_LO, FINAL_HI = 2009.0, 2615.0   # S3 replay final leg (DATED, revealed)
PLAIN_BAKE_MS = 1300.0   # S3 native 400 px plain bake (live floor)
KNOB_LO, KNOB_HI = 2028.0, 2059.0     # knob revealed boot bake (DATED)
KNOB_PLAIN_MS = 991.0    # knob plain baseline (live)
CATCHUP_QUARTER = 4      # REPLAY_CATCHUP_QUARTER: far targets = one settle
                         # frame; the near path plays at most n/4 frames


def kconfig_default(symbol: str, path: str = KCONFIG) -> int:
    """The first ``default <int>`` under ``config <symbol>`` in the live
    Kconfig — the figure reads the device's own source."""
    text = open(path, encoding="utf-8").read()
    m = re.search(rf"config {symbol}\s*\n(?:[ \t].*\n)*?[ \t]+default (\d+)",
                  text)
    if not m:
        raise SystemExit(f"{symbol}: no integer default found in {path}")
    return int(m.group(1))


def movie_frames(target: str) -> int:
    """The baked cycle length for one board — the manifest the generator
    (assets/images/moon/moon/movie.py) wrote, the same source
    gen_perf_storage.py re-derives. Absent manifest = a not-yet-baked
    tree: fail loud, the figure must not draw a phantom cycle."""
    path = os.path.join(MOVIE_MANIFEST, target, "moon_movie_manifest.json")
    if not os.path.exists(path):
        raise SystemExit(
            f"{target}: no moon_movie_manifest.json — bake the movie first "
            "(cd assets/images/moon && .venv/bin/python -m moon.movie "
            f"--target {target})")
    with open(path, encoding="utf-8") as f:
        return int(json.load(f)["frame_count"])


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    fps = kconfig_default("UI_MOON_REPLAY_FPS")
    budget = float(kconfig_default("UI_MOON_REPLAY_BUDGET_MS"))
    knob_fps = kconfig_default("KNOB_UI_MOVIE_FPS", KNOB_KCONFIG)
    floor_ms = 1000.0 / fps
    knob_floor_ms = 1000.0 / knob_fps
    obs_ms = 1000.0 / OBS_FPS
    n_s3 = movie_frames("s3")
    n_knob = movie_frames("knob")
    # the bounded legs: the card fallback (709 files) + the catch-up
    # worst case (n/4 frames at the observed rate — the near path; a
    # far target is one settle frame + the reveal)
    anim_floor_ms = HOURS_CAP * floor_ms
    anim_obs_ms = HOURS_CAP * obs_ms
    catchup_ms = (n_s3 / CATCHUP_QUARTER) * obs_ms
    knob_cycle_ms = n_knob * knob_floor_ms

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
    ax.set_title("presentation pacing: the floor the Kconfig sets,\n"
                 "the rate the panel actually drains at")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the boot paths vs the budget -------------------------------
    ax = axes[1]
    rows = [
        ("card-fallback SD probe  ~1 s\n(the movie path never\ntouches the bus)",
         PROBE_MS, PROBE_MS, MUTED, "hit"),
        ("BOOT_REPLAY=n / demo:\nstatic bake  1300 ms", PLAIN_BAKE_MS,
         PLAIN_BAKE_MS, BLUE, "bake"),
        ("parallel now-bake (the loop's stopper)\n1300 ms (arc sweeps the\nwatermark while the movie loops)",
         PLAIN_BAKE_MS, PLAIN_BAKE_MS, BLUE, "bake"),
        (f"movie catch-up worst case (near path)\n{n_s3}/{CATCHUP_QUARTER} "
         f"frames × {obs_ms:.0f} ms ≈ {catchup_ms/1000:.0f} s (a far target is\n"
         f"one settle frame + the reveal)",
         catchup_ms, catchup_ms, BLUE, "anim_lo"),
        (f"card fallback, floor pacing\n{HOURS_CAP} × {floor_ms:.0f} ms ≈ "
         f"{anim_floor_ms/1000:.0f} s",
         anim_floor_ms, anim_floor_ms, BLUE, "anim_lo2"),
        (f"card fallback, observed ~6 fps\n{HOURS_CAP} × {obs_ms:.0f} ms ≈ "
         f"{anim_obs_ms/1000:.0f} s",
         anim_obs_ms, anim_obs_ms, BLUE, "anim"),
        ("S3 reveal window\n2009–2615 ms (DATED 2026-11-14)",
         FINAL_LO, FINAL_HI, BLUE, "dated"),
        (f"knob (flash = the ONLY movie source):\nmovie cycle, no-clock exit  "
         f"{n_knob} × {knob_floor_ms:.0f} ms ≈ {knob_cycle_ms/1000:.0f} s",
         knob_cycle_ms, knob_cycle_ms, VIOLET, "anim_v"),
        ("knob opening bake over the movie\n991 ms (arc sweeps the watermark)",
         KNOB_PLAIN_MS, KNOB_PLAIN_MS, VIOLET, "bake"),
        ("knob reveal window\n2028–2059 ms (DATED 2026-11-14)",
         KNOB_LO, KNOB_HI, VIOLET, "dated"),
    ]
    ys = range(len(rows))
    for i, (label, lo, hi, color, kind) in enumerate(rows):
        if kind in ("hit", "bake"):
            ax.barh(i, hi, color=color, alpha=0.55, height=0.6, edgecolor=color)
        elif kind in ("anim", "anim_v"):
            ax.barh(i, hi, color=color, alpha=0.9, height=0.6, edgecolor=color)
        elif kind in ("anim_lo", "anim_lo2"):
            ax.barh(i, hi, color=color, alpha=0.45, height=0.6, edgecolor=color)
        else:  # dated: the pre-repurpose reveal window
            ax.barh(i, hi, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.2)
            if hi > lo:
                ax.errorbar((lo + hi) / 2, i, xerr=(hi - lo) / 2, fmt="none",
                            ecolor=color, capsize=3, linewidth=1.0)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=7)
    ax.axvline(budget, color=RED, linestyle="--", linewidth=1.2)
    ax.text(budget * 0.90, -0.45,
            f"replay budget\n{budget/1000:.0f} s\nUI_MOON_REPLAY_BUDGET_MS —\nbounds the bounded legs\n"
            f"(the movie loop is\nunbounded — the\nbake is its stopper)",
            color=RED, fontsize=6.4, ha="right", va="top")
    ax.set_xscale("log")
    ax.set_xlim(400, 600000)
    ax.set_xlabel("wall ms (log) — boot paths, S3 466 (knob rows tagged)")
    ax.set_title("what boot costs the panel — the source ladder vs the "
                 "animation budget",
                 fontsize=8.5)
    ax.grid(axis="x", color=HAIR, linewidth=0.5)
    ax.set_axisbelow(True)
    ax.set_ylim(len(rows) - 0.4, -0.6)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(fps={fps} floor={floor_ms:.0f}ms budget={budget:.0f}ms "
          f"knob_fps={knob_fps} n_s3={n_s3} n_knob={n_knob})")


if __name__ == "__main__":
    main()
