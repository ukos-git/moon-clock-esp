#!/usr/bin/env python3
"""Generate the resolution funnel for docs/verification/performance.rst.

Every other ladder counts a resource a refresh CONSUMES — time, bytes,
RAM, watts. This one counts the resource the picture is made OF: how
finely each stage can speak. The bake samples the equirect albedo and
the relief gradient cache NEAREST-NEIGHBOR (moon_bake.c:450-464:
``int col = (int)(colf + 0.5f)`` and ``(int)(row_frac * alb_h + 0.5f)``)
and the bake's own header comment justifies it with a number — the
per-board texture "is >= 1 texel per screen px on the disk ON AVERAGE"
(the comment carries that qualifier since the figure was drawn; it used
to state the average as a guarantee). Left panel: where the resolution comes from (panel
px, disk px, source columns — the funnel narrows from left to right on
every board). Right panel: the claim tested honestly. The density a
texel has ON SCREEN is orthographic: at the disk CENTRE one radian
spans exactly R screen px (r = u*right + v*up + w*E, w = sqrt(1-r^2)),
so centre density = W/(2*pi*R) texels per screen px — the WORST place
on the disk (texels compress to zero width at the limb). That is pi/2
= 1.57x BELOW the naive diameter-average (W/2 columns over a 2R disk),
which is the number the comment's ">= 1" rides on: it holds at the
centre for the relief caches and the P4's albedo (to 0.3 %), but NOT
for the S3's albedo (0.69 — its 1.08 is the average) nor the knob's
albedo (0.92). The conclusion survives anyway, in its honest form: the
NN pick snaps to the texel centre, so the worst-case pick error is HALF
a texel — 0.73/0.31/0.50/0.50/0.55/0.46 px, every one sub-pixel. The
figure plots the pick error on every bar; "sub-pixel-accurate" is true
as a bound, not as a density.

Every number is PARSED from the live sources at figure time (the house
pattern); nothing is measured, nothing is invented:

  #define MOON_ALB_W / _H, MOON_DEM_W / _H
      components/moon/generated/{s3,p4,knob}/moon_consts.h (generated
      from assets/images/moon/moon/constants.py — asserted equal to the
      bake comment's own "864 on the S3 / 2048 on the P4" :62-63)
  #define MOON_PX 650 / 400 + MOON_R = MOON_PX/2
      components/ui/moon_ui.c:174-178/:185 (the disk radius on the
      clocks: R = MOON_PX/2 = 325/200)
  knob_ui_set_disk_geom(150.0f, KNOB_LCD_H_RES / 2)
      knob/components/knob_ui/knob_moon_render.c:179 — the shared-home
      disk radius r = 150 at cy = 180 (parsed, not recalled)
  #define BSP_LCD_H_RES (466)            components/esp32_s3_touch_amoled_1_75/include/bsp/display.h:20
  CONFIG_BSP_LCD_TYPE_720_720_4_INCH     build/p4/config/sdkconfig.h (P4 panel 720)
  #define KNOB_LCD_H_RES 360             knob/components/knob_board/include/knob_board.h:22
  NN rounding shape asserts              components/moon/core/moon_bake.c:457/:460
  "is >= 1 texel per screen px on the disk ON AVERAGE" + "pick error is
      half a texel" shapes   components/moon/core/moon_bake.c header comment
      (the claim this figure audits — the parser fails if it is reworded away)

Derived (one product each, nothing new measured): centre density
W/(2*pi*R) and the half-texel pick error pi*R/W (half the angular
texel 360/W at R px per radian); the naive diameter-average W/(4R) is
plotted on the albedo bars to show WHERE the comment's average comes
from.

Run:  docs/.venv/bin/python docs/tools/gen_perf_sharp.py
"""
from __future__ import annotations

import math
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_sharpness.png")

GEN = os.path.join(ROOT, "components", "moon", "generated")
BAKE_C = os.path.join(ROOT, "components", "moon", "core", "moon_bake.c")
UI_C = os.path.join(ROOT, "components", "ui", "moon_ui.c")
BSP_DISPLAY_H = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                             "include", "bsp", "display.h")
P4_CFG_H = os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h")
KNOB_UI_C = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_moon_render.c")
KNOB_BOARD_H = os.path.join(ROOT, "knob", "components", "knob_board", "include",
                            "knob_board.h")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # the disk the bake is asked to fill
