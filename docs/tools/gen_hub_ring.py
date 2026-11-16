#!/usr/bin/env python3
"""Generate the settings-hub ring figure for docs/ui/settings-hub.rst.

The icon centres are NOT hand-placed: they are the positions
``create_screen_settings_hub`` (components/ui/screens.c) computes —
n icons 360/n degrees apart, the deep-sleep icon anchored at 180 deg
(found by target), each at th = 180 + step*(i - deep) degrees
clockwise from 12 o'clock, position (R sin th, -R cos th) about the
panel centre, R = UI_PX(140). The UI_PX() integer division is
reproduced exactly. Per-panel icon ink comes from the baked font
tiers (96 / 148 / 74 — fonts{,_p4,_knob}, see components/ui/
CMakeLists.txt).

Palette follows gen_perf_figs.py: blue #2b5fb0 (icons), amber
#b07d10 (exit), gray guides are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_hub_ring.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "hub_ring.png")

BLUE = "#2b5fb0"
AMBER = "#b07d10"
GRAY = "#8a8a8a"
LIGHT = "#c9c9c4"

# name -> (glyph label, ring-array index) is keyed by TARGET so the
# deep-sleep anchor is found by target, exactly like the device.
RING = {
    "s3": dict(panel=466, ink=96,
               order=["alarm", "light", "deep", "wifi", "tz", "sound", "display"],
               title="S3 — 466 px\n7 icons × ~51.4° · R = 140"),
    "p4": dict(panel=720, ink=148,
               order=["clock", "alarm", "light", "deep", "wifi", "tz", "sound", "display"],
               title="P4 — 720 px\n8 icons × 45° · R = 216"),
    "knob": dict(panel=360, ink=74,
                 order=["alarm", "light", "deep", "wifi", "tz", "sound", "display"],
                 title="knob — 360 px\n7 icons × ~51.4° · R = 108"),
}

# short label per target (the screenshot pages carry the real glyphs)
SHORT = {"alarm": "alarm", "light": "light\nsleep", "deep": "deep\nsleep",
         "wifi": "wifi", "tz": "tz", "sound": "sound", "clock": "time\n(P4)",
         "display": "display\ntrim"}


def uipx(x: int, panel: int) -> int:
    """The device macro, integer division."""
    return (x * panel) // 466


def main() -> None:
    fig, axes = plt.subplots(1, 3, figsize=(11.0, 3.9))
    for ax, (key, cfg) in zip(axes, RING.items()):
        panel, ink = cfg["panel"], cfg["ink"]
        n = len(cfg["order"])
        deep = cfg["order"].index("deep")
        step = 360.0 / n
        R = uipx(140, panel)
        tap = max(uipx(150, panel), uipx(96, panel))

        ax.set_aspect("equal")
        ax.axis("off")

        # panel edge (solid gray) and the ring circle (dashed light)
        for r, style in ((panel / 2, "-"), (R, "--")):
            th = [math.radians(a) for a in range(0, 361, 3)]
            color = GRAY if style == "-" else LIGHT
            lw = 1.0 if style == "-" else 0.9
            ax.plot([r * math.sin(t) for t in th], [r * math.cos(t) for t in th],
                    style, color=color, linewidth=lw, zorder=1)

        # centre exit: amber disk at the glyph ink, dotted tap box
        th = [math.radians(a) for a in range(0, 361, 3)]
        ax.fill([ink / 2 * math.sin(t) for t in th], [ink / 2 * math.cos(t) for t in th],
                color=AMBER, alpha=0.30, zorder=2)
        ax.plot([ink / 2 * math.sin(t) for t in th], [ink / 2 * math.cos(t) for t in th],
                "-", color=AMBER, linewidth=1.1, zorder=3)
        ax.plot([tap / 2 * math.sin(t) for t in th], [tap / 2 * math.cos(t) for t in th],
                ":", color=AMBER, linewidth=0.8, alpha=0.7, zorder=2)
        ax.text(0, 0, "exit", ha="center", va="center", fontsize=6.5,
                color="#6b4e08", zorder=4)

        # the ring icons at the code's own positions
        for i, target in enumerate(cfg["order"]):
            th_d = math.radians(180.0 + step * (i - deep))
            # device: dx = (int)(R sin th), screen dy = -(int)(R cos th);
            # matplotlib plots y-up, so the plot y is +R cos th
            dx = int(R * math.sin(th_d))
            dy = int(R * math.cos(th_d))
            ax.plot([0, dx], [0, dy], "-", color=LIGHT, linewidth=0.6, zorder=1)
            ax.scatter([dx], [dy], s=(ink / panel * 72 * 2.2) ** 2,
                       facecolor=BLUE, edgecolor="#1e3f70", linewidth=0.7,
                       alpha=0.85, zorder=3)
            # label outside the dot along the same radius
            off = math.hypot(dx, dy) or 1.0
            lx, ly = (dx + dx / off * ink * 0.75), (dy + dy / off * ink * 0.75)
            ax.text(lx, ly, SHORT[target], ha="center", va="center",
                    fontsize=6.2, color=BLUE, zorder=4)

        ax.set_title(cfg["title"], fontsize=8.5, color="#3c3c3c", pad=6)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight", pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
