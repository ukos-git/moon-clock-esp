#!/usr/bin/env python3
"""Generate the computed roller-settings geometry figure for
docs/ui/index.rst — the companion of gen_menu_geom.py (wheel keyboard +
alarm-time band).

The six roller-based settings screens — settings-time, pomodoro, the
near-identical single-roller family (light-sleep timeout, deep-sleep
countdown, timezone, alarm sound), and the Wi-Fi scan list — drawn on
all three family panels from the code's OWN arithmetic, the way
gen_menu_geom.py does for the wheel: every box is ``UI_PX(x) = (x *
panel) // 466`` of the screens.c constant, every offset the code's own
``lv_obj_align(..., LV_ALIGN_CENTER, x, y)`` arguments (the sign sits
OUTSIDE UI_PX in the code, so the figure mirrors C's truncation toward
zero). The settings rollers are one visible row — the roller IS the
value display; the boxes below are the widget rectangles. The Wi-Fi
roller is the exception (three visible rows, four on the knob).

settings-time (create_screen_settings_time):
  time row: two 140x110 boxes (96 px numbers font) at x -75/+75, y -30,
  the `:` at 0;  date row: three 60x70 boxes (48 px font) at x -75/0/
  +75, y +70, the `.` separators at +-37;  caption y -110.
pomodoro (create_screen_settings_pomodoro):
  the twin-roller clock look: 140x110 at x -80/+70, y -23 (the home
  clock glyph centres), `:` at -10; caption "REPEAT INTERVAL" y -100.
single-roller family (settings_create_sleep_screen / settings_tz /
  settings_alarm_sound — ONE roller per screen, stacked here to compare):
  light/deep sleep 210x110 centred at 0 (captions y -125);
  timezone 380x110 centred at 0 (wide for "Europe/Berlin");
  alarm sound 260x110 at x -70 + the 100x100 preview button at +150;
  tz/sound captions y -110.
wifi (create_screen_settings_wifi — the family's ONE lv_roller):
  wifi glyph y -150, "NETWORKS" title (32 px ssid face) y -84;
  roller UI_PX(430) wide (knob 300) at (0, +18), NORMAL mode, entries
  CENTRED, grey / SELECTED white, no highlight box, pad+border 0 —
  so its height is lv_roller.c's (line_height + line_space) * row_cnt:
  3 rows x the tier 48-baseline SSID line-height 51/77/39 = 153 (S3),
  231 (P4); the knob's 4 rows x 39 = 156 (the taller list is why the
  user asked for the roller on 360 px);
  the roller and the centred SCANNING / NO APS FOUND placeholder label
  share the exact y +18 band and are never visible together (the
  placeholder owns the band while the roller is hidden — one-option
  NORMAL rows pin to the window top, which read as "not centred").
The bottom-centre exit button is the shared sub-screen furniture
(settings_sub_screen_prepare): the call passes UI_PX(90) but
settings_create_exit_button floors the box at UI_PX(96), anchored
BOTTOM_MID -34 -> band top edge y 336 (the x 185..281 / y 336..432
free band the screens.c comment reserves).

Palette follows gen_menu_geom.py / gen_hub_ring.py: blue #2b5fb0,
amber #b07d10, gray guides are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_roller_screens.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "roller_screens.png")

BLUE = "#2b5fb0"
AMBER = "#b07d10"
GRAY = "#8a8a8a"
LIGHT = "#c9c9c4"

PANELS = (("S3 — 466 px (UI_PX 1:1)", 466), ("P4 — 720 px", 720),
          ("knob — 360 px", 360))

# screens.c constants (466 space; every one goes through UI_PX below)
ROLL_W, ROLL_H = 140, 110        # the 96 px-numbers time rollers
DATE_W, DATE_H = 60, 70          # the 48 px-numbers date rollers
TIME_X = 75                      # settings-time rows: hour/minute/date x
TIME_ROW_Y, DATE_ROW_Y = -30, 70
DOT_X = 37                       # the `.` separators
POM_X, POM_PAIR_Y, POM_COLON_X = -80, -23, -10  # pomodoro/alarm twin pair
CAP_TIME_Y, CAP_POM_Y = -110, -100
CAP_SLEEP_Y, CAP_TZ_Y = -125, -110
SLEEP_W, TZ_W, SOUND_W = 210, 380, 260           # single-roller widths
SOUND_X, PREVIEW_X, PREVIEW_D = -70, 150, 100    # alarm-sound line
EXIT_SIZE, EXIT_BOTTOM = 96, 34    # UI_PX(90) call floored at UI_PX(96)
# wifi (create_screen_settings_wifi)
WIFI_GLYPH_Y, WIFI_TITLE_Y, WIFI_ROLL_Y = -150, -84, 18
WIFI_W, WIFI_W_KNOB = 430, 300
WIFI_ROWS, WIFI_ROWS_KNOB = 3, 4
# the tier 48-baseline SSID face's .line_height as generated in
# components/ui/fonts*/ui_font_montserrat_regular_48_ssid.c — the device
# truth lv_roller.c:241 multiplies by the row count
WIFI_LINE_H = {466: 51, 720: 77, 360: 39}


def uipx(x: int, panel: int) -> int:
    return (x * panel) // 466


def smp(v: int, panel: int) -> float:
    """Signed UI_PX: the code negates OUTSIDE UI_PX(-UI_PX(80)), so C
    truncates toward zero — mirror that, not Python's floor."""
    return math.copysign(uipx(abs(v), panel), v)


