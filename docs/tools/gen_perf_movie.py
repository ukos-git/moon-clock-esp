#!/usr/bin/env python3
"""Generate the boot-movie figure for
docs/implementation/moon-movie.rst — the precomputed synodic cycle that
the boot presentation plays (looping) while the live now-bake runs in
parallel (the 2026-11-25 boot re-source).

Left panel — the footprint, per board: the baked ``moon_movie.bin``
against its ``moon_movie`` partition row (row = the ceiling). The
generator (assets/images/moon/moon/movie.py) fills the row with the
FINEST phase step that fits — the "fill the space, never hinder the
code build" policy — so the drawn bar is the generator's own output,
re-derived here.

Right panel — the frames over the cycle: one strip per board on the
hour axis from the header's ``ref_center`` over the span the cache caps
at (709 h = one synodic month, the same cap as the TF hour cache);
every tick is one baked frame. The three boards' densities make the
size-adaptive policy visible: S3 and knob at 2 h/frame (355 frames),
P4 at 4 h/frame (178) — and frame 0 ≈ frame n-1, so the loop is
seamless by construction.

Parsed at figure time (SystemExit-failing, the house pattern — figure
and sources cannot drift apart):

  moon_movie rows            partitions.csv, partitions_p4.csv,
                             knob/partitions.csv (K or hex size)
  the blobs + manifests      assets/images/moon/out/{s3,p4,knob}/
                             moon_movie.bin (on-disk size — asserted
                             EQUAL to the manifest's total_bytes, the
                             same pair the CMake guard #2 prices) +
                             moon_movie_manifest.json (target, s,
                             radius, frame_count, hours_per_frame,
                             ref_center)
  the blob sizes             the same out/ dirs' moon_movie.bin — the
                             on-disk size is asserted EQUAL to the
                             manifest's total_bytes (a stale blob
                             fails the figure) and to the partition
                             row (the tables are fully allocated — the
                             movie row owns each chip's leftover)

Colors reuse the perf identities (gen_perf_figs.py): S3 blue, P4
vermillion, knob violet.

Run:  docs/.venv/bin/python docs/tools/gen_perf_movie.py
"""
from __future__ import annotations

import json
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_movie.png")

TARGETS = ("s3", "p4", "knob")
CSV = {"s3": "partitions.csv", "p4": "partitions_p4.csv",
       "knob": "knob/partitions.csv"}
OUT_DIR = {"s3": "s3", "p4": "p4", "knob": "knob"}

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # S3 identity
VERM = "#c2410c"     # P4 identity
VIOLET = "#7c3aed"   # knob identity
COLOR = {"s3": BLUE, "p4": VERM, "knob": VIOLET}
NAME = {"s3": "S3 466", "p4": "P4 720", "knob": "knob 360"}

# one synodic month — the span the TF hour cache caps at (TF_MAX_HOURS)
# and the movie's 709-frame cycle fills.
SYNODIC_H = 709.0


def _n(s: str) -> int:
    """'2048K' / '0x8E0000' -> bytes."""
    s = s.strip().replace(",", "")
    if s.lower().startswith("0x"):
        return int(s, 16)
    if s.upper().endswith("K"):
        return int(s[:-1]) * 1024
    return int(s)


def movie_row_bytes(t: str) -> int:
    """The committed ``moon_movie`` row of one table (the row is the
    source of truth — the generator reads it the same way)."""
    path = os.path.join(ROOT, CSV[t])
    m = re.search(r"^moon_movie\s*,[^\n]*?,\s*([0-9]+K|0x[0-9A-Fa-f]+)\s*,?",
                  open(path, encoding="utf-8").read(), re.M)
    if not m:
        raise SystemExit(f"{CSV[t]}: no moon_movie row")
    return _n(m.group(1))


