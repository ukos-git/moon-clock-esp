#!/usr/bin/env python3
"""Generate the interactive-pacing ladder for docs/verification/performance.rst.

The perf figures so far measure WORK per event (the hourly refresh,
gen_perf_hourly.py; boot, gen_perf_boot.py; per-board throughput,
gen_perf_variants.py). This one measures the other axis: how often the
device WAKES ITSELF — every period the shipped firmware runs a timer
at, on one log-ms axis, so the reader can see at a glance that the
panel's 50 ms reveal tick and the 1300 ms RTC poll are two orders of
magnitude apart, and where the idle ladder's 1 Hz heartbeat sits.

Every number is PARSED from the live sources at figure time (the
house pattern — compute from the device's own arithmetic, never
hand-place):

  task_min_delay_ms = 10           components/esp32_s3_touch_amoled_1_75
                                   (the LVGL adapter park; the ~93 Hz
                                   settings-idle CPU story, task #31 era)
  lv_timer_create(ui_tick_timer_cb, 10)        components/ui/actions.c
  #define MOON_TICK_MS 50                      components/ui/moon_ui.c
                                               (the bake-reveal strip tick)
  UI_MOON_REPLAY_FPS (Kconfig default 8)       components/ui/Kconfig
                                               -> 1000/fps pacing floor
  UI_MOON_HOURLY_FADE_MS                       components/ui/Kconfig
      default UI_SCREEN_SWITCH_FADE            (SYMBOL indirection —
                                               resolved to the fade
                                               symbol's own default 240)
  lv_timer_create(moon_tilt_monitor_cb, 1000)  components/ui/moon_ui.c
  lv_timer_create(seconds_increment_timer_cb, 1000) components/ui/actions.c
  CLOCK_IDLE_CHECK_INTERVAL_MS (Kconfig 1000)  components/clock/Kconfig
  #define RTC_POLL_INTERVAL_MS 1300            main/main.cpp

Right panel: the same rows as EVENTS PER SECOND — the user-perceptible
rate, derived (1000/ms), nothing new measured. Hatched bars mark the
two Kconfig-paced rows (replay floor, fade legs); their budget-side
consequences live in perf_boot.png / perf_hourly.png.

Run:  docs/.venv/bin/python docs/tools/gen_perf_pacing.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
UI_KCONFIG = os.path.join(ROOT, "components", "ui", "Kconfig")
CLOCK_KCONFIG = os.path.join(ROOT, "components", "clock", "Kconfig")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
BSP = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                   "esp32_s3_touch_amoled_1_75.c")
MAIN = os.path.join(ROOT, "main", "main.cpp")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_pacing.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # LVGL-side timers (tick, adapter, seconds)
VIOLET = "#7c3aed"   # moon pipeline (reveal tick, replay floor, fade)
AMBER = "#b07d10"    # the idle/sleep ladder (idle check, tilt)
RED = "#b91c1c"      # the RTC poll (the only interrupt-driven period)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def kconfig_default(path: str, symbol: str, name: str = "",
                    _depth: int = 0) -> tuple[int, str]:
    """The effective default of an int Kconfig symbol: the first literal
    ``default <int>``, or — the house idiom for coupled timings — the
    first ``default <SYMBOL>`` followed recursively (UI_MOON_HOURLY_FADE_MS
    defaults to UI_SCREEN_SWITCH_FADE, so the figure resolves the chain
    exactly like Kconfig does). Returns (value, provenance)."""
    if _depth > 4:
        raise SystemExit(f"{symbol}: default chain deeper than 4 links")
    name = name or os.path.relpath(path, ROOT)
    text = _read(path)
    # the entry spans from its `config SYMBOL` line (menus INDENT these)
    # to the next entry keyword — never into the following entry's help
    start = re.search(rf"^[ \t]*config {symbol}[ \t]*$", text, re.M)
    if not start:
        raise SystemExit(f"{symbol}: not found in {name}")
    nxt = re.search(r"^[ \t]*(?:config|menu|endmenu|choice|endchoice)\b",
                    text[start.end():], re.M)
    block = text[start.end():start.end() + (nxt.start() if nxt else len(text))]
    m = re.search(r"^[ \t]+default ([A-Za-z_][A-Za-z0-9_]*|\d+)", block, re.M)
    if not m:
        raise SystemExit(f"{symbol}: no default line in {name}")
    tok = m.group(1)
    if tok.isdigit():
        return int(tok), f"{name}:{tok}"
    # symbol indirection — resolve in the same file (both fade symbols
    # live in components/ui/Kconfig)
    val, _ = kconfig_default(path, tok, name, _depth + 1)
    return val, f"via {tok}"  # the resolved value is the bar itself


def c_define(path: str, symbol: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.basename(path)}")
    return int(m.group(1))


def c_timer_period(path: str, cb: str) -> int:
    """The period of lv_timer_create(<cb>, <ms>, ...) — first occurrence."""
    m = re.search(rf"lv_timer_create\(\s*{cb}\s*,\s*(\d+)\s*,", _read(path))
    if not m:
        raise SystemExit(f"lv_timer_create({cb}, <ms>): not found in "
                         f"{os.path.basename(path)}")
    return int(m.group(1))


def c_field(path: str, field: str) -> int:
    m = re.search(rf"{field}\s*=\s*(\d+)\s*;", _read(path))
    if not m:
        raise SystemExit(f"{field} = <n>; not found in {os.path.basename(path)}")
    return int(m.group(1))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def hz_label(hz: float) -> str:
    """2 decimals below 10 Hz, integers above — no 0.769231 noise."""
    if hz < 10:
        return f"{hz:.2f}".rstrip("0").rstrip(".")
    return str(int(round(hz)))


def main() -> None:
    adapter = c_field(BSP, "task_min_delay_ms")
    uitick = c_timer_period(ACTIONS, "ui_tick_timer_cb")
    moon_tick = c_define(MOON_UI, "MOON_TICK_MS")
    replay_fps, replay_prov = kconfig_default(UI_KCONFIG, "UI_MOON_REPLAY_FPS",
                                              "ui/Kconfig")
    fade_ms, fade_prov = kconfig_default(UI_KCONFIG, "UI_MOON_HOURLY_FADE_MS",
                                         "ui/Kconfig")
    tilt = c_timer_period(MOON_UI, "moon_tilt_monitor_cb")
    seconds = c_timer_period(ACTIONS, "seconds_increment_timer_cb")
    idle_check, _ = kconfig_default(CLOCK_KCONFIG, "CLOCK_IDLE_CHECK_INTERVAL_MS",
                                    "clock/Kconfig")
    rtc = c_define(MAIN, "RTC_POLL_INTERVAL_MS")

    # (label, ms, color, hatched?, provenance)
    rows = [
        (f"LVGL adapter park\ntask_min_delay_ms = {adapter}", adapter, BLUE, False,
         "esp32_s3_touch_amoled_1_75.c:605"),
        (f"UI tick (awake state)\nlv_timer_create(ui_tick_timer_cb)", uitick,
         BLUE, False, "actions.c:856"),
        (f"moon reveal tick\nMOON_TICK_MS", moon_tick, VIOLET, False,
         "moon_ui.c:186"),
        (f"replay frame floor\n1000 / UI_MOON_REPLAY_FPS", 1000 / replay_fps,
         VIOLET, True, replay_prov),
        (f"hourly fade leg (each)\nUI_MOON_HOURLY_FADE_MS", fade_ms,
         VIOLET, True, fade_prov),
        (f"idle check\nCLOCK_IDLE_CHECK_INTERVAL_MS", idle_check, AMBER, True,
         "clock/Kconfig"),
        (f"tilt monitor\nlv_timer_create(moon_tilt_monitor_cb)", tilt, AMBER,
         False, "moon_ui.c:2588"),
        (f"seconds increment\nlv_timer_create(seconds_increment_timer_cb)",
         seconds, BLUE, False, "actions.c:862"),
        (f"RTC interrupt poll\nRTC_POLL_INTERVAL_MS", rtc, RED, False,
         "main.cpp:30"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.3))

    # ---- (a) the period ladder ----------------------------------------
    ax = axes[0]
    ys = range(len(rows))
    for y, (label, ms, color, hatched, prov) in enumerate(rows):
        if hatched:
            ax.barh(y, ms, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, ms, color=color, alpha=0.6, height=0.6, edgecolor=color)
        ax.text(ms * 1.12, y, f"{ms:g} ms · {prov}", va="center", fontsize=6.2,
                color=MUTED)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(6, 60000)
    ax.set_xlabel("period, ms (log) — every shipped self-wake period")
    ax.set_title("the device's own heartbeat — parsed from the live sources\n"
                 "(hatched = Kconfig-paced)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the same rows as events per second -------------------------
    ax = axes[1]
    for y, (label, ms, color, hatched, prov) in enumerate(rows):
        hz = 1000.0 / ms
        if hatched:
            ax.barh(y, hz, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, hz, color=color, alpha=0.6, height=0.6, edgecolor=color)
        ax.text(hz * 1.12, y, f"{hz_label(hz)} Hz", va="center", fontsize=6.6,
                color=INK)
    ax.set_yticks(list(ys), [r[0].split("\n")[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlim(0.4, 1000)
    ax.set_xlabel("events per second (log) — 1000 / period, nothing new measured")
    ax.set_title("the perceived rates\n(adapter 100 Hz park ↔ RTC poll below 1 Hz)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(adapter={adapter} tick={uitick} moon={moon_tick} "
          f"replay={1000/replay_fps:g} fade={fade_ms} idle={idle_check} "
          f"tilt={tilt} sec={seconds} rtc={rtc})")


if __name__ == "__main__":
    main()