def panel_frame(ax, panel):
    th = [math.radians(a) for a in range(0, 361, 3)]
    r = panel / 2
    ax.plot([r * math.cos(t) for t in th], [r * math.sin(t) for t in th],
            "-", color=GRAY, linewidth=1.0, zorder=1)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_xlim(-r * 1.06, r * 1.06)
    ax.set_ylim(-r * 1.06, r * 1.06)


def box(ax, panel, cx, cy, w, h, color, alpha, label=None, lw=0.8,
        fontsize=6.5, outline=False):
    from matplotlib.colors import to_rgba
    R = lambda v: uipx(v, panel)
    x, y = smp(cx, panel) - R(w) / 2, -smp(cy, panel) - R(h) / 2
    # outline patches keep a faint fill (to_rgba carries the FACE alpha
    # only — a patch-level alpha would fade the edge with it)
    face = "none" if alpha == 0 else to_rgba(color, alpha)
    ax.add_patch(Rectangle((x, y), R(w), R(h), facecolor=face,
                           edgecolor=color, linewidth=lw, zorder=3))
    if label:
        ax.text(smp(cx, panel), -smp(cy, panel), label, fontsize=fontsize,
                color="#1e3f70", ha="center", va="center", zorder=5)


def caption(ax, panel, cy, text):
    ax.text(0, -smp(cy, panel), text, fontsize=6.0, color=GRAY,
            ha="center", va="center", zorder=5)


def sep(ax, panel, cx, cy, glyph, fontsize=10):
    ax.text(smp(cx, panel), -smp(cy, panel), glyph, fontsize=fontsize,
            color=BLUE, ha="center", va="center", zorder=5)


def exit_button(ax, panel, annotate):
    """The shared sub-screen furniture: the UI_PX(90) call floored to
    a 96 px box, BOTTOM_MID -34 — its top edge is the y 336 line the
    screens.c free-band comment cites."""
    R = lambda v: uipx(v, panel)
    half = panel / 2
    top_below = half - R(EXIT_SIZE + EXIT_BOTTOM)
    chord = 2 * math.sqrt(max(half * half - top_below * top_below, 0))
    ax.plot([-chord / 2, chord / 2], [-top_below, -top_below], "--",
            color=LIGHT, linewidth=1.0, zorder=2)
    cy = -(half - R(EXIT_BOTTOM) - R(EXIT_SIZE) / 2)
    ax.add_patch(Rectangle((-R(EXIT_SIZE) / 2, cy - R(EXIT_SIZE) / 2),
                           R(EXIT_SIZE), R(EXIT_SIZE), facecolor="none",
                           edgecolor=GRAY, linestyle="--", linewidth=0.9,
                           zorder=2))
    if annotate:
        ax.text(0, -top_below - R(14), "exit band top y 336", fontsize=5.2,
                color=GRAY, ha="center", va="center", zorder=5)


