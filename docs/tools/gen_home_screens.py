#!/usr/bin/env python3
"""Generate the computed home-family screen figure for docs/ui/index.rst.

The five moon screens (modem-sleep, wifi, light-sleep, deep-sleep,
alarm) on all three family panels, drawn from the code's OWN object
sets — create_screen_modem_sleep / _wifi / _light_sleep / _deep_sleep /
_alarm in components/ui/screens.c — the way gen_hub_ring.py and
gen_menu_geom.py do for the menu surfaces. What each screen shows
(verified in the builders, then cross-checked against the committed
screenshots):

* modem-sleep: moon disk + the shared clock widget + the seconds arc.
* wifi: the same, plus the wifi connection glyph (solid FA face).
* light-sleep: moon + clock only — NO arc, NO glyph. The runtime dims
  the clock container to UI_CLOCK_ALPHA_PCT (20 %) and the 0x51 ramp
  dims the panel to UI_LIGHT_SLEEP_DIM_PCT (25 %) (actions.c); the
  uisim mirrors both (host_sim/uisim/main.c:359) and the committed
  light_sleep PNGs carry the 20 %-alpha digits (zero pixels >= 245 in
  the time row vs 7335 on modem-sleep).
* deep-sleep: moon + sleep glyph (solid FA face); the clock container
  is LV_OBJ_FLAG_HIDDEN (actions.c:1336, mirrored by the sim at
  main.c:361 — the committed deep_sleep PNGs have zero >= 245 pixels
  in the time row) and the backlight is off.
* alarm: clock + alarm glyph (REGULAR FA face) — NO moon
  (create_screen_alarm calls no moon_screen_prepare).
* The knob reaches only modem/wifi/alarm with a screen: light sleep is
  its real LEDC backlight dim (GPIO 47), deep sleep the real
  esp_deep_sleep_start (panel off, RTC TIMER wake) — those two cells
  are marked, not drawn.

Every radius/box is the code's arithmetic or the probed truth:

* moon limb MOON_PX/2 — 200 / 325 / 150, centred (the shared seam).
* seconds arc: 466-space object with pad UI_PX(10); get_center's
  r = (panel - 2*pad)//2 is the stroke's OUTER edge and the theme's
  12 px stroke inks INWARD (lv_arc.c / lv_draw_sw_arc.c) — S3
  212..223, P4 334..345, knob 162..173 (the gen_disk_geom.py family).
* glyph anchor: lv_obj_set_pos top-left (UI_PX(185), UI_PX(320)),
  LV_SIZE_CONTENT = the tier's FA icon face 96/148/74 (the same ink
  gen_hub_ring.py draws; probed clean boxes in alarm.png).
* clock widget (layout contract 2): ONE widget reparented by
  ui_clock_attach_to onto all five moon screens — identical absolute
  geometry everywhere (probed pixel counts are identical across the
  committed shots). Boxes from one host_sim/uisim --clockprobe run
  (UISIM_TIME=21:26): time row S3 (72,175,322x71) cells 136+30+136 at
  72/218/258, alarm row (72,256,322x34); P4 (116,274,488x108) cells
  210+46+210 at 116/337/394, alarm row (116,393,488x52); knob
  (53,134,253x55) cells 105+23+105 at 53/168/201, alarm row
  (53,199,253x26). The cells are UI_PX(136)/UI_PX(30)/UI_PX(136) with
  the theme's card pad_row gap (LV_DPX_PAD_SMALL: 10/11/10).

Palette follows the house figures: blue #2b5fb0 (moon disk, clock
cells), gray #787878 (the arc's own indicator colour), amber #b07d10
(glyph face), LGRAY housing; dashed marks are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_home_screens.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "home_screens.png")

BLUE = "#2b5fb0"
AMBER = "#b07d10"
GRAY = "#787878"          # the seconds arc's own indicator colour
LGRAY = "#c9c9c4"

PANELS = (
    dict(name="S3 AMOLED — 466 px", panel=466, limb=200, ink_face=96,
         row=(72, 175, 322, 71), cells=(72, 218, 258), cw=(136, 30, 136),
         gap=10, arow=(72, 256, 322, 34)),
    dict(name="P4 4C — 720 px", panel=720, limb=325, ink_face=148,
         row=(116, 274, 488, 108), cells=(116, 337, 394), cw=(210, 46, 210),
         gap=11, arow=(116, 393, 488, 52)),
    dict(name="knob — 360 px", panel=360, limb=150, ink_face=74,
         row=(53, 134, 253, 55), cells=(53, 168, 201), cw=(105, 23, 105),
         gap=10, arow=(53, 199, 253, 26)),
)
ARC_W = 12  # theme stroke width (LV_DPX_CALC(130, 15)), inked INWARD
GLYPH_X, GLYPH_Y = 185, 320  # the indicator anchor (UI_PX, top-left)

# screen row -> what it holds: moon? arc? clock (opa/hidden)? glyph weight?
SCREENS = (
    ("modem-sleep", "moon + clock + seconds arc", True, True, "full", None),
    ("wifi", "moon + clock + arc + wifi glyph", True, True, "full", "solid"),
    ("light-sleep", "moon + clock @ 20 % (panel 25 %)", True, False, "dim20",
     None),
    ("deep-sleep", "moon + sleep glyph — clock hidden", True, False, "hidden",
     "solid"),
    ("alarm", "clock + alarm glyph — NO moon", False, False, "full", "regular"),
)
KNOB_NO_SCREEN = {  # the knob replaces these two SCREENS with hardware
    "light-sleep": "no screen — real LEDC backlight dim (GPIO 47)",
    "deep-sleep": "no screen — esp_deep_sleep_start, RTC TIMER wake",
}


def uipx(x: int, panel: int) -> int:
    return (x * panel) // 466


def panel_frame(ax, panel):
    th = [math.radians(a) for a in range(0, 361, 3)]
    r = panel / 2
    ax.plot([r * math.cos(t) for t in th], [r * math.sin(t) for t in th],
            "-", color=LGRAY, linewidth=1.1, zorder=1)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_xlim(-r * 1.05, r * 1.05)
    ax.set_ylim(-r * 1.05, r * 1.05)


def y_of(py, panel):
    """Panel px (y-DOWN from top-left) to the y-up plot frame."""
    return panel / 2 - py


def box(ax, x, y, w, h, panel, **kw):
    ax.add_patch(Rectangle((x - panel / 2, y_of(y + h, panel)), w, h, **kw))


def draw_clock(ax, cfg, mode):
    panel = cfg["panel"]
    alpha = {"full": 0.30, "dim20": 0.10, "hidden": 0.0}[mode]
    if mode == "hidden":
        return
    x, y, w, h = cfg["row"]
    for cx, cw in zip(cfg["cells"], cfg["cw"]):
        box(ax, cx, y, cw, h, panel, facecolor=BLUE, alpha=alpha,
            edgecolor=BLUE, linewidth=0.7, zorder=4)
    lab_y = y_of(y + h / 2, panel)
    if alpha > 0:
        ax.text(cfg["cells"][0] + cfg["cw"][0] / 2 - panel / 2, lab_y, "HH",
                fontsize=6.2, color="#1e3f70", ha="center", va="center",
                zorder=5)
        ax.text(cfg["cells"][2] + cfg["cw"][2] / 2 - panel / 2, lab_y, "MM",
                fontsize=6.2, color="#1e3f70", ha="center", va="center",
                zorder=5)
    if mode == "dim20":
        ax.text(x + w / 2 - panel / 2, lab_y, "20 % opa", fontsize=5.4,
                color=GRAY, ha="center", va="center", zorder=5)
    rx, ry, rw, rh = cfg["arow"]
    box(ax, rx, ry, rw, rh, panel, facecolor="none", edgecolor=BLUE,
        linestyle=":", linewidth=0.8, alpha=0.8, zorder=4)
    if alpha > 0:
        ax.text(rx + rw / 2 - panel / 2, y_of(ry + rh / 2, panel),
                "alarm row", fontsize=5.0, color=GRAY, ha="center",
                va="center", zorder=5)


def draw_screen(ax, cfg, scr):
    panel = cfg["panel"]
    name, _, moon, arc, clock_mode, glyph = scr
    if name in KNOB_NO_SCREEN and cfg["name"].startswith("knob"):
        panel_frame(ax, panel)
        ax.text(0, 0, KNOB_NO_SCREEN[name], fontsize=6.4, color=GRAY,
                ha="center", va="center", zorder=5, wrap=True)
        return
    panel_frame(ax, panel)
    if moon:
        th = [math.radians(a) for a in range(0, 361, 2)]
        r = cfg["limb"]
        ax.fill([r * math.cos(t) for t in th], [r * math.sin(t) for t in th],
                color=BLUE, alpha=0.26, zorder=2)
        ax.plot([r * math.cos(t) for t in th], [r * math.sin(t) for t in th],
                color=BLUE, linewidth=1.0, zorder=3)
    if arc:
        pad = uipx(10, panel)
        outer = (panel - 2 * pad) // 2
        th = [math.radians(a) for a in range(0, 361, 2)]
        ax.plot([(outer - ARC_W / 2) * math.cos(t) for t in th],
                [(outer - ARC_W / 2) * math.sin(t) for t in th],
                color=GRAY, linewidth=ARC_W, alpha=0.85, zorder=2)
    draw_clock(ax, cfg, clock_mode)
    if glyph:
        gx, gy = uipx(GLYPH_X, panel), uipx(GLYPH_Y, panel)
        face = cfg["ink_face"]
        box(ax, gx, gy, face, face, panel, facecolor=AMBER, alpha=0.18,
            edgecolor=AMBER, linestyle="--", linewidth=0.8, zorder=4)
        ax.text(gx + face / 2 - panel / 2, y_of(gy + face / 2, panel),
                f"{glyph}\nglyph {face} px", fontsize=5.2, color=AMBER,
                ha="center", va="center", zorder=5)


def main() -> None:
    fig, axes = plt.subplots(len(SCREENS), len(PANELS), figsize=(12.4, 16.4))
    for row, scr in enumerate(SCREENS):
        for col, cfg in enumerate(PANELS):
            ax = axes[row][col]
            draw_screen(ax, cfg, scr)
            if row == 0:
                ax.set_title(cfg["name"], fontsize=8.5, color="#3c3c3c",
                             pad=5)
            ax.text(0, -cfg["panel"] / 2 * 1.06, f"{scr[0]} — {scr[1]}",
                    fontsize=7.0, color="#3c3c3c", ha="center", va="top",
                    clip_on=False, zorder=6)
    fig.tight_layout()
    fig.savefig(OUT, dpi=150, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