def manifest(t: str) -> dict:
    path = os.path.join(ROOT, "assets", "images", "moon", "out",
                        OUT_DIR[t], "moon_movie_manifest.json")
    if not os.path.exists(path):
        raise SystemExit(
            f"{t}: no moon_movie_manifest.json — bake the movie first "
            f"(cd assets/images/moon && .venv/bin/python -m moon.movie "
            f"--target {t})")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    data = {}
    for t in TARGETS:
        m = manifest(t)
        row = movie_row_bytes(t)
        blob = os.path.join(ROOT, "assets", "images", "moon", "out",
                            OUT_DIR[t], "moon_movie.bin")
        disk = os.path.getsize(blob) if os.path.exists(blob) else None
        if m["total_bytes"] != disk:
            raise SystemExit(
                f"{t}: manifest total_bytes ({m['total_bytes']}) != on-disk "
                f"{blob} ({disk}) — regenerate the movie")
        if m["target"] != t:
            raise SystemExit(f"{t}: manifest target is {m['target']}")
        if m["total_bytes"] > row:
            raise SystemExit(
                f"{t}: movie blob {m['total_bytes']} B exceeds its "
                f"{row} B moon_movie row — widen the row")
        span_h = (m["frame_count"] - 1) * m["hours_per_frame"]
        data[t] = dict(m=m, row=row, blob_bytes=m["total_bytes"],
                       margin=row - m["total_bytes"], span_h=span_h)

    fig, axes = plt.subplots(1, 2, figsize=(11.0, 3.4))

    # ---- (a) the footprint: blob vs row, per board ----------------------
    ax = axes[0]
    for i, t in enumerate(TARGETS):
        d = data[t]
        ax.barh(i, d["row"], color="white", height=0.55,
                edgecolor=MUTED, linewidth=0.8)  # the row (ceiling)
        ax.barh(i, d["blob_bytes"], color=COLOR[t], alpha=0.75, height=0.55,
                edgecolor=COLOR[t])
        m = d["m"]
        ax.text(d["blob_bytes"] * 1.02, i,
                f"{m['frame_count']} frames @ {m['hours_per_frame']} h\n"
                f"{d['blob_bytes'] / 1e6:.2f} MB — {d['margin'] / 1e6:.1f} MB "
                f"margin",
                va="center", fontsize=7, color=INK)
    ax.set_yticks(range(len(TARGETS)),
                  [f"{NAME[t]} — {CSV[t]}" for t in TARGETS], fontsize=7)
    ax.invert_yaxis()
    ax.set_xlim(0, 24e6)
    ax.set_xticks([0, 5e6, 10e6, 15e6, 20e6])
    ax.set_xticklabels(["0", "5 MB", "10 MB", "15 MB", "20 MB"], fontsize=7)
    ax.set_xlabel("bytes (the row = the white bar, the blob = the filled "
                  "bar)")
    ax.set_title("the movie against its partition row —\n"
                 "the generator fills the row with the finest step that "
                 "fits")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the frames over the synodic cycle -------------------------
    ax = axes[1]
    for i, t in enumerate(TARGETS):
        m = data[t]["m"]
        n = m["frame_count"]
        hpf = m["hours_per_frame"]
        xs = [(k * hpf) / SYNODIC_H for k in range(n)]
        ax.hlines(len(TARGETS) - 1 - i, 0, 1, colors=HAIR, linewidth=6,
                  alpha=0.35)
        ax.vlines(xs, len(TARGETS) - 1 - i - 0.28, len(TARGETS) - 1 - i + 0.28,
                  colors=COLOR[t], linewidth=0.7, alpha=0.9)
        ax.text(-0.035, len(TARGETS) - 1 - i,
                f"{NAME[t]}: {n} × {hpf} h", ha="right", va="center",
                fontsize=7, color=INK)
        ax.text(1.015, len(TARGETS) - 1 - i,
                f"span {data[t]['span_h']:.0f} h", ha="left", va="center",
                fontsize=6.5, color=MUTED)
    ax.set_ylim(-0.6, len(TARGETS) - 0.4)
    ax.set_xlim(0, 1.28)
    ax.set_xticks([0, 0.25, 0.5, 0.75, 1.0])
    ax.set_xticklabels(["0 h", "~7 d", "~14.5 d", "~22 d", "709 h"],
                       fontsize=7)
    ax.set_yticks([])
    ax.set_xlabel("hours from the header's ref_center — one synodic "
                  "cycle (frame 0 ≈ frame n−1: the loop is seamless by "
                  "construction)")
    ax.set_title("the frames over the cycle — every tick is one baked, "
                 "flash-resident frame\n"
                 "(pixel-identical to a device bake; the density is what "
                 "adapts to the row, not the playback rate)")
    ax.grid(axis="x", color=HAIR, linewidth=0.5)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    for t in TARGETS:
        m = data[t]["m"]
        print(f"{t}: {m['frame_count']} frames @ {m['hours_per_frame']} h, "
              f"{data[t]['blob_bytes']} B of {data[t]['row']} B "
              f"({data[t]['margin']} B margin)")
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
