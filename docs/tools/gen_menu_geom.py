#!/usr/bin/env python3
"""Generate the computed menu-screen geometry figure for docs/ui/index.rst.

The three settings-family screens with real geometry — the wheel
keyboard (``create_screen_settings_wifi_password``), the alarm-time day
band (``create_screen_settings_alarm_time``) and the alarm-rules
triangle (``create_screen_settings_alarm``) — drawn on all three family
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

Alarm-time screen (screens.c ALARM_TIME_* defines + the screen builder):
  a PAD-0 466x466 arc — get_center's r = panel/2 is the stroke's OUTER
  edge and the UI_PX(10) stroke is inked INWARD from it (band
  224..233 at 466, TOUCHING the round rim — probed rows 0..9 of the
  committed settings-alarm-time screenshots; NOT the home seconds
  arc's pad-inset outer-223 ring, though the same thin-grey visual
  language) · day handles (34 px circles) at r UI_PX(218) — centred
  10 px under the band midline, so the disc pokes through both ink
  edges — on ALARM_DAY_ANG(d) = 181 + 30 d (d = Mon..Sun) · the dark
  track and the re-seeded indicator run 181..1 (the 360/0 wrap) · the
  two time rollers (140x110 boxes, the 96 px numbers font) centred at
  x −80/+70, y −23 — the home clock's glyph centres — with the `:` at
  x −10 · the "Mo-Fr" range summary at y −100 (NO date row on this
  screen: the DD.MM.YY row lives on the settings-time screen, see
  gen_roller_screens.py) · the bottom-centre exit button.

Alarm-rules screen (create_screen_settings_alarm): the master
deactivate-all alarm glyph (regular FA, 96 px tier) centred at
(0, −UI_PX(30)); the two mutually-exclusive entries on the diagonals
at (±UI_PX(130), +UI_PX(130)) — left stopwatch (solid FA) opens the
pomodoro screen, right alarm-clock (regular FA) opens the alarm-time
screen; glyphs are LV_SIZE_CONTENT labels, the ink is the tier's
96/148/74 icon face (fonts{,_p4,_knob}, as in gen_hub_ring.py).

Exit button (all three screens, settings_sub_screen_prepare): the call
passes ``UI_PX(90)`` but ``settings_create_exit_button`` FLOORS the box
at ``UI_PX(96)`` (below 96 would clip the glyph disk), so the drawn
tap box is the 96 px tier box anchored BOTTOM_MID −34 — free band
x 185..281, y 336..432 exactly as the screens.c comment states.

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

PANELS = (("S3 — 466 px (UI_PX 1:1)", 466, 96), ("P4 — 720 px", 720, 148),
          ("knob — 360 px", 360, 74))  # third entry: the tier's FA icon face

# screens.c constants (466 space; every one goes through UI_PX below)
WHEEL_LETTERS_R, WHEEL_REVEAL_R = 168, 209
WHEEL_HIT_IN, WHEEL_BAND_LO, WHEEL_BAND_HI = 140, 150, 226
WHEEL_MODE_LO, WHEEL_MODE_HI = 84, 144
WHEEL_MODE_MID, WHEEL_CENTRE_R = 114, 55
SECTORS = [2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 3, 2]  # [ab][cd][ef]...[wy], sum 26
SHARE = 360.0 / 26
ALARM_W, ALARM_HANDLE_R = 10, 218  # band width (UI_PX); handle centre radius
ALARM_HANDLE_D = 34      # the day-handle circle's diameter (UI_PX(34))
ALARM_ROLLER_W, ALARM_ROLLER_H = 140, 110  # the time rollers (96 px font)
ALARM_HOUR_X, ALARM_MIN_X = -80, 70        # rollers' centres (home glyph centres)
ALARM_PAIR_Y, ALARM_COLON_X = -23, -10     # pair row + the `:` x
ALARM_RANGE_Y = -100     # the "Mo-Fr" summary row
EXIT_SIZE, EXIT_BOTTOM = 96, 34            # the call passes UI_PX(90) but the
                                           # builder floors the box at UI_PX(96)
RULES_MASTER_Y, RULES_R = 30, 130          # create_screen_settings_alarm offsets


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


def smp(v, panel):
    """Signed UI_PX: the code negates OUTSIDE UI_PX(-UI_PX(80)), so C
    truncates toward zero — mirror that, not Python's floor."""
    return math.copysign(uipx(abs(v), panel), v)