TEAL = "#0e7490"    # a source that clears 1 texel/px at the centre
VIOLET = "#7c3aed"  # the relief gradient cache
RED = "#b91c1c"     # a source that does NOT clear the line at the centre
AMBER = "#b07d10"   # the claim's own bar


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — the "
                         f"generated headers / build tree must exist for "
                         f"this figure (build the target first)")
    with open(path, encoding="utf-8") as f:
        return f.read()


def hdr_define(path: str, symbol: str, pattern: str = r"\s+(\d+)") -> int:
    m = re.search(rf"#define {symbol}{pattern}", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.relpath(path, ROOT)}")
    return int(m.group(1))


def main() -> None:
    # ---- the sources of resolution, parsed --------------------------------
    src = {}
    for board in ("s3", "p4", "knob"):
        hdr = os.path.join(GEN, board, "moon_consts.h")
        src[board] = {
            "alb_w": hdr_define(hdr, "MOON_ALB_W"),
            "alb_h": hdr_define(hdr, "MOON_ALB_H"),
            "dem_w": hdr_define(hdr, "MOON_DEM_W"),
            "dem_h": hdr_define(hdr, "MOON_DEM_H"),
        }

    # ---- the screens they fill, parsed -------------------------------------
    s3_panel = hdr_define(BSP_DISPLAY_H, "BSP_LCD_H_RES", r" \((\d+)\)")
    m = re.search(r"#define CONFIG_BSP_LCD_TYPE_(\d+)_(\d+)_4_INCH", _read(P4_CFG_H))
    if not m:
        raise SystemExit("P4 panel size: no CONFIG_BSP_LCD_TYPE_*_INCH in "
                         "build/p4/config/sdkconfig.h — build the P4 target")
    p4_panel = int(m.group(1))
    knob_panel = hdr_define(KNOB_BOARD_H, "KNOB_LCD_H_RES")

    # ---- the disks, parsed --------------------------------------------------
    mu = re.search(r"#if defined\(CONFIG_BOARD_P4\)\n#define MOON_PX (\d+)\n"
                   r"#else\n#define MOON_PX (\d+)\n#endif", _read(UI_C))
    if not mu:
        raise SystemExit("MOON_PX pair: the #if CONFIG_BOARD_P4 block moved "
                         f"out of {os.path.relpath(UI_C, ROOT)}")
    moon_p4, moon_s3 = int(mu.group(1)), int(mu.group(2))
    if "#define MOON_R ((float)MOON_PX / 2.0f)" not in _read(UI_C):
        raise SystemExit("MOON_R is no longer MOON_PX/2 — the clock disk "
                         "radius formula changed; update this figure")
    md = re.search(r"knob_ui_set_disk_geom\((\d+(?:\.\d+)?)f, "
                   r"KNOB_LCD_H_RES / 2\)", _read(KNOB_UI_C))
    if not md:
        raise SystemExit("the shared-home disk geometry call moved out of "
                         f"{os.path.relpath(KNOB_UI_C, ROOT)}")
    knob_r = float(md.group(1))

    # ---- the claim under audit, shape-asserted ------------------------------
    bake = _read(BAKE_C)
    if "int col = (int)(colf + 0.5f);" not in bake or \
       "int r = (int)(row_frac * (float)c->alb_h + 0.5f);" not in bake:
        raise SystemExit("the nearest-neighbour half-texel rounding is gone "
                         f"from {os.path.relpath(BAKE_C, ROOT)} — the pick-"
                         "error bound this figure plots no longer holds")
    mc = re.search(r"is >= 1 texel per[\s*]+screen px on the disk ON AVERAGE", bake)
    if not mc or "pick error is half a texel" not in bake:
        raise SystemExit("the bake comment's \"is >= 1 texel per screen px on "
                         "the disk ON AVERAGE\" claim (and its half-texel "
                         "pick-error guarantee) was reworded away — re-read "
                         "the passage before plotting against it")
    mw = re.search(r"MOON_ALB_W,[\s*]+(\d+) on the S3 / (\d+) on the P4", bake)
    if not mw or int(mw.group(1)) != src["s3"]["alb_w"] or \
       int(mw.group(2)) != src["p4"]["alb_w"]:
        raise SystemExit("the bake comment's texture widths drifted from "
                         "the generated headers — fix the comment first")

    disks = {
        "s3": (moon_s3 / 2.0, s3_panel),
        "p4": (moon_p4 / 2.0, p4_panel),
        "knob": (knob_r, knob_panel),
    }

    # ---- derived (one product each) -----------------------------------------
    legs = []  # (board, kind, W, texels_per_px_centre, pick_err_px, naive_avg)
    for board in ("s3", "p4", "knob"):
        r, _panel = disks[board]
        for kind, key, naive in (("albedo", "alb_w", True), ("relief", "dem_w", False)):
            w = src[board][key]
            dpp = w / (2.0 * math.pi * r)          # texels per px, disc CENTRE
            pick = math.pi * r / w                  # half a texel, in px
            avg = w / (4.0 * r) if naive else None  # the comment's averaging
            legs.append((board, kind, w, dpp, pick, avg))

    # ---- figure -------------------------------------------------------------
    fig, axes = plt.subplots(1, 2, figsize=(11.6, 4.6))

    # (a) where the resolution comes from
    ax = axes[0]
    left_rows = []
    for board in ("s3", "p4", "knob"):
        r, panel = disks[board]
        d = disks[board][0] * 2.0
        left_rows.append((f"{board} · panel", panel, INK, f"{panel} px square"))
        left_rows.append((f"{board} · disk", d, BLUE,
                          f"diameter {d:.0f} px (r = {r:.0f})"))
        left_rows.append((f"{board} · albedo", src[board]["alb_w"], TEAL,
                          f"{src[board]['alb_w']}x{src[board]['alb_h']} = 360°"))
        left_rows.append((f"{board} · relief DEM", src[board]["dem_w"], VIOLET,
                          f"{src[board]['dem_w']}x{src[board]['dem_h']}"))
    for y, (label, n, color, ann) in enumerate(left_rows):
        ax.barh(y, n, color=color, alpha=0.6, height=0.62, edgecolor=color)
        ax.text(n * 1.03 + 4, y, ann, va="center", fontsize=6.2, color=MUTED)
    ax.set_yticks(range(len(left_rows)), [r0[0] for r0 in left_rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_xlim(0, 2700)
    ax.set_xlabel("pixels / columns — parsed at figure time")
    ax.set_title("where the resolution comes from\n"
                 "(the funnel narrows source → screen on every board)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # (b) centre density vs the claim's bar
    ax = axes[1]
    for y, (board, kind, w, dpp, pick, avg) in enumerate(legs):
        color = TEAL if dpp >= 1.0 else RED
        ax.barh(y, dpp, color=color, alpha=0.6, height=0.62, edgecolor=color)
        extra = f" · pick ≤ {pick:.2f} px"
        if avg is not None:
            extra += f"  · naive Ø-avg {avg:.2f}"
        ax.text(dpp + 0.02, y, f"{dpp:.2f} texel/px @centre{extra}",
                va="center", fontsize=6.2, color=MUTED)
    ax.axvline(1.0, color=AMBER, linestyle="--", linewidth=1.2)
    ax.text(0.05, 5.7,
            "1 texel per screen px — the bar the bake comment claims "
            "(moon_bake.c:63).\nAt the orthographic CENTRE it holds for the "
            "relief caches and the P4\nalbedo (to 0.3 %), NOT for the S3 "
            "albedo (0.69; its 1.08 is the Ø-average)\nnor the knob albedo. "
            "The honest guarantee is the half-texel PICK error\non every bar: "
            "all sub-pixel — which is what \"sub-pixel-accurate\"\nactually "
            "rides on.",
            fontsize=6.2, color=AMBER, va="top")
    ax.set_yticks(range(len(legs)),
                  [f"{b} · {k} {w}" for b, k, w, _, _, _ in legs], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(legs) + 3.2, -0.6)
    ax.set_xlim(0, 2.35)
    ax.set_xlabel("source texels per screen pixel at the disk CENTRE — "
                  "W / (2·pi·R), the orthographic worst case")
    ax.set_title("the funnel's narrow point — one NN tap per output pixel\n"
                 "(teal clears the bar, red does not; every pick error does)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(disks r: S3 {disks['s3'][0]:.0f} P4 {disks['p4'][0]:.0f} "
          f"knob {knob_r:.0f}; panels {s3_panel}/{p4_panel}/{knob_panel}; "
          + "; ".join(f"{b} {k} {dpp:.3f}/px pick {pk:.3f}px"
                      + (f" avg {av:.2f}" if av else "")
                      for b, k, _, dpp, pk, av in legs) + ")")


if __name__ == "__main__":
    main()
