#!/usr/bin/env python3
"""Generate the scheduler ladder for docs/verification/performance.rst.

The other ladders measure how much work, how often, how bright, how
much memory, how loud and how patient. This one measures who gets
there FIRST: the FreeRTOS priority ladder of every app task the shipped
firmware creates, parsed straight out of the xTaskCreate* call sites —
priority, stack, core pinning and the TWDT posture the code's own
comments attest. The comparison it makes visible is the render-path
inversion between the two families: the clocks' bake PRODUCERS
(configMAX_PRIORITIES-3, core 1) OUTRANK the LVGL task (adapter
default 6) because the while-black hand-off lets the renderer wait,
while the knob's producer (5) and even its swap task (7) RANK BELOW
its LVGL task (10) because there the renderer is the foreground and
the producer only fills a back buffer nobody is waiting on.

Every number is PARSED at figure time (the house pattern):

  #define configMAX_PRIORITIES ( 25 )     $IDF/components/freertos/config/
                                          include/freertos/FreeRTOSConfig.h:93
  #define ESP_TASK_PRIO_MAX (configMAX_PRIORITIES)      esp_task.h:27
  #define ESP_TASK_TIMER_PRIO (ESP_TASK_PRIO_MAX - 3)   esp_task.h:46
      (so the esp_timer daemon lands in the PRODUCER band — drawn as
       the band note the two clusters share)
  configMAX_PRIORITIES - 3 / - 4                       components/ui/moon_ui.c
      (1509 rebake, 1692 hourly, 2084 orbit (off by default), 2614 the
       core-1 classify worker at MAX-4 — "below the bake tasks", the
       producers' 12288/12288 pair, panel_wdg 3/core 0 at 2595)
  ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY 6 / _STACK_SIZE (8 * 1024) /
  _TASK_CORE_ID (-1)          managed_components/espressif__esp_lvgl_adapter/
                              include/esp_lv_adapter.h:102-:104
      (the clocks' "lvgl" worker's affinity is PER BOARD: the S3 BSP
       overrides the adapter default itself — "cfg->lv_adapter_cfg.
       task_core_id = 0;" components/esp32_s3_touch_amoled_1_75/
       esp32_s3_touch_amoled_1_75.c:590, its comment :581-590 quoting
       the starvation the default invites — while the P4 branch of
       main.cpp overrides ONLY the stack ("p4_lv_cfg.task_stack_size",
       main/main.cpp:565) and its painter keeps core -1, "no affinity";
       the generator asserts the BSP line AND the absence of a P4-side
       task_core_id, so the split is re-read every figure time)
  play_task 4 / wifi_scan 5 / wifi_sync 6144-prio-10   alarm_audio.c:337,
                              wifi.c:551, actions.c:146
  knob_lvgl 10, boot_arm 6, swap 7, moon 5 (core 1, SPIRAM,
  "unregistered from TWDT"), enc 5, dial 5, power 4      knob/main/main.c:487
                              /:499, knob_ui.c:711/:714,
                              knob_board_encoder.c:88, knob_dial.c:140,
                              knob_power.c:283
  CONFIG_ESP_TASK_WDT_TIMEOUT_S 5, CHECK_IDLE_TASK_CPU0 1
                              (build/config/sdkconfig.h) — and the CPU1
                              check ABSENT on the clocks but =y on the
                              knob: the asymmetry the moon_event comment
                              claims ("IDLE1 is not WDT-watched") and
                              the knob producer comment contradicts
                              (check ON, panic on expiry), read from the
                              two generated headers
  CONFIG_FREERTOS_TIMER_TASK_PRIORITY 1, IDLE 0          same headers

NOT drawn: hang_watch (xTaskCreate at main.cpp:748 sits under
#if CONFIG_TEST_HANG_WATCH — a test hook, not shipped); the knob's
silent alarm stub creates no play_task; the S3 and P4 share the clock
column (identical call sites; the two deltas — the P4's 12 KiB LVGL
stack and the painter's per-board core — are printed on its row).

Right panel: the SAME rows as headroom over each family's own render
task (prio - p_lvgl) — derived, one subtraction, nothing new measured:
above the line, a task can preempt the renderer; below it, the
renderer is the foreground.

Run:  docs/.venv/bin/python docs/tools/gen_perf_sched.py
"""
from __future__ import annotations

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
IDF = os.environ.get("IDF_PATH", os.path.expanduser("~/esp/esp-idf"))