def draw_settings_time(ax, panel, annotate):
    caption(ax, panel, CAP_TIME_Y, "SET TIME / DATE")
    box(ax, panel, -TIME_X, TIME_ROW_Y, ROLL_W, ROLL_H, BLUE, 0.25, "HH")
    sep(ax, panel, 0, TIME_ROW_Y, ":")
    box(ax, panel, TIME_X, TIME_ROW_Y, ROLL_W, ROLL_H, BLUE, 0.25, "MM")
    box(ax, panel, -TIME_X, DATE_ROW_Y, DATE_W, DATE_H, BLUE, 0.15, "DD",
        fontsize=5.5)
    sep(ax, panel, -DOT_X, DATE_ROW_Y, ".", fontsize=8)
    box(ax, panel, 0, DATE_ROW_Y, DATE_W, DATE_H, BLUE, 0.15, "MM",
        fontsize=5.5)
    sep(ax, panel, DOT_X, DATE_ROW_Y, ".", fontsize=8)
    box(ax, panel, TIME_X, DATE_ROW_Y, DATE_W, DATE_H, BLUE, 0.15, "YY",
        fontsize=5.5)
    exit_button(ax, panel, False)
    if annotate:
        # legend block above the caption (the rows themselves are too
        # tightly stacked to carry annotations between them)
        ax.text(0, uipx(150, panel), "time row 140×110 @ ±75, −30",
                fontsize=5.2, color=BLUE, ha="center", va="center", zorder=5)
        ax.text(0, uipx(134, panel),
                "date row 60×70 @ ±75/0, +70 · dots ±37", fontsize=5.2,
                color=BLUE, ha="center", va="center", zorder=5)
        ax.text(-uipx(95, panel), -(panel / 2 - uipx(EXIT_BOTTOM + EXIT_SIZE / 2, panel)),
                "exit 96 @ −34", fontsize=5.2, color=GRAY, ha="right",
                va="center", zorder=5)


def draw_pomodoro(ax, panel, annotate):
    caption(ax, panel, CAP_POM_Y, "REPEAT INTERVAL")
    box(ax, panel, POM_X, POM_PAIR_Y, ROLL_W, ROLL_H, BLUE, 0.25, "HH")
    sep(ax, panel, POM_COLON_X, POM_PAIR_Y, ":")
    box(ax, panel, -POM_X - 10, POM_PAIR_Y, ROLL_W, ROLL_H, BLUE, 0.25, "MM")
    exit_button(ax, panel, False)
    if annotate:
        ax.text(0, -(uipx(-POM_PAIR_Y, panel) + uipx(64, panel)),
                "140×110 @ (−80/+70, −23) — the home clock's glyph centres",
                fontsize=5.2, color=BLUE, ha="center", va="center", zorder=5)


def draw_roller_family(ax, panel, annotate):
    """ONE roller per screen — all three true-centred as outlines to
    compare widths; each screen shows only its own box on the device."""
    caption(ax, panel, CAP_SLEEP_Y, "sleep: IDLE TIMEOUT / COUNTDOWN, y −125")
    caption(ax, panel, CAP_TZ_Y, "tz / sound captions, y −110")
    box(ax, panel, 0, 0, SLEEP_W, ROLL_H, BLUE, 0.0, lw=1.4, outline=True)
    box(ax, panel, 0, 0, TZ_W, ROLL_H, AMBER, 0.0, lw=1.4, outline=True)
    box(ax, panel, SOUND_X, 0, SOUND_W, ROLL_H, GRAY, 0.0, lw=1.4,
        outline=True)
    box(ax, panel, PREVIEW_X, 0, PREVIEW_D, PREVIEW_D, AMBER, 0.12,
        "speaker", fontsize=5.5, lw=1.0, outline=True)
    exit_button(ax, panel, False)
    if annotate:
        # legend block above the circle-top caption rows: colour = box ink
        ax.text(0, uipx(168, panel),
                "single rollers 110 tall, ONE per screen", fontsize=5.4,
                color="#3c3c3c", ha="center", va="center", zorder=5)
        ax.text(0, uipx(150, panel),
                "sleep 210 @ 0   ·   timezone 380 @ 0", fontsize=5.2,
                color=BLUE, ha="center", va="center", zorder=5)
        ax.text(0, uipx(134, panel),
                "sound 260 @ −70  ·  preview 100 @ +150", fontsize=5.2,
                color=AMBER, ha="center", va="center", zorder=5)


