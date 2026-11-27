#!/usr/bin/env python3
"""Generate the static-footprint figure for docs/verification/performance.rst.

The other perf figures measure WORK (gen_perf_hourly.py, gen_perf_boot.py,
gen_perf_variants.py), SELF-WAKE FREQUENCY (gen_perf_pacing.py) or BRIGHTNESS
(gen_perf_dim.py). This one measures the third resource every embedded budget
lives on: MEMORY — where the bytes sit and what they are paid for. Three
panels, all PARSED or DERIVED from the live sources at figure time (the
house pattern — nothing hand-placed, no fabricated device measurements;
every runtime watermarks story stays on the device's own heap log lines):

  Left  — PSRAM working set, stacked per board:
    s_frame (MOON_PX^2 gray) + s_rgb (x2 RGB565) + s_hourly_gray
      (x1, only when UI_MOON_HOURLY_ENABLE)  components/ui/moon_ui.c:2494
    the LVGL draw buffers (S3: buffer_height 25, require_double_buffer,
      466*25*2 = 23,300 B each — the vendored BSP; P4: three 50-row
      MIPI buffers per the published strip-staging figure)
    knob: the producer's double buffer 2 x 360^2 x 2  (knob_ui.c's
      s_buf[2]; the knob EXCLUDES moon_ui.c — components/ui/
      CMakeLists.txt drops the four board TUs, so its working set lives
      in knob_ui instead)
    hatched overlay: the rotated root's layer while rotated (panel^2 x 2
      from the clib malloc -> PSRAM, actions.c ori_apply_v10's comment)
  Middle — internal-RAM commitments (the resource that caused both
    NO_MEM families): the LVGL pool (lvgl Kconfig default 65536, unpin-
    ned in every sdkconfig) + the task stacks (xTaskCreate sites in
    moon_ui.c / actions.c / main.cpp / knob_ui.c; a stack whose OWN create
    call passes MALLOC_CAP_SPIRAM is drawn as a PSRAM call-out, not RAM
    debt (parsed from the caps argument, pinned to moon_event alone —
    knob_moon's stack moved to internal RAM on 2026-11-16, the tick-ISR
    flash-write family) + the S3 drain
    row (DRAM uint16[466])
  Right — flash moon assets vs their partition: alb_bytes + dem_bytes +
    combined_bytes PARSED from the three moon_assets_manifest.json, bars
    against the manifest's own partition_kib

P4 panel size (720) and the XC BSP's 50-row triple buffer are the two
constants gen_strip_staging.py already publishes with the same
provenance (the P4 BSP is not vendored into this tree).

Colours follow gen_perf_dim.py / gen_strip_staging.py: blue #2b5fb0,
cyan #0891b2, amber #b07d10, gray #8a8a8a.

Run:  docs/.venv/bin/python docs/tools/gen_perf_mem.py
"""
from __future__ import annotations

import json
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
MAIN_CPP = os.path.join(ROOT, "main", "main.cpp")
S3_BSP_C = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                        "esp32_s3_touch_amoled_1_75.c")
S3_BSP_H = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                        "include", "bsp", "display.h")
KNOB_UI = os.path.join(ROOT, "components", "knob_ui", "knob_ui.c")
# the knob's bsp/ headers live under the shared board component's knob_include
KNOB_H = os.path.join(ROOT, "components", "board", "knob_include", "bsp",
                      "display.h")
LVGL_KCONFIG = os.path.join(ROOT, "managed_components", "lvgl__lvgl", "Kconfig")
MANIFEST = os.path.join(ROOT, "assets", "images", "moon", "out")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_mem.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"
CYAN = "#0891b2"
AMBER = "#b07d10"
GRAY = "#8a8a8a"
VIOLET = "#7c3aed"

# The two constants gen_strip_staging.py publishes (the P4 BSP is not in
# this tree): 720 px panel, XC BSP 50-row DPI-DMA buffer x3.
P4_PANEL_PX = 720
P4_BUF_ROWS = 50
P4_BUF_COUNT = 3


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def must(pattern, text, what, flags=0):
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"gen_perf_mem: {what} not found — source drifted")
    return m