FREERTOS_CONFIG = os.path.join(
    IDF, "components", "freertos", "config", "include", "freertos",
    "FreeRTOSConfig.h")
ESP_TASK_H = os.path.join(IDF, "components", "esp_system", "include",
                          "esp_task.h")
ADAPTER_H = os.path.join(ROOT, "managed_components",
                         "espressif__esp_lvgl_adapter", "include",
                         "esp_lv_adapter.h")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
ALARM_AUDIO = os.path.join(ROOT, "components", "ui", "alarm_audio.c")
WIFI = os.path.join(ROOT, "components", "wifi", "wifi.c")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
MAIN_CPP = os.path.join(ROOT, "main", "main.cpp")
BSP_S3 = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                      "esp32_s3_touch_amoled_1_75.c")
KNOB_MAIN = os.path.join(ROOT, "knob", "main", "main.c")
KNOB_UI = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_ui.c")
KNOB_ENC = os.path.join(ROOT, "knob", "components", "knob_board",
                        "knob_board_encoder.c")
KNOB_DIAL = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_dial.c")
KNOB_POWER = os.path.join(ROOT, "knob", "components", "knob_power",
                          "knob_power.c")
SDKH = os.path.join(ROOT, "build", "config", "sdkconfig.h")
KNOB_SDKH = "/home/matthias/esp/build/knob/config/sdkconfig.h"
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_sched.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
GRAY = "#6b7280"   # system daemons and boot-only
TEAL = "#0e7490"   # the radio's tasks
RED = "#b91c1c"    # the panel-bus watchdog
BLUE = "#2563eb"   # render / UI-side tasks
VIOLET = "#7c3aed" # the producers (the bake band)
AMBER = "#b07d10"  # the knob's input tasks


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"missing source for the ladder: {path}")
    with open(path, encoding="utf-8") as f:
        return f.read()


def define_int(path: str, symbol: str, what: str) -> int:
    # values are bare ints or products like "( 8 * 1024 )" — the
    # adapter's stack size is the latter
    m = re.search(rf"#define {symbol}\s+\(?\s*(-?\d+)(?:\s*\*\s*(\d+))?",
                  _read(path))
    if not m:
        raise SystemExit(f"{what} ({symbol}): no #define in {path}")
    return int(m.group(1)) * (int(m.group(2)) if m.group(2) else 1)


def config_int(path: str, symbol: str, what: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(path))
    if not m:
        raise SystemExit(f"{what} ({symbol}): not in {path}")
    return int(m.group(1))


def macro_defined(path: str, symbol: str) -> bool:
    return re.search(rf"^#define {symbol}\b", _read(path), re.M) is not None


CREATE_RE = re.compile(r"xTaskCreate(?P<kind>\w*)\s*\(")