def roller_box(ax, panel, cx, cy, w, h, color, alpha, label=None):
    """A roller/tap box: centred at code-space (cx, cy) — code y is
    DOWNWARD from the panel centre, so it plots as -cy (y-up)."""
    from matplotlib.patches import Rectangle
    R = lambda v: uipx(v, panel)
    x, y = smp(cx, panel) - R(w) / 2, -smp(cy, panel) - R(h) / 2
    ax.add_patch(Rectangle((x, y), R(w), R(h), facecolor=color, alpha=alpha,
                           edgecolor=color, linewidth=0.8, zorder=3))
    if label:
        ax.text(smp(cx, panel), -smp(cy, panel), label, fontsize=6.5,
                color="#1e3f70", ha="center", va="center", zorder=5)


def exit_band(ax, panel):
    """The bottom-centre exit button: the UI_PX(90) call floored to a
    96 px box, anchored BOTTOM_MID at -34 (its top edge is the y 336
    line the screens.c free-band comment cites, x 185..281 y 336..432)."""
    from matplotlib.patches import Rectangle
    R = lambda v: uipx(v, panel)
    half = panel / 2
    top_below = half - R(EXIT_SIZE + EXIT_BOTTOM)  # y 336 below centre at 466
    chord = 2 * math.sqrt(max(half * half - top_below * top_below, 0))
    ax.plot([-chord / 2, chord / 2], [-top_below, -top_below], "--",
            color=LIGHT, linewidth=1.0, zorder=2)
    cy = -(half - R(EXIT_BOTTOM) - R(EXIT_SIZE) / 2)
    ax.add_patch(Rectangle((-R(EXIT_SIZE) / 2, cy - R(EXIT_SIZE) / 2),
                           R(EXIT_SIZE), R(EXIT_SIZE), facecolor="none",
                           edgecolor=GRAY, linestyle="--", linewidth=0.9,
                           zorder=2))


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
    # pad-0 full-panel arc: get_center's r = panel/2 is the OUTER ink
    # edge and the UI_PX(10) stroke is inked INWARD from it — the band
    # reaches the rim (unlike the home seconds arc, pad UI_PX(10),
    # outer 223)
    w = max(R(ALARM_W), 2)
    r_out = panel / 2
    # the dark full track 181..1 (the 360/0 wrap) and the indicator sample
    arc(ax, r_out - w / 2, w, 181, 361, LIGHT, alpha=0.9)
    arc(ax, r_out - w / 2, w, 181, 301, BLUE, alpha=0.85)  # Mon..Fri sample
    # the exit band first (behind everything): bottom-centre 96 px button
    exit_band(ax, panel)
    # the two time rollers (140x110, 96 px font) at the home clock's
    # glyph centres + the `:` between them (screens.c settings_create_roller)
    roller_box(ax, panel, ALARM_HOUR_X, ALARM_PAIR_Y, ALARM_ROLLER_W,
               ALARM_ROLLER_H, BLUE, 0.25, "HH")
    roller_box(ax, panel, ALARM_MIN_X, ALARM_PAIR_Y, ALARM_ROLLER_W,
               ALARM_ROLLER_H, BLUE, 0.25, "MM")
    ax.text(smp(ALARM_COLON_X, panel), -smp(ALARM_PAIR_Y, panel), ":",
            fontsize=10, color=BLUE, ha="center", va="center", zorder=5)
    # the "Mo-Fr" range summary row (NO date row on this screen)
    ax.text(0, -smp(ALARM_RANGE_Y, panel), "Mo–Fr", fontsize=6.5, color=GRAY,
            ha="center", va="center", zorder=5)
    # the seven day handles at ALARM_DAY_ANG(d) = 181 + 30 d — real 34 px
    # circles (they poke out of the 10 px ring), 4 px ext-click not drawn
    days = "MTWTFSS"
    from matplotlib.patches import Circle
    for d in range(7):
        a = 181 + 30 * d
        x, y = pt(R(ALARM_HANDLE_R), a)
        big = d in (0, 4)  # the default working endpoints (start, end)
        ax.add_patch(Circle((x, y), R(ALARM_HANDLE_D) / 2, zorder=4,
                            facecolor=AMBER if big else BLUE,
                            edgecolor="#6b4e08" if big else "#1e3f70",
                            linewidth=0.6))
        lx, ly = pt(R(ALARM_HANDLE_R) + R(32), a)
        ax.text(lx, ly, days[d], fontsize=6.0, color=GRAY,
                ha="center", va="center", zorder=5)
    if annotate:
        for r, label, a in ((panel / 2, "outer = panel/2", 243),
                            (ALARM_HANDLE_R, "218 handles", 257),
                            (panel / 2 - R(ALARM_W), "w = 10", 128)):
            x, y = pt(r if r == panel / 2 else R(r), a)
            ax.text(x, y, label, fontsize=5.6, color=GRAY, ha="center",
                    va="center", zorder=5)
        x, y = pt(R(ALARM_HANDLE_R) - R(30), 181 + 30 * 2)
        ax.text(x, y, "34 px handles", fontsize=5.2, color=AMBER,
                ha="center", va="center", zorder=5)
        ax.text(smp(ALARM_HOUR_X, panel), -smp(ALARM_PAIR_Y, panel) - R(62),
                "140×110 @ (−80/+70, −23)", fontsize=5.2, color=BLUE,
                ha="center", va="center", zorder=5)
        ax.text(0, -(panel / 2 - uipx(EXIT_SIZE + EXIT_BOTTOM, panel)) - R(14),
                "exit band top y 336", fontsize=5.2, color=GRAY, ha="center",
                va="center", zorder=5)


