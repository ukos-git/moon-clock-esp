#!/usr/bin/env python3
"""Generate the display-bus ladder for docs/verification/performance.rst.

The other ladders count time (pacing, patience, response), brightness,
memory, loudness and priorities. This one counts BYTES: what one panel
refresh actually pushes across the display bus on each of the three
boards, and what the raw wire costs for it against the 33 ms LVGL beat.
Three rows per board on each panel — the full RGB565 frame (what one
atomic ``lv_refr_now`` crosses), the hourly disk-only flush (the
``MOON_PX``² frame the while-black producer lands), and ONE DMA
transaction (the strip the bus really sees). The story the two QSPI
boards and the DSI board tell against each other: the S3's and the
knob's transactions must fit under the S3 family's 32 KiB SPI-DMA cap
(both BSPs' own comments cite the clock board's NO_MEM lesson), while
the P4's 50-row DPI buffer transaction sits comfortably ABOVE the cap
line — a cap its bus never has.

Every number is PARSED from the live sources at figure time (the house
pattern); the only non-parsed length is the P4 buffer height, which is
CITED (hatched) because the XC BSP is target-gated — managed_components/
carries it only while a P4 build is the most recent component solve:

  #define BSP_LCD_BITS_PER_PIXEL (16)   components/esp32_s3_touch_amoled_1_75/
      include/bsp/display.h:16  (+ BSP_LCD_H_RES/:V_RES :20/:21)
  .max_transfer_sz = BSP_LCD_H_RES * 25 * BSP_LCD_BITS_PER_PIXEL / 8
      components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c:501
      and .buffer_height = 25  same file :530 (asserted equal — the
      "one strip = one buffer = one QSPI transfer" fact of the NO_MEM fix)
  CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK, BSP_LCD_DATA0..DATA3, ...)
      same file :407 — four data lanes, the x4 in "QSPI"
  .pclk_hz = 40 * 1000 * 1000           managed_components/espressif__esp_lcd_co5300/
      include/esp_lcd_co5300.h:136 (inside CO5300_PANEL_IO_QSPI_CONFIG :131)
  CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH 1    build/config/sdkconfig.h:1006
      (set at esp32_s3_touch_amoled_1_75.c:412)
  CONFIG_BSP_LCD_TYPE_720_720_4_INCH    build/p4/config/sdkconfig.h:1387
      (curated sdkconfig.defaults.esp32p4:39) — the P4 panel size
  CONFIG_BSP_LCD_DPI_BUFFER_NUMS 1      build/p4/config/sdkconfig.h:1384
  the BSP's 50-row buffer strips        components/ui/moon_ui.c:193
      (CITED from the moon_ui.c comment; the xc BSP is target-gated — its 50 rows live at esp32_p4_wifi6_touch_lcd_xc.c:1013 when a P4 build populates the tree)
  #define MOON_PX 650 / 400             components/ui/moon_ui.c:175/:177
      (the hourly disk-only flush is MOON_PX² * 2 B)
  #define KNOB_LCD_H_RES 360, KNOB_LCD_ROWS_PER_BUF 30
      knob/components/knob_board/include/knob_board.h:22/:27
  .max_transfer_sz = KNOB_LCD_H_RES * KNOB_LCD_ROWS_PER_BUF * sizeof(uint16_t)
      knob/components/knob_board/knob_board_display.c:243
  .data0_io_num .. .data3_io_num        same file :237 — four lanes
  .pclk_hz = 40 * 1000 * 1000           knob/managed_components/espressif__esp_lcd_sh8601/
      include/esp_lcd_sh8601.h:102 (inside SH8601_PANEL_IO_QSPI_CONFIG :97)
  CONFIG_LV_DEF_REFR_PERIOD 33          build/config/sdkconfig.h:1087,
      build/p4/config/sdkconfig.h:1308, /home/matthias/esp/build/knob/config/
      sdkconfig.h:1086 — parsed from ALL THREE and asserted equal (the same
      beat gen_perf_response.py and gen_perf_figs assert)

Derived (one division / one product, nothing new measured): the raw
wire floor = bytes / (lanes * pclk / 8) — the QSPI x4 panels get
4 * 40 MHz / 8 = 20 MB/s; the P4 row is NOT plotted (the MIPI-DSI
lane rate lives in the absent XC BSP; inventing it would be the one
way to make this figure lie). The raw floor excludes per-transaction
command overhead — the vendored BSP's own comment: the 25-row split
"only the per-transaction command overhead goes up slightly".

Run:  docs/.venv/bin/python docs/tools/gen_perf_bus.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BSP_DIR = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75")
BSP_C = os.path.join(BSP_DIR, "esp32_s3_touch_amoled_1_75.c")
BSP_DISPLAY_H = os.path.join(BSP_DIR, "include", "bsp", "display.h")
CO5300_H = os.path.join(ROOT, "managed_components", "espressif__esp_lcd_co5300",
                        "include", "esp_lcd_co5300.h")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
KNOB_BOARD_H = os.path.join(ROOT, "knob", "components", "knob_board",
                            "include", "knob_board.h")
KNOB_DISP_C = os.path.join(ROOT, "knob", "components", "knob_board",
                           "knob_board_display.c")
SH8601_H = os.path.join(ROOT, "knob", "managed_components",
                        "espressif__esp_lcd_sh8601", "include",
                        "esp_lcd_sh8601.h")
CFG_S3 = os.path.join(ROOT, "build", "config", "sdkconfig.h")
CFG_P4 = os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h")
CFG_KNOB = os.path.join(ROOT, "..", "build", "knob", "config", "sdkconfig.h")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_bus.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # the full atomic frame (the invariant's pixels)
VIOLET = "#7c3aed"  # the hourly disk-only flush (the while-black leg)
TEAL = "#0e7490"    # one DMA transaction (what the bus really sees)
RED = "#b91c1c"     # the 32 KiB SPI-DMA transaction cap
AMBER = "#b07d10"   # the 33 ms LVGL beat


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"{os.path.relpath(path, ROOT)}: not found — "
                         f"the build tree / managed_components must exist "
                         f"for this figure (build the target first)")
    with open(path, encoding="utf-8") as f:
        return f.read()


def hdr_define(path: str, symbol: str, pattern: str = r"\s+(\d+)") -> int:
    m = re.search(rf"#define {symbol}{pattern}", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.relpath(path, ROOT)}")
    return int(m.group(1))


def regex_int(path: str, pattern: str, what: str, group: int = 1) -> int:
    m = re.search(pattern, _read(path), re.S)
    if not m:
        raise SystemExit(f"{what}: pattern not found in "
                         f"{os.path.relpath(path, ROOT)}")
    return int(m.group(group))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    # ---- S3: geometry, strip, lanes, pclk ------------------------------
    s3_h = hdr_define(BSP_DISPLAY_H, "BSP_LCD_H_RES", r" \((\d+)\)")
    s3_v = hdr_define(BSP_DISPLAY_H, "BSP_LCD_V_RES", r" \((\d+)\)")
    s3_bpp = hdr_define(BSP_DISPLAY_H, "BSP_LCD_BITS_PER_PIXEL", r" \((\d+)\)")
    s3_xfer_rows = regex_int(BSP_C, r"\.max_transfer_sz = BSP_LCD_H_RES \* "
                             r"(\d+) \* BSP_LCD_BITS_PER_PIXEL / 8",
                             "S3 max_transfer_sz formula")
    s3_buf_rows = regex_int(BSP_C, r"\.buffer_height = (\d+),", "S3 buffer_height")
    if s3_xfer_rows != s3_buf_rows:
        raise SystemExit(f"S3 strip rows {s3_xfer_rows} != buffer_height "
                         f"{s3_buf_rows} — the 'one strip = one transaction' "
                         f"fact broke")
    s3_lanes = len(re.findall(r"BSP_LCD_DATA\d",
                              _read(BSP_C).split("CO5300_PANEL_BUS_QSPI_CONFIG")[1][:400]))
    s3_pclk = regex_int(CO5300_H, r"#define CO5300_PANEL_IO_QSPI_CONFIG.*?"
                        r"\.pclk_hz = (\d+) \* 1000 \* 1000",
                        "CO5300 QSPI pclk (run an S3 build to populate "
                        "managed_components/)") * 10**6
    s3_qdepth = hdr_define(CFG_S3, "CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH")

    # ---- P4: panel size from the generated config; buffer CITED --------
    m = re.search(r"#define CONFIG_BSP_LCD_TYPE_(\d+)_(\d+)_", _read(CFG_P4))
    if not m:
        raise SystemExit("CONFIG_BSP_LCD_TYPE_<W>_<H>_: not found in "
                         "build/p4/config/sdkconfig.h — build the P4 first")
    p4_h, p4_v = int(m.group(1)), int(m.group(2))
    p4_dpi_bufs = hdr_define(CFG_P4, "CONFIG_BSP_LCD_DPI_BUFFER_NUMS")
    p4_buf_rows = regex_int(MOON_UI, r"always writes the BSP's (\d+)-row "
                            r"buffer strips", "P4 cited 50-row DPI buffer "
                            "(moon_ui.c comment)")

    # ---- knob: geometry, strip, lanes, pclk ----------------------------
    k_h = hdr_define(KNOB_BOARD_H, "KNOB_LCD_H_RES")
    k_v = hdr_define(KNOB_BOARD_H, "KNOB_LCD_V_RES")
    k_buf_rows = hdr_define(KNOB_BOARD_H, "KNOB_LCD_ROWS_PER_BUF")
    m = re.search(r"\.max_transfer_sz = KNOB_LCD_H_RES \* KNOB_LCD_ROWS_PER_BUF"
                  r" \* sizeof\(uint16_t\)", _read(KNOB_DISP_C))
    if not m:
        raise SystemExit("knob max_transfer_sz formula: not found in "
                         "knob_board_display.c:243")
    k_lanes = len(re.findall(r"\.data\d_io_num = LCD_PIN_D\d", _read(KNOB_DISP_C)))
    k_pclk = regex_int(SH8601_H, r"#define SH8601_PANEL_IO_QSPI_CONFIG.*?"
                       r"\.pclk_hz = (\d+) \* 1000 \* 1000",
                       "SH8601 QSPI pclk (run a knob build to populate "
                       "knob/managed_components/)") * 10**6

    # ---- shared: the hourly disk and the beat ---------------------------
    m = re.search(r"#if defined\(CONFIG_BOARD_P4\)\s*\n#define MOON_PX (\d+)\s*"
                  r"\n#else\s*\n#define MOON_PX (\d+)", _read(MOON_UI))
    if not m:
        raise SystemExit("MOON_PX pair: not found in moon_ui.c:174-177")
    moon_p4, moon_s3 = int(m.group(1)), int(m.group(2))
    beats = [hdr_define(p, "CONFIG_LV_DEF_REFR_PERIOD")
             for p in (CFG_S3, CFG_P4, CFG_KNOB)]
    if len(set(beats)) != 1:
        raise SystemExit(f"CONFIG_LV_DEF_REFR_PERIOD disagrees across the "
                         f"three generated headers: {beats}")
    beat = beats[0]

    # ---- derived rows ----------------------------------------------------
    s3_rate = s3_lanes * s3_pclk / 8      # B/s raw wire
    k_rate = k_lanes * k_pclk / 8

    # (label, bytes, color, hatched, annotation)
    left = [
        ("S3 full frame\n466x466x2 RGB565", s3_h * s3_v * s3_bpp // 8, BLUE, False,
         f"{s3_h}·{s3_v}·{s3_bpp//8} = one atomic lv_refr_now"),
        ("S3 hourly disk\nMOON_PX² x 2 B", moon_s3 * moon_s3 * 2, VIOLET, False,
         f"{moon_s3}² · 2 = the while-black flush (moon_ui.c:177)"),
        ("S3 one DMA transaction\n466 x 25 x 2", s3_h * s3_buf_rows * s3_bpp // 8,
         TEAL, False, f"= max_transfer_sz exactly (BSP :501/:530) · queue "
         f"depth {s3_qdepth}"),
        ("P4 full frame\n720x720x2 RGB565", p4_h * p4_v * 2, BLUE, False,
         f"{p4_h}·{p4_v}·2 (TYPE_{p4_h}_{p4_v}_, build/p4 header)"),
        ("P4 hourly disk\nMOON_PX² x 2 B", moon_p4 * moon_p4 * 2, VIOLET, False,
         f"{moon_p4}² · 2 = the while-black flush (moon_ui.c:175)"),
        ("P4 one DMA transaction\n720 x 50 x 2", p4_h * p4_buf_rows * 2, TEAL, True,
         f"CITED {p4_buf_rows}-row DPI buffer (moon_ui.c:193; xc BSP target-gated) · "
         f"DPI_BUFFER_NUMS {p4_dpi_bufs}"),
        ("knob full frame\n360x360x2 RGB565", k_h * k_v * 2, BLUE, False,
         f"{k_h}·{k_v}·2 (knob_board.h:22/:23)"),
        ("knob hourly disk", None, VIOLET, True,
         "no hourly leg — no SD socket, the knob never bakes to a card"),
        ("knob one DMA transaction\n360 x 30 x 2", k_h * k_buf_rows * 2, TEAL, False,
         f"= max_transfer_sz exactly (knob_board_display.c:243, "
         f"ROWS_PER_BUF {k_buf_rows})"),
    ]

    # (label, ms, color, hatched, annotation)
    right = [
        ("S3 full frame", s3_h * s3_v * s3_bpp // 8 / s3_rate * 1000, BLUE, False,
         f"{s3_h * s3_v * s3_bpp // 8 // 1024} KiB / {s3_rate // 10**6} MB/s "
         f"({s3_lanes} lanes x {s3_pclk // 10**6} MHz)"),
        ("S3 hourly disk", moon_s3 * moon_s3 * 2 / s3_rate * 1000, VIOLET, False,
         f"{moon_s3}²·2 / {s3_rate // 10**6} MB/s"),
        ("P4 full frame", None, BLUE, True,
         "not plotted — the DSI lane rate lives in the target-gated XC BSP"),
        ("P4 hourly disk", None, VIOLET, True,
         "not plotted — same reason (on-chip DPI DMA is a different bus)"),
        ("knob full frame", k_h * k_v * 2 / k_rate * 1000, BLUE, False,
         f"{k_h * k_v * 2 // 1024} KiB / {k_rate // 10**6} MB/s "
         f"({k_lanes} lanes x {k_pclk // 10**6} MHz)"),
        ("knob hourly disk", None, VIOLET, True, "no hourly leg"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.6, 4.6))

    # ---- (a) bytes per refresh -------------------------------------------
    ax = axes[0]
    for y, (label, n, color, hatched, ann) in enumerate(left):
        if n is None:
            ax.text(1.5, y, ann, va="center", fontsize=6.4, color=MUTED,
                    style="italic")
            continue
        kib = n / 1024
        if hatched:
            ax.barh(y, kib, color="white", height=0.62, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, kib, color=color, alpha=0.6, height=0.62,
                    edgecolor=color)
        ax.text(kib * 1.03 + 1, y, f"{n:,} B · {ann}", va="center",
                fontsize=6.2, color=MUTED)
    ax.axvline(32768 / 1024, color=RED, linestyle="--", linewidth=1.1)
    ax.text(32768 / 1024 + 2, -1.05,
            "32 KiB — the S3 family's SPI-DMA transaction cap "
            "(SPI_LL_DMA_MAX_BIT_LEN\n= 1<<18, vendored BSP :522). Both QSPI "
            "boards keep ONE strip under it;\nthe P4's DPI transaction sits "
            "above the line — a cap its bus never has.",
            fontsize=6.2, color=RED, va="top")
    ax.set_yticks(range(len(left)), [r[0] for r in left], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(left) - 0.5, -1.7)
    ax.set_xlim(0, 1080)
    ax.set_xlabel("bytes per refresh, KiB — what one panel update crosses the bus")
    ax.set_title("the bus ladder — parsed at figure time\n"
                 "(hatched = cited / absent-BSP; italic = no such leg)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) raw wire floor vs the beat ----------------------------------
    ax = axes[1]
    for y, (label, ms, color, hatched, ann) in enumerate(right):
        if ms is None:
            ax.text(0.6, y, ann, va="center", fontsize=6.4, color=MUTED,
                    style="italic")
            continue
        ax.barh(y, ms, color=color, alpha=0.6, height=0.62, edgecolor=color)
        ax.text(ms * 1.03, y, f"{ms:.1f} ms · {ms / beat:.2f} beat · {ann}",
                va="center", fontsize=6.2, color=MUTED)
    ax.axvline(beat, color=AMBER, linestyle="--", linewidth=1.2)
    ax.set_yticks(range(len(right)), [r[0] for r in right], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(right) - 0.5, -1.9)
    ax.text(beat * 1.04, -1.1,
            f"the beat: CONFIG_LV_DEF_REFR_PERIOD = {beat} ms, parsed and "
            f"asserted equal in ALL\nTHREE generated sdkconfig.h — the "
            f"whole atomic panel crosses the wire inside\none beat, with "
            f"thirds left over",
            fontsize=6.2, color=AMBER, va="top")
    ax.set_xlim(0, 52)
    ax.set_xlabel("raw wire floor, ms — bytes / (lanes·pclk/8); excludes "
                  "per-transaction command overhead")
    ax.set_title("what the QSPI x4 wire needs for one frame\n"
                 "(the whole atomic panel lands inside one beat)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(S3 {s3_h}x{s3_v}@{s3_bpp}bpp strip {s3_buf_rows} lanes {s3_lanes} "
          f"pclk {s3_pclk//10**6} MHz q{ s3_qdepth}; "
          f"P4 {p4_h}x{p4_v} buf {p4_buf_rows} dpi{p4_dpi_bufs}; "
          f"knob {k_h}x{k_v} strip {k_buf_rows} lanes {k_lanes} "
          f"pclk {k_pclk//10**6} MHz; moon {moon_s3}/{moon_p4}; beat {beat}; "
          f"S3 wire {s3_h*s3_v*s3_bpp//8/s3_rate*1000:.1f} ms, "
          f"knob {k_h*k_v*2/k_rate*1000:.1f} ms)")


if __name__ == "__main__":
    main()