def _split_args(s: str) -> list[str]:
    """Split a call's argument string on top-level commas (paren- and
    brace-aware), so a param like (void *)(intptr_t)(x ? 1 : 0) — which
    CONTAINS a comma — stays one field."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "({[":
            depth += 1
        elif ch in ")}]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


def task_at(path: str, tname: str, what: str) -> dict:
    """The xTaskCreate* call whose TASK NAME (second argument) is
    *tname*, with its stack, priority expression, pinned core (None =
    unaffine) and stack caps parsed from the live call site."""
    src = _read(path)
    for m in CREATE_RE.finditer(src):
        # collect the balanced call body from the opening paren
        i, depth = m.end() - 1, 0
        while True:
            if src[i] == "(":
                depth += 1
            elif src[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        args = _split_args(src[m.end():i])
        if len(args) < 6 or args[1] != f'"{tname}"':
            continue
        kind = m.group("kind")
        pinned = "PinnedToCore" in kind
        if len(args) != (8 if "WithCaps" in kind else 7 if pinned else 6):
            raise SystemExit(f"{what}: unexpected xTaskCreate{kind} arity "
                             f"{len(args)} for \"{tname}\"")
        core = int(args[6]) if pinned else None
        caps = args[7] if "WithCaps" in kind else None
        return dict(kind=kind, stack=int(args[2]), prio_expr=args[4],
                    core=core, caps=caps,
                    spiram="SPIRAM" in (caps or ""),
                    pinned=pinned)
    raise SystemExit(f"{what}: xTaskCreate...\"{tname}\" not found in {path}")


plt.rcParams.update({
    "font.size": 8, "axes.edgecolor": MUTED, "axes.labelcolor": INK,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.titlesize": 8.5,
    "axes.titlecolor": INK, "figure.facecolor": "white",
})


def main() -> None:
    max_prio = define_int(FREERTOS_CONFIG, "configMAX_PRIORITIES",
                          "FreeRTOS max priorities")
    # the system defines are SYMBOLIC — parse the shape, not a number
    src = _read(ESP_TASK_H)
    if not re.search(r"#define ESP_TASK_PRIO_MAX\s+\(\s*configMAX_PRIORITIES"
                     r"\s*\)", src):
        raise SystemExit("ESP_TASK_PRIO_MAX is no longer "
                         "(configMAX_PRIORITIES) in esp_task.h — re-read it")
    m = re.search(r"#define ESP_TASK_TIMER_PRIO\s+\(\s*ESP_TASK_PRIO_MAX\s*-\s*"
                  r"(\d+)", src)
    if not m:
        raise SystemExit("ESP_TASK_TIMER_PRIO is no longer "
                         "(ESP_TASK_PRIO_MAX - N) in esp_task.h — re-read it")
    esp_timer_off = int(m.group(1))
    lv_prio = define_int(ADAPTER_H, "ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY",
                         "adapter LVGL priority")
    lv_stack = define_int(ADAPTER_H, "ESP_LV_ADAPTER_DEFAULT_STACK_SIZE",
                          "adapter LVGL stack")
    lv_core = define_int(ADAPTER_H, "ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID",
                         "adapter LVGL core")
    main_src = _read(MAIN_CPP)
    m4 = re.search(r"p4_lv_cfg\.task_stack_size\s*=\s*(\d+)", main_src)
    if not m4:
        raise SystemExit("P4 LVGL stack override (p4_lv_cfg.task_stack_size) "
                         "not found in main.cpp")
    p4_lv_stack = int(m4.group(1))
    p4_lv_line = main_src.count("\n", 0, m4.start()) + 1
    # the per-board painter affinity: the S3 BSP pins the adapter task
    # itself; the P4 branch must NOT (its painter rides the adapter
    # default) — both halves are asserted, so the row cannot silently
    # drift back to a shared "no affinity" lie
    bsp_src = _read(BSP_S3)
    m0 = re.search(r"cfg->lv_adapter_cfg\.task_core_id\s*=\s*(-?\d+)\s*;",
                   bsp_src)
    if not m0:
        raise SystemExit("S3 BSP LVGL core pin (cfg->lv_adapter_cfg."
                         "task_core_id) not found in the BSP — the affinity "
                         "split moved; re-read esp32_s3_touch_amoled_1_75.c")
    s3_lv_core = int(m0.group(1))
    s3_lv_line = bsp_src.count("\n", 0, m0.start()) + 1
    if re.search(r"task_core_id", main_src):
        raise SystemExit("main.cpp now touches task_core_id — the P4 painter "
                         "no longer rides the adapter default; update the "
                         "LVGL row and the crew sketch")
    wdt_timeout = config_int(SDKH, "CONFIG_ESP_TASK_WDT_TIMEOUT_S",
                             "task WDT timeout")
    wdt_cpu0 = config_int(SDKH, "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0",
                          "WDT CPU0 check")
    wdt_cpu1_clocks = macro_defined(SDKH,
                                    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1")
    wdt_cpu1_knob = macro_defined(KNOB_SDKH,
                                  "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1")
    timer_prio = config_int(SDKH, "CONFIG_FREERTOS_TIMER_TASK_PRIORITY",
                            "timer daemon priority")

    def resolve(expr: str) -> int:
        m = re.fullmatch(r"configMAX_PRIORITIES\s*-\s*(\d+)", expr)
        if m:
            return max_prio - int(m.group(1))
        if expr.isdigit():
            return int(expr)
        raise SystemExit(f"unexpected priority expression: {expr!r}")

    # ---- the shipped task set, parsed from the live call sites --------
    clocks: list[tuple] = []   # (label, prio, stack, core, color, note, prov)
    knob: list[tuple] = []

    producers = [task_at(MOON_UI, n, w) for n, w in (
        ("moon_rebake", "deferred re-bake producer"),
        ("moon_hourly", "hourly producer"),
        ("moon_replay", "boot replay producer"))]
    pr_prios = {resolve(t["prio_expr"]) for t in producers}
    if len(pr_prios) != 1:
        raise SystemExit(f"producers not one band: {pr_prios}")
    clocks.append((f"rebake + hourly + replay\nproducers (3 tasks)",
                   pr_prios.pop(), 12288, 1, VIOLET, "",
                   "moon_ui.c:1509/:1692/:2673"))
    ev = task_at(MOON_UI, "moon_event", "sky-event worker")
    clocks.append(("classify worker\nmoon_event", resolve(ev["prio_expr"]),
                   ev["stack"], ev["core"], VIOLET,
                   "SPIRAM stack" if ev["spiram"] else "",
                   "moon_ui.c:2614"))
    sync = task_at(ACTIONS, "wifi_sync", "wifi sync")
    clocks.append(("wifi sync\naction_start_wifi_sync",
                   resolve(sync["prio_expr"]), sync["stack"], None, TEAL, "",
                   "actions.c:146"))
    knob.append(("wifi sync\n(shared actions.c)",
                 resolve(sync["prio_expr"]), sync["stack"], None, TEAL, "",
                 "actions.c:146"))
    clocks.append(("LVGL worker\n(esp_lv_adapter)", lv_prio, lv_stack,
                   None, BLUE,
                   f"S3 core {s3_lv_core} (BSP :{s3_lv_line}) · P4 core "
                   f"{lv_core} · P4 stack {p4_lv_stack}",
                   f"adapter :102-:104, main.cpp:{p4_lv_line}"))
    klv = task_at(KNOB_MAIN, "knob_lvgl", "knob LVGL task")
    knob_prio = resolve(klv["prio_expr"])
    knob.append(("LVGL\nknob_lvgl_task", knob_prio, klv["stack"], None, BLUE,
                 "own task", "knob/main/main.c:487"))
    scan = task_at(WIFI, "wifi_scan", "wifi scan")
    clocks.append(("wifi scan\nbounded retry", resolve(scan["prio_expr"]),
                   scan["stack"], None, TEAL, "", "wifi.c:551"))
    knob.append(("wifi scan\n(shared wifi.c)", resolve(scan["prio_expr"]),
                 scan["stack"], None, TEAL, "", "wifi.c:551"))
    play = task_at(ALARM_AUDIO, "alarm_audio", "alarm ring")
    clocks.append(("alarm ring\nplay_task", resolve(play["prio_expr"]),
                   play["stack"], None, BLUE, "ES8311 stream",
                   "alarm_audio.c:337"))
    wdg = task_at(MOON_UI, "panel_wdg", "panel-io watchdog")
    clocks.append(("panel-io watchdog\n3 s limit", resolve(wdg["prio_expr"]),
                   wdg["stack"], wdg["core"], RED, "", "moon_ui.c:2595"))
    enc = task_at(KNOB_ENC, "knob_enc", "dial encoder poller")
    knob.append(("dial encoder\nknob_enc 3 ms", resolve(enc["prio_expr"]),
                 enc["stack"], None, AMBER, "pulse-pair",
                 "knob_board_encoder.c:88"))
    dial = task_at(KNOB_DIAL, "knob_dial", "dial consumer")
    knob.append(("dial consumer\nknob_dial 20 ms", resolve(dial["prio_expr"]),
                 dial["stack"], None, AMBER, "step + dim",
                 "knob_dial.c:140"))
    km = task_at(KNOB_UI, "knob_moon", "knob producer")
    knob.append(("knob producer\nknob_moon (back buffer)",
                 resolve(km["prio_expr"]), km["stack"], km["core"], VIOLET,
                 "TWDT-unregistered · SPIRAM", "knob_ui.c:711"))
    swap = task_at(KNOB_UI, "knob_swap", "knob swap")
    knob.append(("swap publisher\nknob_swap", resolve(swap["prio_expr"]),
                 swap["stack"], None, BLUE, "registered on purpose",
                 "knob_ui.c:714"))
    boot = task_at(KNOB_MAIN, "knob_boot", "boot arm")
    knob.append(("boot arm\nknob_boot (one-shot)", resolve(boot["prio_expr"]),
                 boot["stack"], None, GRAY, "boot-only", "knob/main/main.c:499"))
    sup = task_at(KNOB_POWER, "knob_power", "power supervisor")
    knob.append(("power supervisor\nknob_power", resolve(sup["prio_expr"]),
                 sup["stack"], None, GRAY, "ladder rungs",
                 "knob_power.c:283"))
    # system rows, drawn once spanning both columns
    syst = [
        ("esp_timer daemon", max_prio - esp_timer_off,
         "prio = PRIO_MAX-3 (esp_task.h:46)"),
        ("timer daemon", timer_prio,
         "CONFIG_FREERTOS_TIMER_TASK_PRIORITY (sdkconfig.h)"),
        ("IDLE0 · IDLE1", 0,
         f"WDT {wdt_timeout} s · clocks: CPU0 "
         f"{'watched' if wdt_cpu0 else 'unwatched'}, CPU1 "
         f"{'watched' if wdt_cpu1_clocks else 'NOT watched'} · knob: CPU1 "
         f"{'CHECKED (panic)' if wdt_cpu1_knob else 'not watched'}"),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.2, 4.9))

    # ---- (a) the ladder -------------------------------------------------
    # Slot roster: every row gets its own y slot (rows sorted by priority,
    # highest first) so the many co-equal rows never collide; the priority
    # itself rides each row as a "pNN" prefix, the role as the dot colour.
    # The centre gutter (between the wall lines) carries the system daemons
    # shared by both families; each family's tasks sit outside its wall.
    ax = axes[0]
    rows_all = ([("clock", *r) for r in clocks]
                + [("knob", *r) for r in knob]
                + [("system", label, prio, None, None, GRAY, "", prov)
                   for label, prio, prov in syst])
    rows_all.sort(key=lambda r: -r[2])          # prio; stable within ties
    n = len(rows_all)
    WALL, DOT, TXT = 1.7, 2.0, 2.25             # wall / dot / label |x|
    band = [i for i, r in enumerate(rows_all) if r[2] == max_prio - 3]
    ax.axhspan(min(band) - 0.5, max(band) + 0.5, color=VIOLET, alpha=0.08,
               zorder=0)
    for y, (side, label, prio, stack, core, color, note, prov) in \
            enumerate(rows_all):
        if side == "system":
            ax.axhline(y, color=GRAY, linewidth=0.6, linestyle=":",
                       alpha=0.55, zorder=1)
            ax.scatter([0], [y], marker="d", s=14, color=GRAY, zorder=3)
            ax.text(0, y, f"p{prio}  {label} — {prov}", ha="center",
                    va="center", fontsize=5.4, color=GRAY, zorder=4,
                    bbox=dict(facecolor="white", edgecolor="none", pad=1.2))
            continue
        left = side == "clock"
        dotted = "TWDT-unregistered" in note
        ax.scatter([(-DOT if left else DOT)], [y],
                   s=16 + 55 * (stack / 12288.0),
                   color=color if not dotted else "white", alpha=0.9,
                   edgecolor=color, linewidth=1.3,
                   linestyle=":" if dotted else "-", zorder=3)
        ha = "right" if left else "left"
        x = -TXT if left else TXT
        ax.text(x, y - 0.21, f"p{prio}  {label.split(chr(10))[0]}",
                ha=ha, va="center", fontsize=5.9, color=INK)
        ax.text(x, y + 0.24,
                f"{label.split(chr(10))[1] if chr(10) in label else ''}"
                f"{'  ·  ' if chr(10) in label else ''}"
                f"{stack // 1024} KiB · core {'any' if core is None else core}"
                f"{(' · ' + note) if note else ''} · {prov}",
                ha=ha, va="center", fontsize=5.2, color=MUTED)
    ax.text(-DOT, -1.0, "the clocks (S3 + P4)", ha="center", fontsize=8.5,
            color=INK)
    ax.text(DOT, -1.0, "the knob", ha="center", fontsize=8.5, color=INK)
    ax.text(0, -1.85,
            f"the producer band = configMAX_PRIORITIES−3 = {max_prio - 3} "
            "(shaded) — the esp_timer daemon shares it (esp_task.h:46)",
            ha="center", fontsize=6.0, color=VIOLET)
    ax.text(-TXT, n + 0.6,
            "not drawn: hang_watch (main.cpp:748, #if CONFIG_TEST_HANG_WATCH —\n"
            "test hook, never shipped); the knob's alarm is its silent stub\n"
            "(no play_task). Dotted edge = TWDT-unregistered (the knob's\n"
            "producer, knob_ui.c:701 comment). S3 and P4 share the clock\n"
            "column — its two deltas ride the LVGL row: the P4's 12 KiB\n"
            "stack, and the painter's core (S3 pinned to 0 by the BSP's own\n"
            "line, P4 kept at the adapter default).",
            fontsize=5.7, color=MUTED, ha="left", va="top")
    ax.set_xlim(-4.6, 4.6)
    ax.set_ylim(n + 4.6, -2.4)
    ax.set_yticks([])
    ax.set_xticks([])
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.axvline(-WALL, color=HAIR, linewidth=0.7)   # gutter walls
    ax.axvline(WALL, color=HAIR, linewidth=0.7)
    ax.set_title("the scheduler ladder — every xTaskCreate* call site parsed\n"
                 "(rows sorted by priority, pNN on each row; dot size ∝ stack)")

    # ---- (b) headroom over the family's own render task -----------------
    ax = axes[1]
    rows = []
    for label, prio, stack, core, color, note, prov in clocks:
        rows.append((f"{label.split(chr(10))[0]} (clocks)",
                     prio - lv_prio, color,
                     "TWDT-unregistered" in note))
    for label, prio, stack, core, color, note, prov in knob:
        rows.append((f"{label.split(chr(10))[0]} (knob)",
                     prio - knob_prio, color, "TWDT-unregistered" in note))
    for label, prio, prov in syst:
        rows.append((f"{label.split(' ·')[0]}", prio - lv_prio, GRAY, False))
    rows.sort(key=lambda r: -r[1])
    ys = list(range(len(rows)))
    for y, (label, d, color, dotted) in enumerate(rows):
        ax.barh(y, d, color="white" if dotted else color, alpha=0.75,
                height=0.62, edgecolor=color, linewidth=1.2,
                linestyle=":" if dotted else "-", zorder=3)
        ax.text(d + (0.5 if d >= 0 else -0.5), y, f"{d:+d}",
                va="center", ha="left" if d >= 0 else "right", fontsize=6.4,
                color=INK)
    ax.set_yticks(ys, [r[0] for r in rows], fontsize=6.4)
    ax.axvline(0, color=INK, linewidth=1.0)
    ax.text(0.4, -0.7, f"0 = the family's own LVGL task\n(clock {lv_prio}, "
            f"knob {knob_prio})", fontsize=6.0, color=INK, va="top")
    ax.set_ylim(len(rows) - 0.5, -1.3)
    ax.set_xlabel("prio − LVGL-task prio, by family — above the line a task\n"
                  "can preempt the renderer; below it the renderer is "
                  "foreground")
    ax.set_title("who outranks the renderer — derived\n"
                 "(one subtraction, nothing new measured)")
    ax.grid(axis="x", color=HAIR, linewidth=0.6)
    ax.set_axisbelow(True)

    fig.tight_layout()
    fig.savefig(OUT, dpi=160, facecolor="white", bbox_inches="tight",
                pad_inches=0.08)
    print(f"wrote {os.path.relpath(OUT, HERE)}  "
          f"(max_prio={max_prio} esp_timer={max_prio - esp_timer_off} "
          f"lvgl={lv_prio}/{lv_stack} p4_lvgl={p4_lv_stack} "
          f"wdt={wdt_timeout}s cpu1_clocks={wdt_cpu1_clocks} "
          f"cpu1_knob={wdt_cpu1_knob})")


if __name__ == "__main__":
    main()
