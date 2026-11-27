#!/usr/bin/env python3
"""Generate the input-response ladder for docs/verification/performance.rst.

The pacing ladder (gen_perf_pacing.py) measures how OFTEN the device
wakes ITSELF; this one measures how soon it notices the USER — the
detection-to-visible-response chain from finger, tilt and dial. On the
axis sit the knob encoder's 3 ms poll (10 ms tick floor at the knob's
FREERTOS_HZ 100), the debounce pair behind it, the 20 ms dial_task,
the one 33 ms LVGL indev/read timer every touch path shares, the 10 ms
UI tick, the 2 x 240 ms fade round-trip every screen switch pays, and
the three human-scale 2 s rows — orientation window+confirm, the
wake's 2-sample streak, the deliberate press — same length, three
intentions. This is the timing companion of docs/_graphs/
input-chain.dot (who reads) and the third column of the pacing/patience
family (how OFTEN / how LONG allowed / how SOON noticed).

Every number is PARSED from the live sources at figure time (the house
pattern):

  #define ENC_POLL_MS 3            components/knob_board/knob_board_encoder.c:32
      (vendor TICKS_INTERVAL; the row's annotation owns the tick-safe
       floor: pdMS_TO_TICKS(3) == 0 at the knob's 100 Hz tick, so the
       vTaskDelay lands at 1 tick = 10 ms — the bench lesson that cost
       a busy-spun WDT abort, :64-:73)
  #define ENC_DEBOUNCE 2           .../knob_board_encoder.c:33
      (edge accepted after DEBOUNCE_TICKS consecutive identical samples
       — :11; right panel derives 2 x 3)
  vTaskDelay(pdMS_TO_TICKS(20))    components/knob_ui/knob_dial.c:85
      (dial_task period; xTaskCreate "knob_dial" prio 5 — :140; the
       consumer of the encoder queue — knob_dial.c:140 era note in
       docs/_graphs/input-chain.dot)
  #define CONFIG_LV_DEF_REFR_PERIOD 33   build/config/sdkconfig.h,
                              build/p4/config/sdkconfig.h,
                              build/knob/config/sdkconfig.h
      (the ONE LVGL read+refresh beat: lv_indev.c:105 creates the
       indev timer at LV_DEF_REFR_PERIOD — parsed from ALL THREE
       generated headers and asserted equal; the CST9217's INT-mode IRQ
       wakes the read ONTO this timer, the P4's GT911 INT is NC and
       polls on it — input-chain.dot)
  lv_timer_create(ui_tick_timer_cb, 10)  components/ui/actions.c:939
      (the UI tick under the beat)
  UI_SCREEN_SWITCH_FADE (Kconfig default 240)  components/ui/Kconfig:680
      (the round-trip is fade-out + while-black flush + fade-in =
       2 x 240 — derived, screen-switch.dot)
  #define ORI_SAMPLE_MS 100        components/ui/actions.c:197
  #define ORI_WINDOW 10            components/ui/actions.c:198
      (the 90-deg step's visible cost is the code's OWN sentence: "the
       1 s window lags the step changes by ~1 s, and the sustained
       confirmation needs a second ~1 s on top" — :314-:315 — so the
       row is 2 x ORI_SAMPLE_MS x ORI_WINDOW, derived)
  lv_timer_create(moon_tilt_monitor_cb, 1000) components/ui/moon_ui.c:2729
  if (++s_motion_streak >= 2)      components/ui/moon_ui.c:2478
      (the wake's confirmation: two consecutive above-bar samples at
       the 1 Hz monitor — derived 1000 x 2)
  #define ORI_RESYNC_PRESS_MS 2000 components/ui/actions.c:265
      (the deliberate 2 s press — same constant as the patience
       ladder's row, drawn here with the human-intent colour)

Right panel: the SAME rows as multiples of the 33 ms beat
(ms / CONFIG_LV_DEF_REFR_PERIOD, log) — derived (one division), nothing
new measured: who BEATS the beat (the knob's dedicated encoder path,
0.09x–0.61x), what IS the beat (the indev row, 1x by definition), and
that every human-scale confirmation sits at 60x — intentional holds,
not latency.

Run:  docs/.venv/bin/python docs/tools/gen_perf_response.py
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
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
ENCODER = os.path.join(ROOT, "components", "knob_board",
                       "knob_board_encoder.c")
DIAL = os.path.join(ROOT, "components", "knob_ui", "knob_dial.c")
# The three generated headers — the same three the scheduler ladder
# reads its WDT baseline from (house precedent: the lvgl Kconfig is
# vendor default, the generated header is what the firmware ships).
SDKH = {
    "S3": os.path.join(ROOT, "build", "config", "sdkconfig.h"),
    "P4": os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h"),
    "knob": os.path.join(ROOT, "build", "knob", "config", "sdkconfig.h"),
}
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_response.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"     # the LVGL-side beats (the beat itself, the UI tick)
AMBER = "#b07d10"    # the knob's dedicated encoder path
VIOLET = "#7c3aed"   # the navigation round-trip (the fade machine)
TEAL = "#0e7490"     # the human-scale confirmations (tilt, press)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def kconfig_default(path: str, symbol: str, name: str = "") -> int:
    """The first literal ``default <int>`` of an int Kconfig symbol."""
    name = name or os.path.relpath(path, ROOT)
    text = _read(path)
    start = re.search(rf"^[ \t]*config {symbol}[ \t]*$", text, re.M)
    if not start:
        raise SystemExit(f"{symbol}: not found in {name}")
    nxt = re.search(r"^[ \t]*(?:config|menu|endmenu|choice|endchoice)\b",
                    text[start.end():], re.M)
    block = text[start.end():start.end() + (nxt.start() if nxt else len(text))]
    m = re.search(r"^[ \t]+default (\d+)", block, re.M)
    if not m:
        raise SystemExit(f"{symbol}: no literal default in {name}")
    return int(m.group(1))


def c_define(path: str, symbol: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{symbol}: no #define in {os.path.basename(path)}")
    return int(m.group(1))


def c_regex_int(path: str, pattern: str, what: str) -> int:
    m = re.search(pattern, _read(path), re.S)
    if not m:
        raise SystemExit(f"{what}: pattern not found in "
                         f"{os.path.basename(path)}")
    return int(m.group(1))


def refr_period() -> int:
    """CONFIG_LV_DEF_REFR_PERIOD, parsed from ALL THREE generated
    headers and asserted equal — the input-chain claim (one 33 ms
    indev timer on all three boards) re-verified at figure time."""
    vals = {}
    for board, p in SDKH.items():
        if not os.path.exists(p):
            raise SystemExit(f"sdkconfig.h for {board} missing: {p} — the "
                             "beat row cannot be verified against all three "
                             "boards (build the trees or drop the row)")
        vals[board] = c_define(p, "CONFIG_LV_DEF_REFR_PERIOD")
    if len(set(vals.values())) != 1:
        raise SystemExit(f"CONFIG_LV_DEF_REFR_PERIOD disagrees across the "
                         f"three boards: {vals}")
    return next(iter(vals.values()))


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def ratio_label(r: float) -> str:
    if r >= 10:
        return f"{int(round(r))}x"
    if r >= 1:
        return f"{r:.1f}".rstrip("0").rstrip(".") + "x"
    return f"{r:.2f}".rstrip("0").rstrip(".") + "x"


def main() -> None:
    enc_poll = c_define(ENCODER, "ENC_POLL_MS")
    enc_deb = c_define(ENCODER, "ENC_DEBOUNCE")
    dial_ms = c_regex_int(DIAL, r"dial_task[\s\S]{0,120}?vTaskDelay\(pdMS_TO_TICKS\((\d+)\)\)",
                          "dial_task period")
    beat = refr_period()
    uitick = c_regex_int(ACTIONS, r"lv_timer_create\(ui_tick_timer_cb,\s*(\d+),",
                         "UI tick period")
    fade = kconfig_default(UI_KCONFIG, "UI_SCREEN_SWITCH_FADE", "ui/Kconfig")
    ori_ms = c_define(ACTIONS, "ORI_SAMPLE_MS")
    ori_win = c_define(ACTIONS, "ORI_WINDOW")
    tilt_ms = c_regex_int(MOON_UI, r"lv_timer_create\(moon_tilt_monitor_cb,\s*(\d+)",
                          "tilt monitor period")
    streak = c_regex_int(MOON_UI, r"\+\+s_motion_streak\s*>=\s*(\d+)",
                         "tilt wake streak")
    press = c_define(ACTIONS, "ORI_RESYNC_PRESS_MS")
    hz = c_define(SDKH["knob"], "CONFIG_FREERTOS_HZ")
    if hz <= 0:
        raise SystemExit(f"CONFIG_FREERTOS_HZ parsed as {hz} — cannot "
                         "derive the encoder's tick floor")
    tick_floor_ms = -(-1000 // hz)  # ceil — the vTaskDelay(1) landing

    # (label, ms, color, hatched?, provenance)
    rows = [
        ("knob encoder poll\nENC_POLL_MS", enc_poll, AMBER, False,
         f"knob_board_encoder.c:32 (≥1 tick = {tick_floor_ms} ms at "
         f"{hz} Hz)"),
        ("knob encoder debounce\nENC_DEBOUNCE x poll", enc_deb * enc_poll,
         AMBER, False, f"knob_board_encoder.c:33 (x {enc_deb})"),
        ("knob dial_task period\npdMS_TO_TICKS", dial_ms, AMBER, False,
         "knob_dial.c:85/:140"),
        ("knob detent visible\ndial_task + repaint", dial_ms + beat, AMBER,
         False, "derived: dial + beat"),
        ("LVGL indev read timer\nCONFIG_LV_DEF_REFR_PERIOD", beat, BLUE,
         True, "sdkconfig.h x3, equal"),
        ("UI tick\nui_tick_timer_cb", uitick, BLUE, False, "actions.c:939"),
        ("screen-switch round-trip\n2 x UI_SCREEN_SWITCH_FADE", 2 * fade,
         VIOLET, True, "ui/Kconfig:680 (x2)"),
        ("tilt 90-degree step visible\n2 x ORI_SAMPLE_MS x ORI_WINDOW",
         2 * ori_ms * ori_win, TEAL, False, "actions.c:197/:198 (x2)"),
        ("tilt wake confirm\nmonitor x streak", tilt_ms * streak, TEAL,
         False, f"moon_ui.c:2729 x :2478 ({streak})"),
        ("orientation press (held)\nORI_RESYNC_PRESS_MS", press, TEAL,
         False, "actions.c:265"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.3))

    # ---- (a) the response ladder ---------------------------------------
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
    ax.set_xlim(2, 30_000)
    ax.set_xlabel("detection → visible response, ms (log)")
    ax.set_title("how soon the machine notices the user — parsed from the "
                 "live sources\n(amber = the knob's dedicated path, blue = "
                 "the shared beat, violet = the fade round-trip, "
                 "teal = human-scale holds)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the same rows vs the beat ----------------------------------
    ax = axes[1]
    for y, (label, ms, color, hatched, prov) in enumerate(rows):
        r = ms / beat
        if hatched:
            ax.barh(y, r, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, r, color=color, alpha=0.6, height=0.6, edgecolor=color)
        ax.text(max(r, 0.05) * 1.12, y, ratio_label(r), va="center",
                fontsize=6.6, color=INK)
    ax.axvline(1.0, color=BLUE, linestyle="--", linewidth=1.1)
    ax.set_yticks(list(ys), [r[0].split("\n")[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(rows) - 0.5, -1.0)
    ax.text(1.08, -0.45, f"the {beat} ms indev/refresh beat = 1x\n"
            "(the beat row lands here by definition)",
            fontsize=6.2, color=BLUE, va="top")
    ax.set_xscale("log")
    ax.set_xlim(0.05, 200)
    ax.set_xlabel(f"multiples of CONFIG_LV_DEF_REFR_PERIOD (log) — ms / {beat}, "
                  "derived, nothing new measured")
    ax.set_title("who beats the beat, who pays it\n(only the knob's encoder "
                 "path is faster than the touchscreen's read)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(enc={enc_poll} deb_x={enc_deb} dial={dial_ms} beat={beat} "
          f"uitick={uitick} fade={fade} ori={ori_ms}x{ori_win} "
          f"tilt={tilt_ms}x{streak} press={press})")


if __name__ == "__main__":
    main()