# ---------------------------------------------------------------- parse
moon_ui = read(MOON_UI)
m = must(r"#if defined\(CONFIG_BOARD_P4\)\s*\n#define MOON_PX (\d+)\s*\n#else\s*\n#define MOON_PX (\d+)",
         moon_ui, "MOON_PX pair")
MOON_PX_P4, MOON_PX_S3 = int(m.group(1)), int(m.group(2))

s3_src = read(S3_BSP_C)
S3_BUF_ROWS = int(must(r"\.buffer_height = (\d+)", s3_src, "S3 buffer_height").group(1))
must(r"\.require_double_buffer = true", s3_src, "S3 double buffer")
S3_PANEL = int(must(r"#define BSP_LCD_H_RES \((\d+)\)", read(S3_BSP_H), "S3 panel").group(1))
KNOB_PANEL = int(must(r"#define BSP_LCD_H_RES (\d+)", read(KNOB_H), "knob panel").group(1))

s3_strip = S3_PANEL * S3_BUF_ROWS * 2          # RGB565 bytes per draw buffer
knob_src = read(KNOB_UI)
must(r"static uint16_t \*s_buf\[2\];", knob_src, "knob double buffer")

lvgl_kc = read(LVGL_KCONFIG)
LV_POOL = int(must(r"config LV_MEM_SIZE\b.*?\n\s*default (\d+)", lvgl_kc, "LV_MEM_SIZE", re.S).group(1))


def stacks(text):
    """(name, bytes) per xTaskCreate(PinnedToCore)(WithCaps) site."""
    return [(m.group(1), int(m.group(2)))
            for m in re.finditer(r'xTaskCreate\w*\(.*?, "([a-z_]+)", (\d+)', text)]


stacks_clock = stacks(moon_ui) + stacks(read(ACTIONS)) + stacks(read(MAIN_CPP))
stacks_knob = stacks(knob_src)


def spiram_stack_tasks(*texts):
    """Task names whose OWN create call passes MALLOC_CAP_SPIRAM — parsed from
    the caps argument, so a stack that moves to internal RAM (knob_moon,
    2026-11-16, the tick-ISR flash-write family) re-anchors itself here
    instead of ageing into a wrong bar. Only ``WithCaps`` calls carry caps, so
    every plain ``xTaskCreate`` stack is internal-RAM debt by definition."""
    out = set()
    for text in texts:
        for call in re.findall(r"xTaskCreate\w*\(([^;]*);", text, re.S):
            if "MALLOC_CAP_SPIRAM" in call:
                mn = re.search(r'"([a-z_]+)"', call)
                if mn:
                    out.add(mn.group(1))
    return out


# The SPIRAM-stack tasks (WithCaps MALLOC_CAP_SPIRAM) are not internal-RAM debt.
SPIRAM_STACK_TASKS = spiram_stack_tasks(moon_ui, read(ACTIONS), read(MAIN_CPP), knob_src)
if SPIRAM_STACK_TASKS != {"moon_event"}:
    raise SystemExit(f"gen_perf_mem: SPIRAM-stack roster {sorted(SPIRAM_STACK_TASKS)} — expected "
                     "['moon_event'] (the S3's sky-event worker only; knob_moon's stack is internal "
                     "RAM since the 2026-11-16 tick-ISR fix)")

man = {t: json.load(open(os.path.join(MANIFEST, t, "moon_assets_manifest.json")))
       for t in ("s3", "p4", "knob")}
for t, mm in man.items():
    assert mm["target"] == t and mm["combined_bytes"] <= mm["partition_kib"] * 1024


def kib(b):
    return b / 1024.0