def draw_alarm_rules(ax, panel, ink, annotate):
    """create_screen_settings_alarm: master + the two diagonal entries.
    Glyphs are LV_SIZE_CONTENT labels — the ink is the tier's FA icon
    face (96/148/74 physical px, fonts{,_p4,_knob}), same as gen_hub_ring."""
    from matplotlib.patches import Circle
    R = lambda v: uipx(v, panel)
    # the shared sub-screen furniture first (behind the content)
    exit_band(ax, panel)
    # thin construction diagonals centre -> entries (guides, not entities)
    for sx in (-1, 1):
        ax.plot([0, sx * R(RULES_R)], [0, -R(RULES_R)], ":", color=LIGHT,
                linewidth=0.8, zorder=1)

    def disk(cx, cy, color, edge, label):
        x, y = smp(cx, panel), -smp(cy, panel)
        ax.add_patch(Circle((x, y), ink / 2, facecolor=color, alpha=0.85,
                            edgecolor=edge, linewidth=0.7, zorder=3))
        ax.text(x, y - ink * 0.78, label, fontsize=6.0, color=BLUE,
                ha="center", va="center", zorder=5)

    # master deactivate-all: regular FA alarm glyph at (0, -30)
    disk(0, -RULES_MASTER_Y, AMBER, "#6b4e08", "master — deactivate all")
    # the two entries at (±130, +130): solid stopwatch | regular alarm-clock
    disk(-RULES_R, RULES_R, BLUE, "#1e3f70", "stopwatch → pomodoro")
    disk(+RULES_R, RULES_R, BLUE, "#1e3f70", "alarm-clock → alarm-time")

    if annotate:
        ax.text(0, R(RULES_MASTER_Y) + ink * 0.85, "master (0, −30)",
                fontsize=5.6, color=AMBER, ha="center", va="center", zorder=5)
        x, y = pt(R(RULES_R) * math.sqrt(2), 45)  # the (130, +130) entry
        ax.text(x + ink * 0.95, y, "±(130, +130)\nsolid stopwatch /\nregular alarm-clock",
                fontsize=5.2, color=GRAY, ha="left", va="center", zorder=5)


def main() -> None:
    fig, axes = plt.subplots(3, 3, figsize=(11.0, 10.9))
    for col, (panel, size, ink) in enumerate(PANELS):
        for row, (fn, title) in enumerate((
                (draw_wheel, "wheel keyboard"),
                (draw_alarm_rules, "alarm rules"),
                (draw_alarm_band, "alarm-time band"))):
            ax = axes[row][col]
            panel_frame(ax, size)
            if fn is draw_alarm_rules:
                fn(ax, size, ink, annotate=(col == 0))
            else:
                fn(ax, size, annotate=(col == 0))
            ax.set_title(f"{panel}\n{title} — radii = UI_PX(code)",
                         fontsize=8, color="#3c3c3c", pad=5)
    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
