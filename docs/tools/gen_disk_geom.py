#!/usr/bin/env python3
"""Generate the per-variant bake-disk containment figure for
docs/implementation/display.rst.

Every radius is the code's own arithmetic, not a hand placement:

* seconds arc: a full-panel ``lv_arc`` with pad UI_PX(10) on all four
  sides (create_screen_modem_sleep, components/ui/screens.c).
  lv_arc's get_center (widgets/arc/lv_arc.c) takes r = (min(w, h) -
  pads) / 2 as the OUTER stroke edge, and lv_draw_sw_arc builds the
  band mask INWARD from it (area_out = the coords, area_in inset by
  the stroke width), so the theme's 12 px stroke (LV_DPX_CALC(130,
  15)) inks r-11 .. r — NOT centred on r. Probed along the
  top-centre column of the committed modem-sleep PNGs: S3 rows
  10..21 (ink 212..223), P4 15..26 (334..345), knob 7..18
  (162..173).
* moon limb: MOON_PX/2 centred — 200 (S3, MOON_PX 400) / 325 (P4,
  MOON_PX 650) / 150 (knob, knob_ui_set_disk_geom(150.0f, 180),
  components/knob — the shared seam centres the disk; the LEGACY
  own-screen view baked r 150 at cy 168 for its caption row).
* the containment rule (task #48, 2026-11-14): the limb sits INSIDE
  the arc ink on every board — 12 px (S3), 9 px (P4), 12 px (knob,
  reproducing the S3's ink-inner-to-limb relationship exactly). The
  battery gauge and the gated progress row OVERLAP the black
  surround instead — they never push the disk off centre (task #35
  rule).

UI_PX() integer division is reproduced exactly (gen_hub_ring.py's
uipx). Palette follows gen_perf_figs.py: blue #2b5fb0 (moon disk),
vermilion #c2410c (the measured gap), gray ring = the arc's own
0xff787878 ink; dashed guides are geometry, not entities.

Run:  docs/.venv/bin/python docs/tools/gen_disk_geom.py
"""
from __future__ import annotations

import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "disk_geom.png")

BLUE = "#2b5fb0"
VERM = "#c2410c"
GRAY = "#787878"          # the seconds arc's own indicator colour
LGRAY = "#c9c9c4"

VARIANTS = {
    "s3":   dict(panel=466, limb=200, cy=233, title="S3 AMOLED — 466 px",
                 note="MOON_PX 400 · limb 200"),
    "p4":   dict(panel=720, limb=325, cy=360, title="P4 4C — 720 px",
                 note="MOON_PX 650 · limb 325"),
    "knob": dict(panel=360, limb=150, cy=180, title="knob — 360 px",
                 note="seam r 150 · cy 180"),
}
ARC_W = 12  # theme stroke width (LV_DPX_CALC(130, 15) -> 12), inked INWARD
            # from get_center's r (the OUTER stroke edge), never centred


def uipx(x: int, panel: int) -> int:
    """The device macro, integer division."""
    return (x * panel) // 466


def circle(ax, r, **kw):
    th = [math.radians(a) for a in range(0, 361, 2)]
    xs = [r * math.cos(t) for t in th]
    ys = [r * math.sin(t) for t in th]
    if kw.pop("fill", False):
        ax.fill(xs, ys, **kw)
    else:
        ax.plot(xs, ys, **kw)


def main() -> None:
    fig, axes = plt.subplots(1, 3, figsize=(11.2, 4.1))
    for ax, cfg in zip(axes, VARIANTS.values()):
        panel = cfg["panel"]
        pad = uipx(10, panel)
        # get_center's r IS the outer stroke edge; the stroke inks INWARD
        outer = (panel - 2 * pad) // 2
        ink_in, ink_out = outer - ARC_W + 1, outer  # discrete inked rows
        limb = cfg["limb"]
        gap = round(ink_in - limb)

        ax.set_aspect("equal")
        ax.axis("off")

        # round window (panel edge) — gray, the product housing
        circle(ax, panel / 2, color=LGRAY, linewidth=1.0, zorder=1)

        # seconds-arc ink band: stroke width 12 INWARD from get_center's r
        for r, a in ((ink_in, 0.9), (ink_out, 0.9)):
            circle(ax, r, color=GRAY, linewidth=1.0, alpha=a, zorder=2)
        circle(ax, outer - ARC_W / 2, color=GRAY, linewidth=ARC_W, alpha=0.55,
               zorder=2)

        # the bake disk, centred (cy == panel/2 everywhere today)
        circle(ax, limb, fill=True, color=BLUE, alpha=0.28, zorder=3)
        circle(ax, limb, color=BLUE, linewidth=1.2, zorder=4)

        # the measured limb->ink gap, drawn along +x with an arrow
        y0 = 0
        ax.annotate("", xy=(ink_in - 0.5, y0), xytext=(limb, y0),
                    arrowprops=dict(arrowstyle="<->", color=VERM, lw=1.1),
                    zorder=5)
        ax.text((limb + ink_in) / 2, 6, f"{gap} px", ha="center", va="bottom",
                fontsize=7.5, color=VERM, zorder=5)

        # labels
        ax.text(0, 0, cfg["note"], ha="center", va="center",
                fontsize=6.8, color="#1e3f70", zorder=6)
        pct = 2 * limb / panel * 100
        ax.text(0, -panel / 2 + 4, f"disk {2 * limb} px = {pct:.1f}% of panel",
                ha="center", va="bottom", fontsize=6.5, color="#3c3c3c", zorder=6)
        ax.set_title(cfg["title"] + f"\narc pad {pad} · outer {outer} · ink "
                     f"{ink_in}..{ink_out}",
                     fontsize=8.5, color="#3c3c3c", pad=6)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight", pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
