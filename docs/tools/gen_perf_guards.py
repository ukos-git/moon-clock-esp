#!/usr/bin/env python3
"""Generate the guard/deadline ladder for docs/verification/performance.rst.

The pacing ladder (gen_perf_pacing.py) measures the device's HEART —
how OFTEN it wakes itself. This one measures its PATIENCE: every
ONE-SHOT wait the shipped firmware can sit in before it acts — the
2 s drain-witness gate, the 3 s panel-io watchdog, the traffic hold's
cap and tail, the 2 s orientation-press, the scan-retry gap, the two
NTP fix windows, the knob's join wait — plus the two BUDGETS the other
ladders measure against (the hourly producer's 5 s, the replay's
240 s). None of these is a period; they are deadlines, and on this
ladder a lost GPSPI completion (the 2 s vs 3 s pair) sits visibly
beside a deliberate press (also 2 s) — same length, opposite intent.

Every number is PARSED from the live sources at figure time (the
house pattern):

  #define FLUSH_WAIT_TIMEOUT_MS 2000       components/ui/moon_ui.c:437
      (the drain witness — moon_flush_wait_cb's gate on s_panel_mtx,
       the lost-trans-done hand of task #29/#30; docs/_graphs/
      flush-path.dot draws the machine this wait belongs to)
  #define PANEL_IO_WATCHDOG_LIMIT_MS 3000  components/ui/moon_ui.c:588
      (the true-stall case: the 3 s panel-io watchdog restart)
  #define PANEL_TRAFFIC_HOLD_MS 15000      components/ui/moon_ui.c:362
  #define PANEL_TRAFFIC_TAIL_MS 4000       components/ui/moon_ui.c:373
      (the scan-following traffic hold: a cap on the hold itself and
       the tail the last holder's drop lags behind)
  #define ORI_RESYNC_PRESS_MS 2000         components/ui/actions.c:265
      (the deliberate 2 s press that re-anchors the orientation)
  const int WIFI_SCAN_MAX_ATTEMPTS = 4     components/wifi/wifi.c:478
      + vTaskDelay(pdMS_TO_TICKS(1000))    components/wifi/wifi.c:531
      (the bounded first-scan retry, fef184e — the bar is the GAP,
       the annotation the attempt count)
  CLOCK_NTP_TIMEOUT_MS (Kconfig 10000)     components/clock/Kconfig
      (the synchronous boot fix window inside clock_init)
  KNOB_TIME_BOOT_SYNC_MS (Kconfig 20000)   knob/components/knob_time/Kconfig
      (the knob's fix window; after it the glass arms on the NVS
       snapshot as "last known")
  while (!connected) <= 15000              knob/main/main.c:225
      (the join wait before any NTP attempt)
  #define HOURLY_BUDGET_MS 5000            components/ui/moon_ui.c:1006
  UI_MOON_REPLAY_BUDGET_MS (Kconfig)       components/ui/Kconfig
      (the two budgets — right panel draws them at exactly 1x)

Right panel: the SAME rows as multiples of the hourly producer budget
(ms / HOURLY_BUDGET_MS, log) — derived (one division), nothing new
measured: which waits can outlive a producer cycle, and how far the
replay budget sits above the bus guards (five orders of patience in
one view).

Run:  docs/.venv/bin/python docs/tools/gen_perf_guards.py
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
KNOB_TIME_KCONFIG = os.path.join(
    ROOT, "knob", "components", "knob_time", "Kconfig")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
WIFI = os.path.join(ROOT, "components", "wifi", "wifi.c")
KNOB_MAIN = os.path.join(ROOT, "knob", "main", "main.c")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_guards.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
RED = "#b91c1c"      # the panel-bus guards (the lost-completion family)
AMBER = "#b07d10"   # the deliberate human wait (the 2 s press)
TEAL = "#0e7490"    # the radio's fix windows (network waits)
VIOLET = "#7c3aed"  # the two moon budgets (the yardsticks)


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def kconfig_default(path: str, symbol: str, name: str = "") -> int:
    """The first literal ``default <int>`` of an int Kconfig symbol
    (the house parser, trimmed to the ladder's needs — no symbol
    indirection among these rows)."""
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


def c_const_int(path: str, symbol: str) -> int:
    m = re.search(rf"const int {symbol}\s*=\s*(\d+)\s*;", _read(path))
    if not m:
        raise SystemExit(f"const int {symbol} = <n>; not found in "
                         f"{os.path.basename(path)}")
    return int(m.group(1))


def c_regex_int(path: str, pattern: str, what: str) -> int:
    m = re.search(pattern, _read(path), re.S)
    if not m:
        raise SystemExit(f"{what}: pattern not found in "
                         f"{os.path.basename(path)}")
    return int(m.group(1))


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
    flush = c_define(MOON_UI, "FLUSH_WAIT_TIMEOUT_MS")
    wdg = c_define(MOON_UI, "PANEL_IO_WATCHDOG_LIMIT_MS")
    hold = c_define(MOON_UI, "PANEL_TRAFFIC_HOLD_MS")
    tail = c_define(MOON_UI, "PANEL_TRAFFIC_TAIL_MS")
    press = c_define(ACTIONS, "ORI_RESYNC_PRESS_MS")
    attempts = c_const_int(WIFI, "WIFI_SCAN_MAX_ATTEMPTS")
    retry_gap = c_regex_int(WIFI, r"attempt == WIFI_SCAN_MAX_ATTEMPTS\)"
                            r"[\s\S]{0,200}?vTaskDelay\(pdMS_TO_TICKS\((\d+)\)\)",
                            "scan retry gap")
    ntp = kconfig_default(CLOCK_KCONFIG, "CLOCK_NTP_TIMEOUT_MS", "clock/Kconfig")
    knob_fix = kconfig_default(KNOB_TIME_KCONFIG, "KNOB_TIME_BOOT_SYNC_MS",
                               "knob_time/Kconfig")
    join = c_regex_int(KNOB_MAIN, r"waited < (\d+) && !wifi_manager_is_connected",
                       "knob join wait")
    hourly = c_define(MOON_UI, "HOURLY_BUDGET_MS")
    replay = kconfig_default(UI_KCONFIG, "UI_MOON_REPLAY_BUDGET_MS", "ui/Kconfig")

    # (label, ms, color, hatched?, provenance)
    rows = [
        (f"drain-witness gate\nFLUSH_WAIT_TIMEOUT_MS", flush, RED, False,
         "moon_ui.c:471"),
        (f"panel-io watchdog\nPANEL_IO_WATCHDOG_LIMIT_MS", wdg, RED, False,
         "moon_ui.c:625"),
        (f"traffic hold tail\nPANEL_TRAFFIC_TAIL_MS", tail, RED, False,
         "moon_ui.c:380"),
        (f"traffic hold cap\nPANEL_TRAFFIC_HOLD_MS", hold, RED, False,
         "moon_ui.c:369"),
        (f"orientation press\nORI_RESYNC_PRESS_MS", press, AMBER, False,
         "actions.c:265"),
        (f"scan retry gap (x {attempts})\nWIFI_SCAN_MAX_ATTEMPTS @ 1 s", retry_gap,
         TEAL, False, "wifi.c:478/531"),
        (f"boot NTP fix window\nCLOCK_NTP_TIMEOUT_MS", ntp, TEAL, True,
         "clock/Kconfig"),
        (f"knob join wait\nwhile (!connected) <=", join, TEAL, False,
         "knob/main/main.c:225"),
        (f"knob NTP fix window\nKNOB_TIME_BOOT_SYNC_MS", knob_fix, TEAL, True,
         "knob_time/Kconfig"),
        (f"hourly producer budget\nHOURLY_BUDGET_MS", hourly, VIOLET, False,
         "moon_ui.c:1070"),
        (f"boot replay budget\nUI_MOON_REPLAY_BUDGET_MS", replay, VIOLET, True,
         "ui/Kconfig"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.3))

    # ---- (a) the patience ladder ---------------------------------------
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
    ax.set_xlim(700, 2_000_000)
    ax.set_xlabel("deadline, ms (log) — every one-shot wait the firmware sits in")
    ax.set_title("the device's patience — parsed from the live sources\n"
                 "(hatched = Kconfig-paced; violet = the yardsticks)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the same rows vs the producer budget -----------------------
    ax = axes[1]
    for y, (label, ms, color, hatched, prov) in enumerate(rows):
        r = ms / hourly
        if hatched:
            ax.barh(y, r, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, r, color=color, alpha=0.6, height=0.6, edgecolor=color)
        ax.text(max(r, 0.05) * 1.12, y, ratio_label(r), va="center",
                fontsize=6.6, color=INK)
    ax.axvline(1.0, color=RED, linestyle="--", linewidth=1.1)
    ax.set_yticks(list(ys), [r[0].split("\n")[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(rows) - 0.5, -1.0)
    ax.text(1.08, -0.45, "the hourly producer budget = 1x\n(perf_hourly.png's own line)",
            fontsize=6.2, color=RED, va="top")
    ax.set_xscale("log")
    ax.set_xlim(0.05, 200)
    ax.set_xlabel("multiples of HOURLY_BUDGET_MS (log) — ms / 5000, derived, "
                  "nothing new measured")
    ax.set_title("how much producer time each wait is worth\n"
                 "(the replay budget: 48x a whole hourly cycle)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(flush={flush} wdg={wdg} tail={tail} hold={hold} press={press} "
          f"scan={retry_gap}x{attempts} ntp={ntp} join={join} fix={knob_fix} "
          f"hourly={hourly} replay={replay})")


if __name__ == "__main__":
    main()