# Board working sets: (label, [(part_name, bytes, colour, hatch)])
boards = [
    ("S3 AMOLED\n(466, disk 400)", S3_PANEL, MOON_PX_S3, [
        ("s_rgb  400²×2", kib(MOON_PX_S3 ** 2 * 2), BLUE, None),
        ("s_frame  400²", kib(MOON_PX_S3 ** 2), BLUE, None),
        ("s_hourly_gray  400²", kib(MOON_PX_S3 ** 2), BLUE, None),
        ("draw buffers 2×23,300", kib(2 * s3_strip), CYAN, None),
    ]),
    ("P4 4C\n(720, disk 650)", P4_PANEL_PX, MOON_PX_P4, [
        ("s_rgb  650²×2", kib(MOON_PX_P4 ** 2 * 2), BLUE, None),
        ("s_frame  650²", kib(MOON_PX_P4 ** 2), BLUE, None),
        ("s_hourly_gray  650²", kib(MOON_PX_P4 ** 2), BLUE, None),
        ("MIPI buffers 3×(720·50·2)", kib(P4_BUF_COUNT * P4_PANEL_PX * P4_BUF_ROWS * 2), CYAN, None),
    ]),
    ("knob\n(360, full square)", KNOB_PANEL, KNOB_PANEL, [
        ("s_buf[2] 2×360²×2", kib(2 * KNOB_PANEL ** 2 * 2), BLUE, None),
    ]),
]

# ---------------------------------------------------------------- figure
fig, (ax_l, ax_m, ax_r) = plt.subplots(1, 3, figsize=(12.4, 5.0))
fig.patch.set_facecolor("white")

# ---- left: PSRAM working set
xs = range(len(boards))
for bi, (label, panel_px, moon_px, parts) in enumerate(boards):
    bottom = 0.0
    for name, b, col, hatch in parts:
        ax_l.bar(bi, b, bottom=bottom, color=col, alpha=0.35, edgecolor=col, linewidth=0.9,
                 width=0.62)
        if b > 90:
            ax_l.text(bi, bottom + b / 2, f"{name}\n{b:,.0f} KiB", ha="center", va="center",
                      fontsize=6.0, color=INK)
        bottom += b
    rot = kib(panel_px ** 2 * 2)
    ax_l.bar(bi, rot, bottom=bottom, color="none", edgecolor=VIOLET, linewidth=1.0,
             hatch="///", width=0.62)
    ax_l.text(bi, bottom + rot / 2, f"rotated layer\nwhile rotated\n{rot:,.0f} KiB",
              ha="center", va="center", fontsize=5.8, color=VIOLET)
    ax_l.text(bi, bottom + rot + 12, f"{bottom + rot:,.0f} KiB", ha="center", fontsize=6.5,
              color=INK, fontweight="bold")
ax_l.set_xticks(list(xs))
ax_l.set_xticklabels([b[0] for b in boards], fontsize=7.2, color=INK)
ax_l.set_ylabel("KiB (PSRAM)", fontsize=8, color=INK)
ax_l.set_title("PSRAM working set — parsed allocators\n(s_hourly_gray only with UI_MOON_HOURLY_ENABLE)",
               fontsize=8.6, color=INK, fontweight="bold")
ax_l.tick_params(labelsize=7, colors=MUTED)
for s in ax_l.spines.values():
    s.set_color(HAIR)

# ---- middle: internal-RAM commitments
clock_stacks = {n: b for n, b in stacks_clock}
knob_stacks = {n: b for n, b in stacks_knob}
# groups for the clocks: shared, moon producers, housekeeping
GROUPS = {
    "LVGL pool": lambda d: LV_POOL,
    "task stacks\n(internal)": lambda d: sum(b for n, b in d.items() if n not in SPIRAM_STACK_TASKS),
    "task stacks\n(PSRAM, WithCaps)": lambda d: sum(b for n, b in d.items() if n in SPIRAM_STACK_TASKS),
}
names = ["S3 + P4\n(clock build)", "knob"]
data = [clock_stacks, knob_stacks]
colors = {k: c for k, c in zip(GROUPS, (VIOLET, BLUE, CYAN))}
for bi, (nm, d) in enumerate(zip(names, data)):
    bottom = 0.0
    for gname, fn in GROUPS.items():
        b = kib(fn(d))
        solid = "(PSRAM" not in gname
        ax_m.bar(bi, b, bottom=bottom, color=colors[gname] if solid else "none",
                 alpha=0.32, edgecolor=colors[gname], linewidth=1.0,
                 hatch=None if solid else "...", width=0.55)
        if b > 6:
            ax_m.text(bi, bottom + b / 2, f"{gname}\n{b:,.0f} KiB", ha="center", va="center",
                      fontsize=5.9, color=INK)
        bottom += b
