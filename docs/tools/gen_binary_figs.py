#!/usr/bin/env python3
"""Generate the flash-budget figure for docs/verification/binary.rst.

The page is the project's flash budget log; until now it was the only
numbers page without a figure. Two panels:

(a) the three current binaries against their ``factory`` ceilings —
    single-sourced FROM the page: the numbers are parsed out of the
    "current binaries" table itself, so figure and table cannot drift
    (and docs/tools/check_docs_drift.py verifies the page against the
    binaries on disk — the chain figure <- page <- disk is closed).
(b) the S3 ceiling chronology — the app outgrowing the original
    1,425,408 B ceiling, the widening to 2048 K, and the growth under
    it. The steps mirror the chronology code block on the page.

Palette reuses the perf figure identities (S3 blue, P4 vermillion,
knob violet; gray = ceiling geometry, not an entity).

Run:  docs/.venv/bin/python docs/tools/gen_binary_figs.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(HERE, "..")
OUT = os.path.join(DOCS, "_static", "figs", "binary_budget.png")
PAGE = os.path.join(DOCS, "verification", "binary.rst")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"      # S3 identity
VERM = "#c2410c"      # P4 identity
VIOLET = "#7c3aed"    # knob identity

plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})

# The chronology block on the page (kept in sync with it by review; the
# current bar in (a) is PARSED from the page, never duplicated here).
CHRONO = [
    ("production build\n(pre-widening)", 1_451_472, "outgrew the ceiling\nby ~26 KB -> widened"),
    ("wheel v2",                          1_462_320, ""),
    ("2026-11-02",                        1_462_560, ""),
    ("2026-11-04\nM4-era",                1_511_280, ""),
    ("2026-11-15\nfamily build",          1_706_960, ""),
]
CEIL_OLD, CEIL_NEW = 1_425_408, 2_097_152


def parse_current() -> list[tuple[str, int, int, str]]:
    """The page's 'current binaries' table: (board, size, ceiling, file)."""
    text = open(PAGE).read()
    rows = re.findall(
        r"``([\w/._-]+\.bin)``\s*\n\s*- ([\d,]+) B \(0x[\da-f]+\)\s*\n\s*- ([\d,]+) B",
        text)
    if len(rows) != 3:
        raise SystemExit(f"expected 3 current-binary rows on {PAGE}, found "
                         f"{len(rows)} — update the parser with the table")
    board_of = {"build/moon-phase.bin": ("S3 466", BLUE),
                "build/p4/moon-phase.bin": ("P4 720", VERM),
                "build/knob/moon-phase.bin": ("knob 360", VIOLET)}
    out = []
    for f, size, ceil in rows:
        name, color = board_of[f]
        out.append((name, int(size.replace(",", "")),
                    int(ceil.replace(",", "")), color))
    return out


def main() -> None:
    current = parse_current()
    fig, axes = plt.subplots(1, 2, figsize=(11.0, 3.7))

    # ---- (a) binaries vs factory ceilings ------------------------------
    ax = axes[0]
    ys = range(len(current))
    for y, (name, size, ceil, color) in enumerate(current):
        ax.barh(y, ceil, color=HAIR, height=0.55, zorder=1)
        ax.barh(y, size, color=color, height=0.55, zorder=2)
        pct = 100.0 * size / ceil
        # one right-aligned label in the far white band — never clipped
        # at the axis edge, never dark-on-blue or white-on-blue
        ax.text(9_900_000 * 0.995, y, f"{size:,} / {ceil:,} B — {pct:.0f} % "
                f"of ceiling", va="center", ha="right", fontsize=6.8,
                color=INK, zorder=3)
    ax.set_yticks(list(ys), [c[0] for c in current])
    ax.invert_yaxis()
    ax.set_xlim(0, 9_900_000)
    ax.set_xlabel("bytes (linear)")
    ax.set_xticks([0, 2_097_152, 4_194_304, 9_437_184])
    ax.set_xticklabels(["0", "2048 K", "4096 K", "9216 K"])
    ax.set_title("current binaries vs the factory ceilings\n"
                 "(gray = the partition, filled = the app)")
    ax.grid(axis="x", color="white", linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the S3 chronology under the ceiling ------------------------
    ax = axes[1]
    xs = range(len(CHRONO))
    vals = [c[1] for c in CHRONO]
    ax.bar(list(xs), vals, color=BLUE, width=0.58, zorder=2)
    # explicit data coords (not axhline fractions) so the widened left
    # margin does not shift where the ceiling lines start/stop
    # the old ceiling spans bar 0 only (stopped just before bar 1, which
    # was built under the new one); the new ceiling spans bars 1..4
    ax.plot([-0.32, 0.66], [CEIL_OLD, CEIL_OLD],
            color=MUTED, linestyle="--", linewidth=1.1, zorder=3)
    ax.plot([0.38, 4.55], [CEIL_NEW, CEIL_NEW],
            color=MUTED, linestyle="-", linewidth=1.1, zorder=3)
    ax.text(len(CHRONO) - 0.4, CEIL_NEW + 28_000, "widened factory: 2048 K",
            fontsize=6.8, color=MUTED, ha="right")
    # the old-ceiling label sits in a widened left margin, clear of the
    # bar tops and the value labels it used to collide with
    ax.set_xlim(-1.7, 4.6)
    ax.text(-0.34, CEIL_OLD - 40_000, "original\n1392 KiB ceiling",
            fontsize=6.8, color=MUTED, ha="right", va="top")
    for x, (label, val, note) in enumerate(CHRONO):
        ax.text(x, val + 24_000, f"{val:,}", ha="center", fontsize=6.4,
                color=INK)
        if note:
            ax.annotate(note + f"\n({val:,})", xy=(x, val + 120_000),
                        xytext=(x + 0.15, 1_880_000), fontsize=6.4,
                        color=VERM, ha="left",
                        arrowprops=dict(arrowstyle="->", color=VERM,
                                        linewidth=0.7))
    ax.set_xticks(list(xs), [c[0] for c in CHRONO], fontsize=6.8)
    ax.set_ylim(0, 2_300_000)
    ax.set_ylabel("S3 app binary (bytes)")
    ax.set_title("the S3 chronology: hit the old ceiling, widened\n"
                 "once, grew to 81 % of the new one")
    ax.grid(axis="y", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, DOCS)}")


if __name__ == "__main__":
    main()