def draw_wifi(ax, panel, annotate):
    """The scan list: ONE NORMAL-mode roller with the placeholder label
    sharing its band — exactly one of the two is visible (the
    placeholder owns the band while the roller is hidden)."""
    from matplotlib.colors import to_rgba
    from matplotlib.patches import Arc
    R = lambda v: uipx(v, panel)
    rows = WIFI_ROWS_KNOB if panel == 360 else WIFI_ROWS
    w = WIFI_W_KNOB if panel == 360 else WIFI_W
    lh = WIFI_LINE_H[panel]
    cy = -smp(WIFI_ROLL_Y, panel)
    h = rows * lh
    # wifi glyph (solid face) at y -150: dot + three opening arcs
    gy = -smp(WIFI_GLYPH_Y, panel)
    ax.plot([0], [gy - R(16)], marker="o", markersize=max(1.6, R(4) / 12),
            color=GRAY, zorder=4)
    for k in (1, 2, 3):
        ax.add_patch(Arc((0, gy - R(16)), R(16 * k), R(13 * k),
                         theta1=35, theta2=145, color=GRAY,
                         linewidth=1.0, zorder=4))
    ax.text(0, -smp(WIFI_TITLE_Y, panel), "NETWORKS", fontsize=6.5,
            color=GRAY, ha="center", va="center", zorder=5)
    # the roller body + entry hairlines (rows stacked at line-height)
    x, y = -R(w) / 2, cy - h / 2
    ax.add_patch(Rectangle((x, y), R(w), h, facecolor=to_rgba(BLUE, 0.10),
                           edgecolor=BLUE, linewidth=0.9, zorder=3))
    for k in range(1, rows):
        yy = y + k * lh
        ax.plot([x, x + R(w)], [yy, yy], "-", color=LIGHT, linewidth=0.7,
                zorder=4)
    # window centre = cursor: the SELECTED entry goes white, no box
    ax.add_patch(Rectangle((x, cy - lh / 2), R(w), lh, facecolor="none",
                           edgecolor=AMBER, linewidth=1.1, zorder=4))
    # the other state of the same band: centred placeholder label
    ax.add_patch(Rectangle((-R(250) / 2, cy - lh / 2 - R(6)), R(250),
                           lh + R(12), facecolor="none", edgecolor=GRAY,
                           linestyle=(0, (4, 3)), linewidth=0.9, zorder=5))
    ax.text(0, cy, "SCANNING", fontsize=6.2, color=GRAY, ha="center",
            va="center", zorder=6)
    exit_button(ax, panel, False)
    if annotate:
        # legend block above the glyph's top arc (S3 column only)
        ly = uipx(186, panel)
        for i, (txt, col) in enumerate((
                ("roller 430 (knob 300) @ (0, +18) · rows 3 (knob 4)"
                 " · h = rows × 51/77/39", BLUE),
                ("centred: 14 chars + '+' uppercased · ' −54DBM'"
                 " · cursor SELECTED white", AMBER),
                ("dashed SCANNING/NO APS = roller hidden, same band (y +18)",
                 GRAY))):
            ax.text(0, ly + i * R(16), txt, fontsize=4.8, color=col,
                    ha="center", va="center", zorder=5)


def main() -> None:
    fig, axes = plt.subplots(4, 3, figsize=(11.0, 14.7))
    rows = ((draw_settings_time, "settings-time"),
            (draw_pomodoro, "pomodoro"),
            (draw_roller_family, "single-roller family"),
            (draw_wifi, "settings-wifi"))
    for row, (fn, title) in enumerate(rows):
        for col, (panel, size) in enumerate(PANELS):
            ax = axes[row][col]
            panel_frame(ax, size)
            fn(ax, size, annotate=(col == 0))
            ax.set_title(f"{panel}\n{title} — boxes = UI_PX(code)",
                         fontsize=8, color="#3c3c3c", pad=5)
    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