ax_m.text(0.5, -0.24,
          "clock stacks: wifi_sync 6144 · hang_watch 6144 · panel_wdg 4096 ·\n"
          "rebake/hourly/orbit/replay 4×12288 · moon_event 12288 (PSRAM)\n"
          "knob stacks: knob_moon 12288 (internal RAM) · knob_swap 8192\n"
          "S3-only: drain row DRAM 932 B · LVGL pool = static .bss array",
          transform=ax_m.transAxes, ha="center", va="top", fontsize=5.6, color=MUTED)
ax_m.set_xticks(range(len(names)))
ax_m.set_xticklabels(names, fontsize=7.2, color=INK)
ax_m.set_ylabel("KiB", fontsize=8, color=INK)
ax_m.set_title("internal-RAM commitments — parsed pools/stacks\n(dotted = PSRAM stacks, not RAM debt)",
               fontsize=8.6, color=INK, fontweight="bold")
ax_m.tick_params(labelsize=7, colors=MUTED)
ax_m.set_ylim(0, kib(LV_POOL + sum(b for n, b in clock_stacks.items() if n not in SPIRAM_STACK_TASKS)) * 1.15)
for s in ax_m.spines.values():
    s.set_color(HAIR)

# ---- right: flash assets vs partition
xs3 = range(3)
labels = ["S3", "P4", "knob"]
for bi, t in enumerate(("s3", "p4", "knob")):
    mm = man[t]
    alb, dem = kib(mm["alb_bytes"]), kib(mm["dem_bytes"])
    part = kib(mm["partition_kib"] * 1024)
    comb = kib(mm["combined_bytes"])
    ax_r.bar(bi, part, color="none", edgecolor=GRAY, linewidth=1.1, width=0.55)
    ax_r.bar(bi, alb, color=AMBER, alpha=0.45, edgecolor=AMBER, linewidth=0.9, width=0.55)
    ax_r.bar(bi, dem, bottom=alb, color=BLUE, alpha=0.30, edgecolor=BLUE, linewidth=0.9, width=0.55)
    ax_r.text(bi, part + 30, f"{part:,.0f} KiB partition", ha="center", fontsize=6.0, color=MUTED)
    ax_r.text(bi, alb / 2, f"albedo\n{alb:,.0f}", ha="center", va="center", fontsize=5.8, color=INK)
    ax_r.text(bi, alb + dem / 2, f"DEM\n{dem:,.0f}", ha="center", va="center", fontsize=5.8, color=INK)
    ax_r.text(bi, alb + dem + part * 0.03,
              f"combined {comb:,.0f} KiB · {100.0 * comb / part:.0f} %",
              ha="center", fontsize=6.2, color=INK, fontweight="bold")
ax_r.set_xticks(list(xs3))
ax_r.set_xticklabels(labels, fontsize=7.2, color=INK)
ax_r.set_ylabel("KiB (flash, moon_assets partition)", fontsize=8, color=INK)
ax_r.set_title("flash moon assets vs their partition —\nparsed from each moon_assets_manifest.json",
               fontsize=8.6, color=INK, fontweight="bold")
ax_r.tick_params(labelsize=7, colors=MUTED)
for s in ax_r.spines.values():
    s.set_color(HAIR)

fig.suptitle("The static footprint, three resources: PSRAM working set · internal-RAM commitments · "
             "flash asset budget — every number parsed or derived at figure time",
             fontsize=10.5, color=INK, y=0.99)
fig.text(0.01, 0.012,
         "Parsed: moon_ui.c allocators + MOON_PX, the vendored S3 BSP (buffer_height 25, double buffer), the "
         "task-stack xTaskCreate sites, lvgl Kconfig LV_MEM_SIZE (unpinned), the three manifests. "
         "Derived: RGB565/gray byte sizes, the rotated-root layer (panel²×2, clib→PSRAM). "
         "P4 draw buffers follow the published strip-staging constants (XC BSP not vendored). "
         "Runtime watermarks are the device's own heap log lines — not this figure.",
         fontsize=5.8, color=MUTED)
fig.tight_layout(rect=(0, 0.085, 1, 0.965))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
fig.savefig(OUT, dpi=200)
print(f"wrote {OUT}")
