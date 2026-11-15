#!/usr/bin/env python3
"""Generate the strip-staging figure for docs/implementation/display.rst —
how a moon frame reaches the panel on each clock board.

The geometry is the code's own (components/ui/moon_ui.c and the vendored
BSPs):

* S3: the 400 px disk tiles into 400/25 = 16 INVALIDATION strips
  (MOON_STRIP_ROWS 25); each strip rides the BSP draw buffer of
  buffer_height = 25 rows (components/esp32_s3_touch_amoled_1_75) —
  466*25*2 = 23.3 KiB, under the 32 KiB internal-RAM SPI-DMA cap,
  queue depth 1 (the NO_MEM fix): one strip = one draw buffer = one
  QSPI transfer.
* P4: 650/130 = 5 pacing strips (MOON_STRIP_ROWS 130 — a pacing
  choice, the MIPI-DSI panel has no SPI-DMA cap); the flush itself
  runs the XC BSP's 50-row DPI-DMA buffer (buffer_height 50).
* Both: the finished frame lands as ONE atomic lv_refr_now under
  s_rgb_mtx with the drain witness; the black surround (26 % less
  traffic) is why only the disk rows ever stage.

Palette follows gen_perf_figs.py: blue #2b5fb0 (baked rows), cyan
#0891b2 (in-flight strips), amber #b07d10 (DMA caps), vermilion
#c2410c (the atomic-show gate); dashed guides are geometry.

Run:  docs/.venv/bin/python docs/tools/gen_strip_staging.py
"""
from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "_static", "figs", "strip_staging.png")

BLUE = "#2b5fb0"
CYAN = "#0891b2"
AMBER = "#b07d10"
VERM = "#c2410c"
GRAY = "#8a8a8a"
LGRAY = "#c9c9c4"


def panel(ax, title, panel_px, moon_px, strip_rows, buf_rows, cap_note):
    """One board: panel square, disk box, pacing strips, staging stack."""
    n_strips = moon_px // strip_rows
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_xlim(-20, panel_px + 210)
    ax.set_ylim(-20, panel_px + 30)

    # panel + disk
    ax.add_patch(Rectangle((0, 0), panel_px, panel_px, fill=False,
                           edgecolor=LGRAY, linewidth=1.0))
    off = (panel_px - moon_px) / 2
    # baked rows below the watermark, two cyan strips in flight, rest
    # shows the disk frame tone
    ax.add_patch(Rectangle((off, off), moon_px, moon_px,
                           facecolor="#dfe6f1", edgecolor=BLUE, linewidth=1.1))
    for i in range(n_strips):
        y = off + i * strip_rows
        ax.plot([off, off + moon_px], [y, y], "-", color=BLUE,
                linewidth=0.5, alpha=0.5)
    for i in (n_strips - 2, n_strips - 1):
        y = off + i * strip_rows
        ax.add_patch(Rectangle((off, y), moon_px, strip_rows,
                               facecolor=CYAN, alpha=0.35, edgecolor=CYAN,
                               linewidth=0.8))
    ax.text(panel_px / 2, -12, "black surround — only the disk rows stage",
            ha="center", fontsize=6.5, color="#3c3c3c")
    ax.text(off, off + moon_px + 8,
            f"disk {moon_px} px · {n_strips} pacing strips × {strip_rows} rows",
            fontsize=7, color=BLUE)

    # staging stack (draw buffers) to the right of the panel
    bx = panel_px + 28
    bw = 96
    for i, (c, a) in enumerate(((CYAN, 0.35), (LGRAY, 0.5), (LGRAY, 0.28))):
        y = panel_px - 90 - i * 34
        h = bw * buf_rows / panel_px * 1.15
        ax.add_patch(Rectangle((bx, y), bw, h, facecolor=c, edgecolor=CYAN if i == 0 else GRAY,
                               alpha=a, linewidth=0.8))
        tag = "in-flight" if i == 0 else "queued"
        ax.text(bx + bw + 4, y + h / 2, f"{buf_rows}-row buffer — {tag}", va="center",
                fontsize=6.0, color="#3c3c3c")
    ax.annotate("", xy=(bx - 6, panel_px - 92), xytext=(off + moon_px + 4, off + moon_px - 20),
                arrowprops=dict(arrowstyle="->", color=CYAN, lw=1.2))
    ax.text(bx, panel_px - 38, "staged → flush", fontsize=7, color=CYAN)
    ax.text(bx - 6, -12, cap_note, fontsize=6.5, color=AMBER)

    # the atomic-show gate
    ax.text(0, panel_px + 10, title, fontsize=8.5, color="#3c3c3c", fontweight="bold")
    ax.text(panel_px + 28, panel_px + 10,
            "ONE atomic lv_refr_now\nunder s_rgb_mtx + drain witness",
            fontsize=6.8, color=VERM)


def main() -> None:
    fig, axes = plt.subplots(1, 2, figsize=(11.6, 5.2))
    panel(axes[0], "S3 AMOLED (QSPI)", 466, 400, 25, 25,
          "466·25·2 = 23.3 KiB ≤ 32 KiB SPI-DMA cap ·\nqueue depth 1 — 1 strip = 1 buffer = 1 QSPI xfer")
    panel(axes[1], "P4 4C (MIPI-DSI)", 720, 650, 130, 50,
          "pacing choice, not a hard cap (no SPI-DMA limit);\nflush runs the XC BSP's 50-row DPI-DMA buffer")
    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight", pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}")


if __name__ == "__main__":
    main()
