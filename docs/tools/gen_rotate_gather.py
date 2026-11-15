#!/usr/bin/env python3
"""Generate the rotation-gather + quarter-table figure for
docs/implementation/rotate-lut.rst.

Panel A draws the gather exactly as ``moon_rotate_range``
(components/moon/core/moon_render.c) works: the function stands at a
DESTINATION pixel and asks which SOURCE pixel it wants — the inverse
map p = R(−theta)·(dx, dy) — nearest-neighbour with round-half-up,
one byte pulled, fused through the gray->RGB565 dictionary. The
4-px unroll's increment (fx += c15; fy -= s15) is drawn as the row
step. Panel B draws the lazily built quarter table
(components/moon/core/moon_fixp.c): NQ = 1024 entries covering
[0, pi/2] (1025 int32 = 8 KB), the other three quadrants folded in
by mirror + sign flip, Q15.15 scale (cos 0 = 32768).

Palette follows gen_perf_figs.py: blue #2b5fb0, amber #b07d10,
grays are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_rotate_gather.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Polygon

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "rotate_gather.png")

BLUE = "#2b5fb0"
BLUE_DARK = "#1e3f70"
AMBER = "#b07d10"
GRAY = "#8a8a8a"
LIGHT = "#c9c9c4"

THETA_DEG = 25.0  # the turn the picture shows


def rot(x: float, y: float, deg: float) -> tuple[float, float]:
    """Rotate by +deg (plot y-up, CCW positive)."""
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return c * x - s * y, s * x + c * y


def main() -> None:
    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(10.5, 4.4))

    # ---------------- Panel A: the inverse gather ----------------
    S = 100.0  # frame half-size in plot units (the 400 px square)

    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_xlim(-160, 160)
    ax.set_ylim(-135, 135)

    # the source frame: rotated square + disk + two maria so the
    # turn is visible (body-frame features, like the baked frame)
    corners = [rot(sx, sy, -THETA_DEG) for sx, sy in
               ((-S, -S), (S, -S), (S, S), (-S, S))]
    ax.add_patch(Polygon(corners, closed=True, facecolor="#f2f2ef",
                         edgecolor=GRAY, linewidth=1.0, linestyle="--",
                         zorder=1))
    ax.add_patch(Circle((0, 0), 0.86 * S, facecolor="#e8e8e4",
                        edgecolor=GRAY, linewidth=0.9, zorder=2))
    for (bx, by), r in (((-0.32, 0.30), 0.16), ((0.28, -0.18), 0.22)):
        px, py = rot(bx * S, by * S, -THETA_DEG)
        ax.add_patch(Circle((px, py), r * S, facecolor="#d8d8d2",
                           edgecolor="none", zorder=3))
    ax.text(*rot(-0.72 * S, S + 9, -THETA_DEG), "baked source frame\n(gray, one byte/px)",
            ha="center", va="bottom", fontsize=7.5, color=GRAY, zorder=6)

    # the destination frame: axis-aligned square in front
    ax.add_patch(Polygon([(-S, -S), (S, -S), (S, S), (-S, S)], closed=True,
                         facecolor="none", edgecolor=BLUE, linewidth=1.4,
                         zorder=4))
    ax.text(0, S + 8, "destination frame (RGB565, on the panel)",
            ha="center", va="bottom", fontsize=7.5, color=BLUE_DARK, zorder=6)

    # three destination pixels pulling their source byte: p = R(-th) s
    # (the destination asks the source — the inverse map)
    for (dx, dy) in ((38, -26), (-52, 34), (14, 52)):
        sx, sy = rot(dx, dy, -THETA_DEG)  # R(-theta)
        ax.annotate(
            "", xy=(sx, sy), xytext=(dx, dy),
            arrowprops=dict(arrowstyle="-|>", color=AMBER, linewidth=1.3,
                            shrinkA=3, shrinkB=3), zorder=7)
        ax.scatter([dx], [dy], s=26, facecolor=BLUE, edgecolor=BLUE_DARK,
                   linewidth=0.6, zorder=8)
        ax.scatter([sx], [sy], s=26, facecolor=AMBER, edgecolor="#6b4e08",
                   linewidth=0.6, zorder=8)

    # the 4-px unroll: one group of four destination pixels on a row
    row_y = -62.0
    for k in range(4):
        x0 = 46 + k * 11
        ax.scatter([x0], [row_y], s=16, facecolor="white", edgecolor=BLUE,
                   linewidth=0.9, zorder=8)
    ax.annotate("fx += c15\nfy −= s15", xy=(46 + 1.5 * 11, row_y - 4),
                xytext=(84, row_y - 44), fontsize=7, color=AMBER,
                ha="left", arrowprops=dict(arrowstyle="-", color=LIGHT),
                zorder=7)

    ax.text(0, -128, "inverse mapping: the destination pixel names the byte it pulls —\n"
                     "nearest neighbour (floor + half-pixel step), out-of-frame reads black",
            ha="center", va="bottom", fontsize=7.5, color="#3c3c3c")

    # ---------------- Panel B: the quarter table ----------------
    ax2.set_aspect("equal")
    ax2.axis("off")
    ax2.set_xlim(-1.45, 1.65)
    ax2.set_ylim(-1.35, 1.35)

    # the quarter that is stored: [0, pi/2], dense tick marks = 1024 entries
    th0 = [math.radians(a) for a in range(0, 91, 2)]
    ax2.plot([math.cos(t) for t in th0], [math.sin(t) for t in th0],
             "-", color=BLUE, linewidth=3.0, zorder=3)
    for a in range(0, 91, 10):
        t = math.radians(a)
        ax2.scatter([math.cos(t)], [math.sin(t)], s=8, color=BLUE_DARK,
                    zorder=4)
    # the folded quadrants: mirrors, drawn faint
    for a0, a1 in ((90, 180), (180, 270), (270, 360)):
        th = [math.radians(a) for a in range(a0, a1 + 1, 3)]
        ax2.plot([math.cos(t) for t in th], [math.sin(t) for t in th],
                 "-", color=LIGHT, linewidth=2.2, zorder=2)
    # fold arrows: mirror the quarter into the other quadrants
    for (tx, ty) in ((0.0, 1.16), (-1.16, 0.0), (-1.12, -1.12)):
        ax2.annotate("", xy=(tx * 0.78, ty * 0.78), xytext=(tx, ty),
                     arrowprops=dict(arrowstyle="-|>", color=AMBER,
                                     linewidth=1.1, shrinkA=2, shrinkB=2),
                     zorder=5)
    ax2.plot([-1, 1], [0, 0], "-", color=LIGHT, linewidth=0.8, zorder=1)
    ax2.plot([0, 0], [-1, 1], "-", color=LIGHT, linewidth=0.8, zorder=1)
    ax2.scatter([1], [0], s=26, facecolor=AMBER, edgecolor="#6b4e08",
                linewidth=0.6, zorder=6)
    ax2.text(1.06, -0.10, "cos 0 → 32768\n(Q15.15)", fontsize=7.5,
             color="#6b4e08", zorder=6)
    ax2.text(0.62, 0.86, "stored quarter\nNQ = 1024 · 8 KB", fontsize=7.5,
             color=BLUE_DARK, ha="center", zorder=6)
    ax2.text(-0.98, 0.62, "walked\nbackwards", fontsize=6.8, color=AMBER,
             ha="center", zorder=6)
    ax2.text(-0.72, -0.72, "mirror +\nsign flip", fontsize=6.8, color=AMBER,
             ha="center", zorder=6)
    ax2.text(0.5, -0.82, "sign flip", fontsize=6.8, color=AMBER,
             ha="center", zorder=6)
    ax2.text(0, -1.30, "one quarter turn precomputed, the circle folded into it —\n"
                       "linear interp ≈ 18-bit angle, far under one pixel of turn",
             ha="center", va="bottom", fontsize=7.5, color="#3c3c3c")

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
