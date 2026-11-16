#!/usr/bin/env python3
"""Generate the pixel ladder for docs/verification/performance.rst — the
counting-work axis next to perf_bus (how many BYTES cross the wire) and
perf_sharp (how FINELY each stage can speak): **how many PIXELS every
stage touches**, per board. Pixels are the currency the page's own
complexity section prices everything in — every per-pixel step is O(N) —
so this is the ladder the O(N) statements deserve: the Ns themselves,
side by side. Nothing is measured and nothing is invented: every bar is
parsed at figure time or one product of parsed numbers.

Left panel — the work ONE published frame touches:

  bake pixels          MOON_PX²        the mill's leg: per pixel, Lambert
                                       + one albedo NN tap + one PACKED
                                       relief cell (moon_bake.c)
  publish/convert      MOON_PX²        the gather: gray8 read, RGB565
                                       write (moon_convert_to_rgb; the
                                       knob has NO such leg — its bake
                                       writes RGB565 straight into the
                                       back buffer, knob_ui.c:368)
  hourly disk flush    MOON_PX²        the while-black whole-panel
                                       lv_refr_now with the moon image
                                       invalidated (perf_bus's violet row
                                       in pixels: bytes/2, one bar)
  whole-panel flush    panel²          the screen switch: the flush
                                       covers the whole panel, the moon
                                       image square covers only
                                       (MOON_PX/panel)² of it (the knob's
                                       home IS the square — the drawn
                                       disk is the circle of
                                       knob_ui_set_disk_geom's r inside)
  one DMA transaction  W × rows        the transaction the bus really
                                       sees, in pixels = bytes/2 (S3/knob
                                       = max_transfer_sz exactly; P4's
                                       50-row DPI buffer CITED — the xc
                                       BSP is absent, hatched)
  one reveal state     MOON_PX × rows  per SHOWN state, not per frame
                                       (MOON_REVEAL_ROWS 20 S3 / 26 P4;
                                       the knob reveals one ROW per
                                       state — the code's own "one atomic
                                       band lv_refr_now per completed
                                       row", knob_ui.c:31)

Right panel — the pixel universe behind one moon (per board, log): the
albedo texels (MOON_ALB_W×H, generated moon_consts.h), the DEM cells =
relief pack entries (MOON_DEM_W×H, ×2 B = the 4 MB wall), the bake
square, the panel square. The annotations carry the ratios: the relief
wall is 13.1× / 5.0× / 4.1× the bake square it feeds — yet exactly ONE
packed cell is read per bake pixel.

Parsed at figure time (SystemExit-failing parsers, the house pattern):

  #define MOON_PX 650 / 400 + MOON_R = MOON_PX/2   components/ui/moon_ui.c:174-178
  #define MOON_STRIP_ROWS 130 / 25                  moon_ui.c:193-199 (+ divisibility, the code's own #error)
  #define MOON_REVEAL_ROWS 26 / 20                  moon_ui.c:1192-1196 (+ divisibility)
  "always writes the BSP's 50-row buffer strips"    moon_ui.c:192-193 (P4 cited DPI rows)
  #define BSP_LCD_H_RES (466) / _V_RES / _BITS      vendored S3 BSP include/bsp/display.h
  .max_transfer_sz = BSP_LCD_H_RES * 25 * ... / .buffer_height = 25  vendored S3 BSP .c (asserted EQUAL — the bus lesson)
  CONFIG_BSP_LCD_TYPE_720_720_4_INCH                build/p4/config/sdkconfig.h
  #define KNOB_LCD_H_RES / _V_RES / _ROWS_PER_BUF   knob/components/knob_board/include/knob_board.h
  .max_transfer_sz = KNOB_LCD_H_RES * KNOB_LCD_ROWS_PER_BUF * sizeof(uint16_t)   knob_board_display.c (shape-asserted)
  #define MOON_SIZE KNOB_LCD_H_RES + s_buf[2]       knob/components/knob_ui/knob_ui.c:79/:97 (the bake square IS the panel)
  knob_ui_set_disk_geom(150.0f, KNOB_LCD_H_RES / 2) knob/components/knob_ui/knob_moon_render.c (the drawn disk, as gen_perf_sharp parses it)
  MOON_ALB_W/_H + MOON_DEM_W/_H                     components/moon/generated/{s3,p4,knob}/moon_consts.h (albedo asserted 2:1 equirect)

Identity colors per gen_perf_figs.py: S3 blue, P4 vermillion, knob
violet. Hatched = cited; italic = the honest *no such leg* (the bus
ladder's precedent).

Run:  docs/.venv/bin/python docs/tools/gen_perf_pixels.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_pixels.png")

UI_C = os.path.join(ROOT, "components", "ui", "moon_ui.c")
GEN = os.path.join(ROOT, "components", "moon", "generated")
BSP_DISPLAY_H = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                             "include", "bsp", "display.h")
BSP_C = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                     "esp32_s3_touch_amoled_1_75.c")
CFG_P4 = os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h")
KNOB_BOARD_H = os.path.join(ROOT, "knob", "components", "knob_board",
                            "include", "knob_board.h")
KNOB_DISP_C = os.path.join(ROOT, "knob", "components", "knob_board",
                           "knob_board_display.c")
KNOB_UI_C = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_ui.c")
KNOB_RENDER_C = os.path.join(ROOT, "knob", "components", "knob_ui",
                             "knob_moon_render.c")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # S3 identity
VERM = "#c2410c"     # P4 identity
VIOLET = "#7c3aed"   # knob identity
TEAL = "#0e7490"     # one DMA transaction (perf_bus's teal)
ORANGE = "#b5643a"   # reveal states (flush-path's reveal colour)
AMBER = "#b07d10"    # the relief wall


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — the "
                         f"build tree / managed_components must exist for "
                         f"this figure (build the target first)")
    with open(path, encoding="utf-8") as f:
        return f.read()


def must(pattern: str, path: str, what: str, flags: int = 0) -> re.Match:
    m = re.search(pattern, _read(path), flags)
    if not m:
        raise SystemExit(f"{what}: pattern not found in "
                         f"{os.path.relpath(path, ROOT)}")
    return m


def hdr_define(path: str, symbol: str, pattern: str = r"\s+(\d+)") -> int:
    m = re.search(rf"#define {symbol}{pattern}", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.relpath(path, ROOT)}")
    return int(m.group(1))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    # ---- the disks and the strip/reveal geometry, parsed -----------------
    m = must(r"#if defined\(CONFIG_BOARD_P4\)\n#define MOON_PX (\d+)\n"
             r"#else\n#define MOON_PX (\d+)\n#endif", UI_C,
             "MOON_PX pair (the #if CONFIG_BOARD_P4 block)")
    moon_p4, moon_s3 = int(m.group(1)), int(m.group(2))
    if "#define MOON_R ((float)MOON_PX / 2.0f)" not in _read(UI_C):
        raise SystemExit("MOON_R is no longer MOON_PX/2 — the disk shape "
                         "this figure assumes changed; re-read before plotting")
    m = must(r"#if defined\(CONFIG_BOARD_P4\)\n#define MOON_STRIP_ROWS (\d+)\n"
             r"#else\n#define MOON_STRIP_ROWS (\d+)\n#endif", UI_C,
             "MOON_STRIP_ROWS pair")
    strip_p4, strip_s3 = int(m.group(1)), int(m.group(2))
    m = must(r"#if defined\(CONFIG_BOARD_P4\)\n#define MOON_REVEAL_ROWS (\d+)\n"
             r"#else\n#define MOON_REVEAL_ROWS (\d+)\n#endif", UI_C,
             "MOON_REVEAL_ROWS pair")
    reveal_p4, reveal_s3 = int(m.group(1)), int(m.group(2))
    # the code guards these with #error — the figure refuses to lie without them
    for px, rows, name in ((moon_s3, strip_s3, "S3 MOON_STRIP_ROWS"),
                           (moon_p4, strip_p4, "P4 MOON_STRIP_ROWS"),
                           (moon_s3, reveal_s3, "S3 MOON_REVEAL_ROWS"),
                           (moon_p4, reveal_p4, "P4 MOON_REVEAL_ROWS")):
        if px % rows:
            raise SystemExit(f"{name}: MOON_PX no longer divides evenly — "
                             f"the code's own #error guard broke")
    p4_dpi_rows = must(r"always writes the BSP's (\d+)-row buffer strips",
                       UI_C, "P4 cited DPI buffer rows (moon_ui.c comment)"
                       ).group(1)
    p4_dpi_rows = int(p4_dpi_rows)

    # ---- the panels and the one real DMA transaction, parsed -------------
    s3_w = hdr_define(BSP_DISPLAY_H, "BSP_LCD_H_RES", r" \((\d+)\)")
    s3_h = hdr_define(BSP_DISPLAY_H, "BSP_LCD_V_RES", r" \((\d+)\)")
    if s3_w != s3_h:
        raise SystemExit(f"S3 panel {s3_w}x{s3_h} is not square")
    s3_xfer_rows = must(r"\.max_transfer_sz = BSP_LCD_H_RES \* (\d+) \* "
                        r"BSP_LCD_BITS_PER_PIXEL / 8", BSP_C,
                        "S3 max_transfer_sz formula").group(1)
    s3_buf_rows = must(r"\.buffer_height = (\d+),", BSP_C,
                       "S3 buffer_height").group(1)
    if s3_xfer_rows != s3_buf_rows:
        raise SystemExit(f"S3 strip rows {s3_xfer_rows} != buffer_height "
                         f"{s3_buf_rows} — the 'one strip = one transaction' "
                         f"fact broke")
    s3_tx_rows = int(s3_buf_rows)
    m = must(r"#define CONFIG_BSP_LCD_TYPE_(\d+)_(\d+)_4_INCH", CFG_P4,
             "P4 panel size (build the P4 target)")
    p4_w, p4_h = int(m.group(1)), int(m.group(2))
    if p4_w != p4_h:
        raise SystemExit(f"P4 panel {p4_w}x{p4_h} is not square")
    k_w = hdr_define(KNOB_BOARD_H, "KNOB_LCD_H_RES")
    k_h = hdr_define(KNOB_BOARD_H, "KNOB_LCD_V_RES")
    if k_w != k_h:
        raise SystemExit(f"knob panel {k_w}x{k_h} is not square")
    must(r"\.max_transfer_sz = KNOB_LCD_H_RES \* KNOB_LCD_ROWS_PER_BUF"
         r" \* sizeof\(uint16_t\)", KNOB_DISP_C,
         "knob max_transfer_sz formula (shape)")
    k_tx_rows = hdr_define(KNOB_BOARD_H, "KNOB_LCD_ROWS_PER_BUF")

    # ---- the knob's square + drawn disk, parsed ---------------------------
    must(r"#define MOON_SIZE KNOB_LCD_H_RES", KNOB_UI_C,
         "the knob's bake square is no longer the panel "
         "(#define MOON_SIZE KNOB_LCD_H_RES)")
    must(r"static uint16_t \*s_buf\[2\];", KNOB_UI_C,
         "the knob's RGB565 double buffer moved (knob_ui.c:97)")
    must(r"one atomic band lv_refr_now per completed row", KNOB_UI_C,
         "the knob's per-ROW reveal rule was reworded (knob_ui.c:31)")
    m = must(r"knob_ui_set_disk_geom\((\d+(?:\.\d+)?)f, KNOB_LCD_H_RES / 2\)",
             KNOB_RENDER_C, "the shared-home disk geometry call")
    knob_disk_px = 2.0 * float(m.group(1))

    # ---- the source worlds, parsed ----------------------------------------
    src = {}
    for board in ("s3", "p4", "knob"):
        hdr = os.path.join(GEN, board, "moon_consts.h")
        alb_w = hdr_define(hdr, "MOON_ALB_W")
        alb_h = hdr_define(hdr, "MOON_ALB_H")
        if alb_w != 2 * alb_h:
            raise SystemExit(f"{board} albedo {alb_w}x{alb_h} is not the "
                             f"2:1 equirectangular the sampler assumes")
        src[board] = (alb_w * alb_h,
                      hdr_define(hdr, "MOON_DEM_W") * hdr_define(hdr, "MOON_DEM_H"))

    disks = {"s3": (moon_s3, s3_w, BLUE), "p4": (moon_p4, p4_w, VERM),
             "knob": (k_w, k_w, VIOLET)}
    for board, (sq, panel, _c) in disks.items():
        if sq * sq > panel * panel:
            raise SystemExit(f"{board}: the bake square {sq}² no longer fits "
                             f"the panel {panel}² — re-read before plotting")

    # ---- left panel: the work one published frame touches -----------------
    def frame_rows(board: str) -> list[tuple]:
        """The clock-build shape: bake -> gather -> while-black disk flush
        -> whole-panel flush, then the transaction and one reveal state."""
        sq, panel, color = disks[board]
        bake = sq * sq
        return [
            (f"{board} · bake", bake, color, False,
             "MOON_PX² — one albedo tap + one packed relief cell each"),
            (f"{board} · publish/convert", bake, color, False,
             "gray8 read + RGB565 write, one gather"),
            (f"{board} · hourly disk flush", bake, TEAL, False,
             "the while-black flush — perf_bus's violet bar ÷ 2"),
            (f"{board} · whole-panel flush", panel * panel, color, False,
             f"screen switch — moon image square {(sq / panel) ** 2 * 100:.0f} % of it"),
        ]

    def tx_reveal_rows(board: str, w: int, tx_rows: int, tx_hatched: bool,
                       tx_ann: str, px: int, reveal_rows: int, strips: int | None) -> list[tuple]:
        out = [(f"{board} · one DMA transaction", w * tx_rows, TEAL,
                tx_hatched, tx_ann)]
        if strips is not None:
            out[0] = (out[0][0], out[0][1], TEAL, tx_hatched,
                      f"{tx_ann} · {strips} strips per panel")
        out.append((f"{board} · one reveal state", px * reveal_rows, ORANGE,
                    False, f"MOON_PX × MOON_REVEAL_ROWS {reveal_rows} — per "
                           f"SHOWN state"))
        return out

    left = []
    left += frame_rows("s3")
    left += tx_reveal_rows(
        "s3", s3_w, s3_tx_rows, False,
        f"{s3_w}×{s3_tx_rows} = max_transfer_sz/2 exactly",
        moon_s3, reveal_s3, -(-s3_h // s3_tx_rows))
    left += frame_rows("p4")
    left += tx_reveal_rows(
        "p4", p4_w, p4_dpi_rows, True,
        f"CITED {p4_dpi_rows}-row DPI buffer (xc BSP absent)",
        moon_p4, reveal_p4, None)
    # the knob: no clock relay (moon_ui.c excluded) — bake square IS the
    # panel (MOON_SIZE = KNOB_LCD_H_RES), RGB565 straight to the back
    # buffer, no hourly, no SD socket (the bus ladder's *no such leg*)
    left += [
        ("knob · bake square", k_w * k_w, VIOLET, False,
         "MOON_SIZE² — the bake square IS the panel (knob_ui.c:79)"),
        ("knob · publish/convert", None, None, False,
         "no such leg — the bake writes RGB565 straight into the back "
         "buffer (knob_ui.c:368)"),
        ("knob · hourly disk flush", None, None, False,
         "no such leg — no hourly, no SD socket"),
        ("knob · whole-panel flush", k_w * k_w, VIOLET, False,
         f"home IS the square — drawn disk {knob_disk_px:.0f} px = "
         f"{(knob_disk_px / k_w) ** 2 * 100:.0f} % of it"),
        ("knob · one DMA transaction", k_w * k_tx_rows, TEAL, False,
         f"{k_w}×{k_tx_rows} = max_transfer_sz/2 exactly · "
         f"{-(-k_w // k_tx_rows)} strips per panel"),
        ("knob · one reveal state", k_w * 1, ORANGE, False,
         "ONE row per shown state (knob_ui.c:31)"),
    ]

    # ---- right panel: the pixel universe behind one moon ------------------
    right = []
    for board in ("s3", "p4", "knob"):
        sq, panel, color = disks[board]
        alb, dem = src[board]
        right.append((f"{board} · albedo", alb, AMBER, False,
                      f"{alb:,} texels — NN-tapped, ≤1 per bake pixel"))
        right.append((f"{board} · DEM = relief pack", dem, AMBER, False,
                      f"{dem:,} × 2 B = {dem * 2 / 1024 / 1024:.0f} MB wall · "
                      f"{dem / (sq * sq):.1f}× the bake — ONE cell per pixel"))
        right.append((f"{board} · bake square", sq * sq, color, False,
                      f"{sq}² — the mill's canvas"))
        right.append((f"{board} · panel", panel * panel, INK, False,
                      f"{panel}² — everything the flush must own"))

    fig, axes = plt.subplots(1, 2, figsize=(13.5, 6.4))

    ax = axes[0]
    for y, (label, n, color, hatched, ann) in enumerate(left):
        if n is None:
            ax.text(0.012, y, ann, va="center", fontsize=6.4, color=MUTED,
                    style="italic", transform=ax.get_yaxis_transform())
            continue
        if hatched:
            ax.barh(y, n, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, n, color=color, alpha=0.55, height=0.6,
                    edgecolor=color,
                    linestyle="--" if color == ORANGE else "-")
        ax.text(n * 1.12, y, f"{n:,} · {ann}", va="center", fontsize=6.2,
                color=MUTED)
    ax.set_yticks(range(len(left)), [r[0] for r in left], fontsize=7)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(100, 1_000_000_000)
    ax.set_xlabel("pixels (log) — counted work, not time, not bytes")
    ax.set_title("the pixel ladder — how many pixels every stage touches\n"
                 "(one published frame; hatched = cited, dashed = per shown "
                 "state, italic = no such leg)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    ax = axes[1]
    for y, (label, n, color, hatched, ann) in enumerate(right):
        ax.barh(y, n, color=color, alpha=0.55, height=0.6, edgecolor=color)
        ax.text(n * 1.12, y, f"{n:,} · {ann}", va="center", fontsize=6.2,
                color=MUTED)
    ax.set_yticks(range(len(right)), [r[0] for r in right], fontsize=7)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(50_000, 500_000_000)
    ax.set_xlabel("pixels (log) — the source world behind one moon")
    ax.set_title("the pixel universe — what each bake square samples from\n"
                 "(amber = the asset wall, identity = the work surface)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=150)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
