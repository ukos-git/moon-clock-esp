#!/usr/bin/env python3
"""Generate the freshness ladder for docs/verification/performance.rst.

The response ladder (gen_perf_response.py) measures how soon the user
is NOTICED — the input side of the same currency family. This is its
mirror: how OLD every number on the panel may be when the user looks
at it — the output side. Nine timed rows plus two honest no-timer
rows, every value parsed from the live sources at figure time (the
house pattern), on one log millisecond axis:

  lv_timer_create(seconds_increment_timer_cb, 1000, NULL)
      components/ui/actions.c:945 (+1450/1525 siblings) — the
      seconds var counts +1 per tick (actions.c:1143) and is
      re-synced to the clock at the minute callback (:1038)
  s_rtc_poll_interval_ms = 1300    components/clock/clock_interrupt.c:26
      (the poll that notices the wall-minute rollover — the code's own
      comment: "the 1.3 s poll cannot straddle one rollover twice";
      the digits then repaint from the callback the app registered at
      main.cpp:616 -> action_update_clock_display, :954)
  (the minute tick's own reloads)   components/ui/actions.c:1042
      (refresh_alarm_display at the minute tick — the weekday prefix
      flips at the 24 h boundary and lands within one minute)
  lv_timer_create(battery_timer_cb, 60000, NULL)
      knob/components/knob_ui/knob_battery_ui.c:204 (+205 first fill) —
      "the cell moves on the minute scale, not the second" (:6)
  #define CLOCK_NVS_SAVE_INTERVAL_SEC 900
      components/clock/clock_sntp.c:52 (+76 gate, +116 FORCE on every
      SNTP fix) — the P4/knob boot floor's age ceiling
  CONFIG_CLOCK_NTP_SYNC_INTERVAL_HOURS (Kconfig default 1)
      components/clock/Kconfig, × 3600 at components/clock/clock_sntp.c:268
      (clock_sntp_needs_sync's correction cadence — what the radio
      corrects AGAINST; hardware drift between corrections is not a
      figure value, it is the 32k-crystal bench question in
      docs/devices/p4.rst)
  s_hourly_epoch = hour * 3600 + 1800
      components/ui/moon_ui.c:1667 — the hour-centre pin: the shown
      frame depicts the hour centre, so it is "at most 30 min stale at
      the show" (:1531, the code's own sentence); the cycle is the
      hour (due = hour != s_baked_hour, :1649). The offset 1800 is
      PARSED, not assumed.
  (the TF hour cache)                components/ui/moon_ui.c:1530-1532
      the same pin, but "byte-identical across boots and boards" — the
      content is a pure function of (hour-centre, calcver), so its
      age is never stale-by-accumulation (hatched row)
  (sky events' calendar-day scan)   components/ui/moon_ui.c:1090
      (s_ev_warm_day; probed from the minute path via
      moon_ui_day_tick, components/ui/actions.c:1050 — "the one
      untinted bake self-heals", the code's own words)
  #define CONFIG_LV_DEF_REFR_PERIOD 33   build/config/sdkconfig.h,
                              build/p4/config/sdkconfig.h,
                              /home/matthias/esp/build/knob/config/sdkconfig.h
      (the render beat every row lands on — parsed from ALL THREE
      generated headers and asserted equal, the response ladder's
      parser reused)

Two rows carry NO bar because the code has no timer for them (the
honest *no such leg*, the bus ladder's precedent): the Wi-Fi state is
on demand only (kickoffs at actions.c:816 screen change, :1494 wake,
:1027 the modem-sleep minute-top sync — no poll), and the moon
albedo/DEM blob is a pure function of the pipeline vintage keyed by
calcver + width guard (the CMake FATAL_ERRORs).

The 60 s minute, the 3600 s hour and the 86400 s day are wall-calendar
definitions, not firmware constants — they are stated as such on the
rows and in the page prose, never disguised as parses.

Right panel: the SAME rows as checks per day (86400 s / cadence, log)
— derived (one division), nothing new measured: ~156,000 checks a day
(86,400 + 66,462 + 2×1,440 + 96 + 3×24 + 1 = 155,911) and the only
ones that can go STALE at all are the ADC (1440), the radio (on
demand) and the NVS snapshot (96) — the rest is the ephemeris being
re-derived, which is fresh by construction.

Run:  docs/.venv/bin/python docs/tools/gen_perf_fresh.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
CLOCK_INT = os.path.join(ROOT, "components", "clock", "clock_interrupt.c")
SNTP = os.path.join(ROOT, "components", "clock", "clock_sntp.c")
CLOCK_KCONFIG = os.path.join(ROOT, "components", "clock", "Kconfig")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
KNOB_BATT = os.path.join(ROOT, "knob", "components", "knob_ui",
                         "knob_battery_ui.c")
SDKHS = {
    "S3": os.path.join(ROOT, "build", "config", "sdkconfig.h"),
    "P4": os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h"),
    "knob": "/home/matthias/esp/build/knob/config/sdkconfig.h",
}
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_fresh.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
BLUE = "#2b5fb0"    # the clock/tick family (the shared home display)
AMBER = "#b07d10"   # the knob's gauge (the only ADC in the family)
TEAL = "#0e7490"    # the time source (NVS snapshot + SNTP cadence)
VIOLET = "#7c3aed"  # the moon family (disk pin, hour cache, sky tint)

# Wall-calendar definitions (NOT firmware constants — stated on the row).
MINUTE_MS = 60_000
HOUR_MS = 3_600_000
DAY_MS = 86_400_000


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


def beat_from_three_headers() -> int:
    """CONFIG_LV_DEF_REFR_PERIOD parsed from ALL THREE generated
    sdkconfig.h files and asserted equal (the response ladder's parser)
    — if a tree holds the other target's config the parse or the
    equality will fail loudly."""
    vals = {}
    for board, p in SDKHS.items():
        if not os.path.exists(p):
            raise SystemExit(f"sdkconfig.h for {board} missing: {p} — the "
                             f"figure is only as honest as the build trees")
        vals[board] = c_define(p, "CONFIG_LV_DEF_REFR_PERIOD")
    if len(set(vals.values())) != 1:
        raise SystemExit(f"CONFIG_LV_DEF_REFR_PERIOD disagrees across the "
                         f"three trees: {vals}")
    return next(iter(vals.values()))


def fmt_age(ms: float) -> str:
    if ms >= DAY_MS:
        return f"{ms / DAY_MS:g} d"
    if ms >= HOUR_MS:
        return f"{ms / HOUR_MS:g} h"
    if ms >= MINUTE_MS:
        return f"{ms / MINUTE_MS:g} min"
    if ms >= 1000:
        return f"{ms / 1000:g} s"
    return f"{ms:g} ms"


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    secs = c_regex_int(ACTIONS,
                       r"lv_timer_create\(seconds_increment_timer_cb, "
                       r"(\d+), NULL\)", "seconds tick")
    poll = c_regex_int(CLOCK_INT, r"s_rtc_poll_interval_ms\s*=\s*(\d+)",
                       "rtc poll interval")
    gauge = c_regex_int(KNOB_BATT,
                        r"lv_timer_create\(battery_timer_cb, (\d+), NULL\)",
                        "battery gauge timer")
    nvs_s = c_define(SNTP, "CLOCK_NVS_SAVE_INTERVAL_SEC")
    ntp_h = kconfig_default(CLOCK_KCONFIG, "CLOCK_NTP_SYNC_INTERVAL_HOURS",
                            "clock/Kconfig")
    centre_off_s = c_regex_int(MOON_UI, r"s_hourly_epoch = hour \* 3600 \+ (\d+)",
                               "hour-centre offset")
    beat = beat_from_three_headers()

    # (label, staleness_ms, cadence_ms, color, hatched?, provenance)
    # staleness_ms == None → the honest no-bar rows (no timer exists).
    rows = [
        (f"seconds arc\nseconds_increment_timer_cb", secs, secs, BLUE, False,
         f"actions.c:945 (+{beat} ms beat)"),
        (f"clock digits HH:MM\ns_rtc_poll_interval_ms", poll, poll, BLUE,
         False, f"clock_interrupt.c:26 (+{beat} ms beat)"),
        (f"alarm row prefix\nwall-minute reload", MINUTE_MS, MINUTE_MS, BLUE,
         False, "actions.c:1042 (24 h-boundary flip)"),
        (f"battery gauge (knob)\nbattery_timer_cb", gauge, gauge, AMBER,
         False, "knob_battery_ui.c:204"),
        (f"NVS epoch snapshot\nCLOCK_NVS_SAVE_INTERVAL_SEC", nvs_s * 1000,
         nvs_s * 1000, TEAL, True, "clock_sntp.c:52 (force @116)"),
        (f"SNTP correction cadence\nCLOCK_NTP_SYNC_INTERVAL_HOURS",
         ntp_h * HOUR_MS, ntp_h * HOUR_MS, TEAL, True,
         "clock/Kconfig × clock_sntp.c:268"),
        (f"home moon disk\nhour-centre pin", centre_off_s * 1000, HOUR_MS,
         VIOLET, False, "moon_ui.c:1667 (:1531 own words)"),
        (f"TF hour cache\nsame pin, pure content", centre_off_s * 1000,
         HOUR_MS, VIOLET, True, "moon_ui.c:1530-32 byte-identical"),
        (f"sky event tint\ncalendar-day scan", DAY_MS, DAY_MS, VIOLET, False,
         "moon_ui.c:1090 via actions.c:1050"),
        (f"Wi-Fi connection state\nno timer at all", None, None, TEAL, False,
         "on demand: actions.c:816/1494/1027"),
        (f"moon albedo/DEM blob\npure function", None, None, MUTED, False,
         "calcver+width guard, CMake CONFIGURE"),
    ]
    timed = [r for r in rows if r[1] is not None]

    fig, axes = plt.subplots(1, 2, figsize=(11.2, 4.5))

    # ---- (a) the freshness ladder --------------------------------------
    ax = axes[0]
    ys = range(len(rows))
    for y, (label, ms, cad, color, hatched, prov) in enumerate(rows):
        if ms is None:
            ax.text(7e2, y, f"no bar — {prov}", va="center", fontsize=6.4,
                    color=MUTED, style="italic")
            continue
        if hatched:
            ax.barh(y, ms, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, ms, color=color, alpha=0.6, height=0.6,
                    edgecolor=color)
        ax.text(ms * 1.18, y, f"{fmt_age(ms)} · {prov}", va="center",
                fontsize=6.2, color=MUTED)
    ax.set_yticks(list(ys), [r[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    # extra bottom room so the two no-bar italic rows clear the x ticks
    ax.set_ylim(len(rows) - 0.5 + 0.75, -0.55)
    ax.set_xscale("log")
    ax.set_xlim(600, 1.2e9)
    ax.set_xlabel("worst age of the shown value, ms (log) — the ephemeris "
                  "behind it is at most this old")
    ax.set_title("how stale every number on the panel can get — parsed from "
                 "the live sources\n(hatched = Kconfig-paced; italic rows "
                 "have no timer; beat asserted equal in all three trees)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    # ---- (b) the same rows as checks per day ----------------------------
    ax = axes[1]
    for y, (label, ms, cad, color, hatched, prov) in enumerate(rows):
        if cad is None:
            ax.text(0.28, y, "— (no cadence: %s)" % (
                "on demand" if "Wi-Fi" in label else "pipeline age"),
                va="center", fontsize=6.4, color=MUTED, style="italic")
            continue
        v = DAY_MS / cad
        if hatched:
            ax.barh(y, v, color="white", height=0.6, edgecolor=color,
                    hatch="..", linewidth=1.1)
        else:
            ax.barh(y, v, color=color, alpha=0.6, height=0.6,
                    edgecolor=color)
        label_v = f"{v:,.0f}/d".replace(",", " ") if v >= 1 else f"{v:.2f}/d"
        ax.text(max(v, 0.5) * 1.18, y, label_v, va="center", fontsize=6.6,
                color=INK)
    ax.set_yticks(list(ys), [r[0].split("\n")[0] for r in rows], fontsize=7)
    ax.invert_yaxis()
    ax.set_ylim(len(rows) - 0.5 + 0.75, -0.55)
    ax.set_xscale("log")
    ax.set_xlim(0.25, 4e5)
    ax.set_xlabel("checks per day (log) — 86400 s / cadence, derived (one "
                  "division), nothing new measured")
    ax.set_title("how often the check runs — ~156,000 checks a day and "
                 "only three\nrows can go stale (the ADC, the radio, the "
                 "NVS snapshot); the rest is pure function")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(secs={secs} poll={poll} gauge={gauge} nvs={nvs_s}s "
          f"ntp={ntp_h}h centre={centre_off_s}s beat={beat})")


if __name__ == "__main__":
    main()
