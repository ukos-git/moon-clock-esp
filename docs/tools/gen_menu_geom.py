#!/usr/bin/env python3
"""Generate the computed menu-screen geometry figure for docs/ui/index.rst.

The two settings-family screens with real geometry — the wheel keyboard
(``create_screen_settings_wifi_password``) and the alarm-time day band
(``create_screen_settings_alarm_time``) — drawn on all three family
panels from the code's OWN arithmetic, the way gen_hub_ring.py does for
the hub: every radius is ``UI_PX(x) = (x * panel) // 466`` of the
screens.c constant, every angle the code's own angle expression.

Wheel keyboard (screens.c WHEEL_* defines + the design comment):
  hit band r 140..panel/2 · at-rest letters r 168 · reveal letters r 209
  sector hairlines r 150..226 (12 groups, cumulative of
  [ab][cd][ef][gh][il][jkq][mn][op][rs][tu][vxz][wy] x 360/26)
  register track LVGL 150..390 at r 84..144 (midline 114) · commit
  LVGL ~31.5..88.5 + discard ~91.5..148.5 at r 114 · centre box r ~55.
  LVGL angles (0 = right, 90 = down, clockwise) plot as
  (r cos a, -r sin a) in the y-up figure.

Alarm-time band (screens.c ALARM_TIME_* defines):
  object 2(R+W) -> outer R = UI_PX(223), width UI_PX(10) (inner 213) —
  the home seconds-arc ring · day handles at r UI_PX(218) on
  ALARM_DAY_ANG(d) = 181 + 30 d (d = Mon..Sun) · the dark track and the
  re-seeded indicator run 181..1 (the 360/0 wrap).

Palette follows gen_perf_figs.py / gen_hub_ring.py: blue #2b5fb0,
amber #b07d10, gray guides are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_menu_geom.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Wedge

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "menu_geom.png")

BLUE = "#2b5fb0"
AMBER = "#b07d10"
GRAY = "#8a8a8a"
LIGHT = "#c9c9c4"

PANELS = (("S3 — 466 px (UI_PX 1:1)", 466), ("P4 — 720 px", 720),
          ("knob — 360 px", 360))

# screens.c constants (466 space; every one goes through UI_PX below)
WHEEL_LETTERS_R, WHEEL_REVEAL_R = 168, 209
WHEEL_HIT_IN, WHEEL_BAND_LO, WHEEL_BAND_HI = 140, 150, 226
WHEEL_MODE_LO, WHEEL_MODE_HI = 84, 144
WHEEL_MODE_MID, WHEEL_CENTRE_R = 114, 55
SECTORS = [2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 3, 2]  # [ab][cd][ef]...[wy], sum 26
SHARE = 360.0 / 26
ALARM_R_OUT, ALARM_W, ALARM_HANDLE_R = 223, 10, 218


def uipx(x: int, panel: int) -> int:
    return (x * panel) // 466


def pt(r: float, lvgl_deg: float) -> tuple[float, float]:
    """LVGL angle (0 right, 90 down, clockwise) -> y-up plot point."""
    a = math.radians(lvgl_deg)
    return r * math.cos(a), -r * math.sin(a)


def arc(ax, r, w, a0, a1, color, alpha=1.0, lw=None):
    """Thick arc (filled annular wedge when lw=None, else an outline)."""
    # matplotlib Wedge angles are CCW from +x; LVGL CW == -angle
    ax.add_patch(Wedge((0, 0), r + w / 2, -a1, -a0, width=w,
                       facecolor=color, edgecolor="none", alpha=alpha,
                       zorder=3))


def panel_frame(ax, panel):
    th = [math.radians(a) for a in range(0, 361, 3)]
    r = panel / 2
    ax.plot([r * math.cos(t) for t in th], [r * math.sin(t) for t in th],
            "-", color=GRAY, linewidth=1.0, zorder=1)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_xlim(-r * 1.06, r * 1.06)
    ax.set_ylim(-r * 1.06, r * 1.06)


def draw_wheel(ax, panel, annotate):
    R = lambda v: uipx(v, panel)
    hit_out = panel / 2
    # forgiving press band 140..panel/2
    ax.add_patch(Wedge((0, 0), hit_out, 0, 360, width=hit_out - R(WHEEL_HIT_IN),
                       facecolor=BLUE, alpha=0.07, edgecolor="none", zorder=0))
    # band hairlines at 150 / 226 + sector hairlines between them
    for v in (WHEEL_BAND_LO, WHEEL_BAND_HI):
        th = [math.radians(a) for a in range(0, 361, 3)]
        ax.plot([R(v) * math.cos(t) for t in th], [R(v) * math.sin(t) for t in th],
                "--", color=LIGHT, linewidth=0.8, zorder=1)
    s0 = 0
    for n in SECTORS:
        a = s0 * SHARE - 90.0
        x0, y0 = pt(R(WHEEL_BAND_LO), a)
        x1, y1 = pt(R(WHEEL_BAND_HI), a)
        ax.plot([x0, x1], [y0, y1], "-", color=GRAY, linewidth=0.6, zorder=2)
        s0 += n
    # at-rest letters (r 168) as ticks; reveal row (r 209) as a dotted ring
    for s in range(26):
        a = (s + 0.5) * SHARE - 90.0
        x0, y0 = pt(R(WHEEL_LETTERS_R) - R(8), a)
        x1, y1 = pt(R(WHEEL_LETTERS_R) + R(8), a)
        ax.plot([x0, x1], [y0, y1], "-", color=BLUE, linewidth=1.3, zorder=3)
    th = [math.radians(a) for a in range(0, 361, 3)]
    ax.plot([R(WHEEL_REVEAL_R) * math.cos(t) for t in th],
            [R(WHEEL_REVEAL_R) * math.sin(t) for t in th],
            ":", color=BLUE, linewidth=0.7, alpha=0.7, zorder=2)
    # register track (LVGL 150..390) + the four key boundaries
    arc(ax, R(WHEEL_MODE_MID), R(WHEEL_MODE_HI) - R(WHEEL_MODE_LO), 150, 390,
        BLUE, alpha=0.25)
    for b in (150, 210, 270, 330):
        x0, y0 = pt(R(WHEEL_MODE_LO), b)
        x1, y1 = pt(R(WHEEL_MODE_HI), b)
        ax.plot([x0, x1], [y0, y1], "-", color=GRAY, linewidth=0.6, zorder=4)
    # bottom 120 degrees: commit (house circle-check) + discard (circle-xmark)
    arc(ax, R(WHEEL_MODE_MID), R(WHEEL_MODE_HI) - R(WHEEL_MODE_LO),
        31.5, 88.5, AMBER, alpha=0.85)
    arc(ax, R(WHEEL_MODE_MID), R(WHEEL_MODE_HI) - R(WHEEL_MODE_LO),
        91.5, 148.5, AMBER, alpha=0.45)
    # centre tap box
    ax.add_patch(Wedge((0, 0), R(WHEEL_CENTRE_R), 0, 360,
                       facecolor=GRAY, alpha=0.35, edgecolor="none", zorder=3))
    if annotate:
        for r, label, a in ((panel / 2, "233", 250), (226, "226", 285),
                            (209, "209", 300), (168, "168", 315),
                            (150, "150", 330), (140, "140", 345)):
            x, y = pt(uipx(r, panel) if r != panel / 2 else panel / 2, a)
            ax.text(x, y, label, fontsize=5.6, color=BLUE,
                    ha="left", va="center", zorder=5)
        for r, label, a in ((114, "114 mid", 60), (84, "84", 45),
                            (55, "55", 120)):
            x, y = pt(uipx(r, panel), a)
            ax.text(x, y, label, fontsize=5.6, color=AMBER,
                    ha="left", va="center", zorder=5)


def draw_alarm_band(ax, panel, annotate):
    R = lambda v: uipx(v, panel)
    r_out, w = R(ALARM_R_OUT), max(R(ALARM_W), 2)
    # the dark full track 181..1 (the 360/0 wrap) and the indicator sample
    arc(ax, r_out - w / 2, w, 181, 361, LIGHT, alpha=0.9)
    arc(ax, r_out - w / 2, w, 181, 301, BLUE, alpha=0.85)  # Mon..Fri sample
    # the seven day handles at ALARM_DAY_ANG(d) = 181 + 30 d
    days = "MTWTFSS"
    for d in range(7):
        a = 181 + 30 * d
        x, y = pt(R(ALARM_HANDLE_R), a)
        big = d in (0, 4)  # the default working endpoints (start, end)
        ax.scatter([x], [y], s=52 if big else 26, zorder=4,
                   facecolor=AMBER if big else BLUE,
                   edgecolor="#1e3f70" if not big else "#6b4e08",
                   linewidth=0.6)
        lx, ly = pt(R(ALARM_HANDLE_R) + R(26), a)
        ax.text(lx, ly, days[d], fontsize=6.0, color=BLUE if not big else "#6b4e08",
                ha="center", va="center", zorder=5)
    # the two rollers' labels (time HH:MM above the date row — the layout)
    ax.text(0, R(30), "HH:MM", ha="center", va="center", fontsize=9,
            color=BLUE, zorder=5)
    ax.text(0, -R(20), "DD.MM.YY", ha="center", va="center", fontsize=6,
            color=BLUE, zorder=5)
    if annotate:
        for r, label, a in ((ALARM_R_OUT, "223 outer", 236),
                            (ALARM_HANDLE_R, "218 handles", 254),
                            (ALARM_R_OUT - ALARM_W, "w = 10", 128)):
            x, y = pt(R(r), a)
            ax.text(x, y, label, fontsize=5.6, color=GRAY, ha="center",
                    va="center", zorder=5)


def main() -> None:
    fig, axes = plt.subplots(2, 3, figsize=(11.0, 7.4))
    for col, (panel, size) in enumerate(PANELS):
        for row, (fn, title) in enumerate((
                (draw_wheel, "wheel keyboard"),
                (draw_alarm_band, "alarm-time band"))):
            ax = axes[row][col]
            panel_frame(ax, size)
            fn(ax, size, annotate=(col == 0))
            ax.set_title(f"{panel}\n{title} — radii = UI_PX(code)",
                         fontsize=8, color="#3c3c3c", pad=5)
    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
