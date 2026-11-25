#!/usr/bin/env python3
"""Generate the guard ladder for docs/verification/performance.rst.

The family's ladders count work (taps, bus bytes, pixels, assembly),
money (memory, flash, storage, writes, loudness), time (pacing,
patience, response, freshness, resolution), silicon asks (silicon),
population (assembly), ink (glyphs) and the crew itself (scheduler).
NONE of them counts the GUARDS: how many synchronization objects the
app actually owns, how many sites take them, and with what wait
policy. This ladder prices exactly that, per board, plus the presence
matrix showing which guard exists at all and why the absent ones are
absent.

The comparisons the ladder makes visible:

  * The WHOLE family's app-owned synchronization is three mutexes and
    two event groups — xQueueCreate has ZERO sites in tracked code.
    The clocks' moon pipeline runs on two mutexes (moon_ui.c); the
    knob's runs on ONE reentrant wrapper plus six SEQ_CST fences and
    a volatile watermark (its moon pipeline has no lock at all).
  * The wait policy splits: s_rgb_mtx takes are 18-of-18 unbounded
    (portMAX_DELAY) — a publish block is never allowed to fail; the
    panel mutex is the one guard with BOUNDED takes: the 2 s drain
    witness ("observed quiescence only"), the 500 ms traffic-hold
    quiesce, and the 3 s panel-io watchdog probe that names its
    holder (xSemaphoreGetMutexHolder) and esp_restart()s on overrun.
    The pacing-build drain holds the key for a 20 ms lead-in, ONE
    parsed grace window and the straight-away heal (~50 ms) — ~60x
    under the watchdog limit; the pre-pacing ladder burned a de-facto
    ~640 ms per verified flush.
  * The knob's single mutex takes ONE physical take site but serves
    SIXTEEN wrapper entries — the owner-recursion depth counter is
    what the TAP deadlock (bench 2026-11-10) forced, because "a
    FreeRTOS mutex is NOT recursively takable by its owner".
  * The presence matrix is the ladder's other half: moon_ui.c is not
    compiled on the knob at all (UI_BOARD_SRCS "" under KNOB_BOARD),
    the drain witness take is compiled out on the P4 (#if
    !defined(CONFIG_BOARD_P4) — the lost-GPSPI family is S3 silicon),
    and the two event groups bill identically to all THREE boards
    because clock_sntp.c and wifi.c are shared code (the shared-
    component precedent from the write ladder).

Every dot parsed at figure time (the house pattern); every parser
SystemExits when its fact moved:

  components/ui/moon_ui.c — 18 s_rgb_mtx takes, all portMAX_DELAY;
      9 s_panel_mtx takes with the exact policy multiset
      {portMAX_DELAY x6, FLUSH_WAIT_TIMEOUT_MS (=2000), 500,
      PANEL_IO_WATCHDOG_LIMIT_MS (=3000)}; none of the nine enclosing
      functions contains an s_rgb_mtx take (the one-way order); the
      drain body inside #if !defined(CONFIG_BOARD_P4) with its
      audited rule 3; 8 drain call sites; the panel_wdg spawn
      literal; the draw_bitmap wrapper install; the notify pair.
  knob/components/knob_ui/knob_ui.c — the reentrant wrapper
      (s_lvgl_holder/s_lvgl_depth, owner bump, non-owner give
      rejected); exactly one xSemaphoreTake in the whole file; the
      16 wrapper entries across four files; six SEQ_CST fences
      together with knob_moon.c (five in knob_ui.c — the boot movie
      added the movie_publish producer fence — plus the row
      watermark in knob_moon.c).
  components/clock/clock_sntp.c + components/wifi/wifi.c — the two
      event groups, both waits bounded (caller timeout 10 s via
      CONFIG_CLOCK_NTP_TIMEOUT_MS on the clocks, 20 s/2 s seeds on
      the knob; 8 s WIFI_CONNECTION_TIMEOUT_MS).
  main/main.cpp — bsp_display_lock(-1) the app's ONLY take of the
      vendored adapter mutex; the BSP's one-line delegation.
  components/ui/CMakeLists.txt — UI_BOARD_SRCS the four names, and
      the KNOB_BOARD leg that empties it (the knob's x-marks).
  the three generated sdkconfig.h (clocks, P4, knob — the knob's
      lives OUTSIDE the repo at ~/esp/build/knob) — the trans-queue
      depth (S3 only, = 1), the NTP timeout (all three), the orbit
      gate (unset everywhere: the "orbit batch" leg is a ghost).
"""

import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

SDKH = {
    "S3": os.path.join(ROOT, "build", "config", "sdkconfig.h"),
    "P4": os.path.join(ROOT, "build", "p4", "config", "sdkconfig.h"),
    "knob": "/home/matthias/esp/build/knob/config/sdkconfig.h",
}
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
KNOB_UI = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_ui.c")
KNOB_MAIN = os.path.join(ROOT, "knob", "main", "main.c")
KNOB_DIAL = os.path.join(ROOT, "knob", "components", "knob_ui", "knob_dial.c")
KNOB_RENDER = os.path.join(ROOT, "knob", "components", "knob_ui",
                           "knob_moon_render.c")
KNOB_MOON = os.path.join(ROOT, "knob", "components", "knob_moon",
                         "knob_moon.c")
SNTP = os.path.join(ROOT, "components", "clock", "clock_sntp.c")
CLOCK_C = os.path.join(ROOT, "components", "clock", "clock.c")
WIFI = os.path.join(ROOT, "components", "wifi", "wifi.c")
MAIN_CPP = os.path.join(ROOT, "main", "main.cpp")
BSP = os.path.join(ROOT, "components", "esp32_s3_touch_amoled_1_75",
                   "esp32_s3_touch_amoled_1_75.c")
UI_CMAKE = os.path.join(ROOT, "components", "ui", "CMakeLists.txt")
KNOB_SDKCONFIG = os.path.join(ROOT, "knob", "sdkconfig")
APP_TREES = (os.path.join(ROOT, "components"),
             os.path.join(ROOT, "main"),
             os.path.join(ROOT, "knob"))
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_locks.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
GRAY = "#6b7280"    # vendored hands (never counted as app-owned)
VIOLET = "#7c3aed"  # the moon's two keys (moon_ui.c)
AMBER = "#b07d10"  # the knob's single reentrant mux
STEEL = "#5c7fb8"  # the shared radio/NTP flags (event groups)
RED = "#b91c1c"    # the bounded watchdog probe side
BLUE = "#2563eb"
BOARD_C = {"S3": BLUE, "P4": "#0e7490", "knob": AMBER}


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(f"missing source for the ladder: {path}")
    with open(path, encoding="utf-8") as f:
        return f.read()


def expect(src: str, pattern: str, what: str, flags=0) -> re.Match:
    m = re.search(pattern, src, flags)
    if not m:
        raise SystemExit(f"{what}: pattern {pattern!r} not found — "
                         "the fact moved; re-read the source before drawing")
    return m


def count(src: str, pattern: str, what: str, want: int, flags=0) -> int:
    n = len(re.findall(pattern, src, flags))
    if n != want:
        raise SystemExit(f"{what}: found {n}, expected exactly {want} — "
                         f"pattern {pattern!r}")
    return n


def walk(*tokens: str):
    """Yield (path, content) over the app trees — managed_components is
    vendored public API and is never evidence (the family rule)."""
    for tree in APP_TREES:
        for dirpath, dirnames, files in os.walk(tree):
            dirnames[:] = [d for d in dirnames
                           if d not in ("managed_components", "build",
                                        "docs", ".git")]
            for fn in files:
                if not fn.endswith((".c", ".cpp", ".h")):
                    continue
                p = os.path.join(dirpath, fn)
                txt = _read(p)
                if any(t in txt for t in tokens):
                    yield p, txt


def enclosing_fn(src: str, line_no: int):
    """(head, body) of the enclosing function. In clang-format house
    style no line inside a body starts at column 0, so the last
    column-0, non-preprocessor, non-';' line at-or-before line_no is the
    definition's first line (signatures may wrap), and the body ends at
    the next '}' in column 0."""
    lines = src.splitlines()
    start = None
    for i in range(line_no - 1, -1, -1):
        s = lines[i]
        if s and s[0] not in " \t#" and "(" in s and not s.rstrip().endswith(";"):
            start = i
            break
    if start is None:
        raise SystemExit(f"no top-level function head found at/before "
                         f"line {line_no} — clang-format house style broken?")
    end = start
    while end + 1 < len(lines) and lines[end + 1] != "}":
        end += 1
    return lines[start], "\n".join(lines[start:end + 1])


def main() -> None:
    mu = _read(MOON_UI)
    ku = _read(KNOB_UI)
    km = _read(KNOB_MAIN)
    kd = _read(KNOB_DIAL)
    kr = _read(KNOB_RENDER)
    kmoon = _read(KNOB_MOON)
    sntp = _read(SNTP)
    clockc = _read(CLOCK_C)
    wifi = _read(WIFI)
    maincpp = _read(MAIN_CPP)
    bsp = _read(BSP)
    uicm = _read(UI_CMAKE)

    # ---- repo-wide sync-object census (the claims node's arithmetic) ----
    create_sites = []
    take_files = set()
    queue_sites = 0
    evgroup_create = 0
    evgroup_wait = 0
    fence_sites = {}
    notify_gives = 0
    notify_takes = 0
    bsp_locks = []
    for path, txt in walk("xSemaphoreCreateMutex", "xSemaphoreTake",
                          "xSemaphoreGive", "xQueueCreate",
                          "xEventGroupCreate", "xEventGroupWaitBits",
                          "__atomic_thread_fence", "xTaskNotifyGive",
                          "ulTaskNotifyTake", "bsp_display_lock"):
        rel = os.path.relpath(path, ROOT)
        n_create = len(re.findall(r"xSemaphoreCreateMutex\s*\(", txt))
        if n_create:
            create_sites += [(rel, i + 1) for i, ln in
                             enumerate(txt.splitlines())
                             if "xSemaphoreCreateMutex(" in ln]
        if re.search(r"xSemaphore(Take|Give)\s*\(", txt):
            take_files.add(rel)
        queue_sites += len(re.findall(r"xQueueCreate\s*\(", txt))
        evgroup_create += len(re.findall(r"xEventGroupCreate\s*\(", txt))
        evgroup_wait += len(re.findall(r"xEventGroupWaitBits\s*\(", txt))
        nf = len(re.findall(r"__atomic_thread_fence\s*\(", txt))
        if nf:
            fence_sites[rel] = fence_sites.get(rel, 0) + nf
        notify_gives += len(re.findall(r"xTaskNotifyGive\s*\(", txt))
        notify_takes += len(re.findall(r"ulTaskNotifyTake\s*\(", txt))
        bsp_locks += [(rel, i + 1) for i, ln in enumerate(txt.splitlines())
                      if re.search(r"\bbsp_display_lock\s*\(", ln)]
    if len(create_sites) != 3 or not all(
            (os.path.join("components", "ui", "moon_ui.c") in rel
             or rel in (os.path.join("knob", "main", "main.c"),))
            for rel, _ in create_sites):
        raise SystemExit(f"mutex creation census moved: {create_sites}")
    if take_files != {os.path.join("components", "ui", "moon_ui.c"),
                      os.path.join("knob", "components", "knob_ui",
                                   "knob_ui.c")}:
        raise SystemExit(f"semaphore take/give lives in {sorted(take_files)} "
                         "— the two-file claim no longer holds")
    if queue_sites != 0:
        raise SystemExit(f"xQueueCreate: {queue_sites} sites in app code — "
                         "the zero-queue claim no longer holds")
    if evgroup_create != 2 or evgroup_wait != 2:
        raise SystemExit(f"event groups: {evgroup_create} create / "
                         f"{evgroup_wait} wait — expected 2/2")
    if fence_sites != {os.path.join("knob", "components", "knob_ui",
                                    "knob_ui.c"): 5,
                       os.path.join("knob", "components", "knob_moon",
                                    "knob_moon.c"): 1}:
        raise SystemExit(f"SEQ_CST fence census moved: {fence_sites}")
    if notify_gives != 1 or notify_takes != 1:
        raise SystemExit(f"notify census moved: {notify_gives} give / "
                         f"{notify_takes} take — expected 1/1")
    # the app takes the vendored adapter mutex exactly once; the BSP
    # delegates once; the knob tree never mentions it.
    if [(r, l) for r, l in bsp_locks if r.startswith("knob")]:
        raise SystemExit("the knob grew a bsp_display_lock — the "
                         "single-mux story would be wrong")
    count(maincpp, r"bsp_display_lock\(-1\);", "the boot lock", 1)
    count(maincpp, r"bsp_display_unlock\(\);", "the boot unlock", 1)
    expect(bsp, r"bsp_display_lock\(uint32_t timeout_ms\)\s*\{\s*return "
               r"esp_lv_adapter_lock\(timeout_ms\);\s*\}",
           "the BSP's one-line adapter delegation")
    if [(r, l) for r, l in bsp_locks
        if not r.startswith("main") and "esp32_s3_touch_amoled_1_75" not in r]:
        raise SystemExit("bsp_display_lock appeared in an unexpected file")

    # ---- moon_ui.c: the clocks' two keys, site by site ------------------
    rgb_takes = re.findall(r"xSemaphoreTake\(\s*s_rgb_mtx\s*,\s*([^)]*)\)",
                           mu)
    if len(rgb_takes) != 18 or not all("portMAX_DELAY" in a
                                       for a in rgb_takes):
        raise SystemExit(f"s_rgb_mtx takes: {len(rgb_takes)} with policies "
                         f"{sorted(set(a.strip() for a in rgb_takes))} — "
                         "expected 18 all-portMAX_DELAY")
    pan_takes = re.findall(
        r"xSemaphoreTake\(\s*s_panel_mtx\s*,\s*"
        r"(pdMS_TO_TICKS\([^)]*\)|portMAX_DELAY)\s*\)", mu)
    policies = [a.strip() for a in pan_takes]
    want_policy = sorted(["portMAX_DELAY"] * 6 +
                         ["pdMS_TO_TICKS(FLUSH_WAIT_TIMEOUT_MS)",
                          "pdMS_TO_TICKS(500)",
                          "pdMS_TO_TICKS(PANEL_IO_WATCHDOG_LIMIT_MS)"])
    if sorted(policies) != want_policy:
        raise SystemExit(f"s_panel_mtx policies {sorted(policies)} != {want_policy}")
    expect(mu, r"#define FLUSH_WAIT_TIMEOUT_MS\s+2000\b",
           "the drain witness cap", 0)
    expect(mu, r"#define PANEL_IO_WATCHDOG_LIMIT_MS\s+3000\b",
           "the watchdog limit")
    # one-way order: the panel key is never HELD across an image-key
    # take. (1) between every s_panel take and its next give lies no
    # s_rgb take; (2) the enclosing function never takes s_rgb at all —
    # the single exception the code itself shows is moon_ui_screen_switched,
    # whose two keys run SEQUENTIALLY (panel take → give → image take);
    # the sequential order is asserted explicitly.
    mu_lines = mu.splitlines()
    for m in re.finditer(r"xSemaphoreTake\(\s*s_panel_mtx\s*,", mu):
        take_ln = mu[:m.start()].count("\n")
        give_ln = None
        for j in range(take_ln + 1, len(mu_lines)):
            if "xSemaphoreGive(s_panel_mtx" in mu_lines[j]:
                give_ln = j
                break
        if give_ln is None:
            raise SystemExit(f"s_panel take at :{take_ln + 1} has no give")
        if any("s_rgb_mtx" in mu_lines[j] for j in range(take_ln + 1, give_ln)):
            raise SystemExit(f"lock order broken: an s_rgb take sits INSIDE "
                             f"the held window :{take_ln + 1}..:{give_ln + 1}")
        head, body = enclosing_fn(mu, take_ln + 1)
        if "s_rgb_mtx" in body:
            if "moon_ui_screen_switched" not in head:
                raise SystemExit(f"lock order broken: {head.strip()!r} holds "
                                 "s_panel_mtx AND takes s_rgb_mtx")
            b_panel_t = body.index("xSemaphoreTake(s_panel_mtx")
            b_panel_g = body.index("xSemaphoreGive(s_panel_mtx")
            b_rgb_t = body.index("xSemaphoreTake(s_rgb_mtx")
            if not (b_panel_t < b_panel_g < b_rgb_t):
                raise SystemExit("moon_ui_screen_switched's two keys are no "
                                 "longer sequential — draw the nesting in "
                                 "the sketch before believing it")
    # the drain witness: S3-only body, rule 3 audited, 8 call sites
    m_def = expect(mu, r"static void moon_panel_drain_verify\(const char "
                      r"\*site\) \{", "the drain verify definition")
    drain_body = enclosing_fn(mu, mu[:m_def.start()].count("\n") + 1)[1]
    expect(drain_body, r"#if\s+!defined\(CONFIG_BOARD_P4\)",
           "the drain's S3-only #if")
    expect(drain_body, r"#else\s*\(void\)site;\s*#endif",
           "the P4 no-op leg", re.S)
    count(drain_body, r"xSemaphoreTake\(\s*s_panel_mtx", "the drain take", 1)
    n_calls = len(re.findall(r"moon_panel_drain_verify\(", mu)) - 1
    if n_calls != 8:
        raise SystemExit(f"drain call sites: {n_calls}, expected 8")
    for site in ("\"re-bake\"", "\"boot replay final\"",
                 "\"boot replay\"", "\"rebake\"", "\"orbit batch\"",
                 "\"tz re-gather\"",
                 "\"hourly\"", "\"switch fade-in\"",
                 "\"screen switch\""):
        expect(mu, re.escape(f"moon_panel_drain_verify(") + r".*" +
               re.escape(site), f"drain site {site}", re.S)
    expect(mu, r"drain call site holds it -- audited 2026-11-13",
           "the rule-3 audit note")
    # the pacing build retired the two-300 ms-poll ladder: the drain
    # now grants ONE parsed grace window and heals straight away
    m_grace = expect(mu, r"grants ONE (\d+) ms grace",
                     "the drain's single grace window")
    m_heal = expect(mu, r"heals straight away -- ~(\d+) ms",
                    "the drain heal duration")
    m_oldl = expect(mu, r"a de-facto ~(\d+) ms per verified flush",
                    "the retired ladder's per-flush cost")
    grace_ms, drain_ms, old_ladder_ms = (int(m_grace.group(1)),
                                         int(m_heal.group(1)),
                                         int(m_oldl.group(1)))
    # the watchdog
    expect(mu, r"\"panel_wdg\", 4096, NULL, 3, &s_panel_watchdog, 0",
           "the panel_wdg spawn (4096, prio 3, core 0)")
    expect(mu, r"PANEL IO STUCK", "the stuck-bus log")
    expect(mu, r"xSemaphoreGetMutexHolder\(s_panel_mtx\)",
           "the watchdog naming its holder")
    expect(mu, r"s_panel->draw_bitmap = moon_panel_draw_bitmap",
           "the draw_bitmap wrapper install")
    # the notify pair (the rest of the family needs no lock either)
    expect(mu, r"ulTaskNotifyTake\(pdTRUE, portMAX_DELAY\)",
           "the classify worker's wait")
    expect(mu, r"xTaskNotifyGive\(s_event_task\)",
           "the classify worker's wake")

    # ---- the knob: ONE reentrant mux, six fences, zero panel locks ----
    count(ku, r"xSemaphoreTake\(", "knob_ui.c takes", 1)
    count(ku, r"xSemaphoreGive\(", "knob_ui.c gives", 1)
    expect(ku, r"static TaskHandle_t s_lvgl_holder;", "the wrapper holder")
    expect(ku, r"static int s_lvgl_depth;", "the wrapper depth")
    expect(ku, r"self == s_lvgl_holder && s_lvgl_depth > 0",
           "the owner-recursion bump")
    expect(ku, r"NOT recursively takable", "the TAP deadlock rationale")
    expect(ku, r"lvgl_unlock from non-owner", "the non-owner guard")
    expect(ku, r"xSemaphoreTake\(s_lvgl_mux, portMAX_DELAY\)",
           "the wrapper's single physical take")
    expect(ku, r"void knob_ui_lvgl_lock\(void\) \{ lvgl_lock\(\); \}",
           "the public forwarder")
    # 16 entries: 8 in knob_ui.c (9 lvgl_lock(); lines minus the public
    # forwarder: the swap task's apply, invalidate_all, the [BF] probe,
    # show_no_time, show_moon's warm/cold caption x2, the last-frame
    # lander's flush, and the movie arm's stage (the frame-0 pre-ui_init
    # descriptor hand-off — the boot movie's new entry; the retired
    # reveal consumer's two entries (the band flush, the re-hide) went
    # with the strip reveal), main's park loop +
    # the 4 public takes (the transient WiFi arm, the two modem-sleep
    # swaps, alarm wake — the local forwarder definition is not an
    # entry), dial 1, render 2.
    ku_lines = ku.splitlines()
    ku_entries = sum(1 for ln in ku_lines
                     if re.search(r"\blvgl_lock\(\);", ln)
                     and "knob_ui_lvgl_lock" not in ln)
    if ku_entries != 8:
        raise SystemExit(f"knob_ui.c wrapper entries {ku_entries}, expected 8")
    count(km, r"^\s*lvgl_lock\(\);", "main.c park-loop entry", 1, re.M)
    count(km, r"knob_ui_lvgl_lock\(\);", "main.c public entries "
          "(forwarder + 4 takes)", 5)
    expect(km, r"static void lvgl_lock\(void\) \{ knob_ui_lvgl_lock\(\); \}",
           "main.c's local forwarder")
    count(kd, r"knob_ui_lvgl_lock\(\);", "dial entry", 1)
    count(kr, r"knob_ui_lvgl_lock\(\);", "seam entries (refr + home dim)", 2)
    expect(km, r"lvgl_lock\(\);\s*\n\s*uint32_t wait_ms = "
               r"lv_timer_handler\(\);", "the hold across lv_timer_handler")
    expect(km, r"s_lvgl_mux = xSemaphoreCreateMutex\(\)",
           "the knob's single creation")
    expect(km, r"knob_ui_set_lvgl_lock\(s_lvgl_mux\)", "the seam wiring")
    # the knob answers moon_ui.h itself — no panel/image mutex exists there
    count(kr, r"xSemaphoreCreateMutex|s_panel_mtx|s_rgb_mtx",
          "knob seam mutexes", 0)
    expect(kr, r"void moon_ui_init\(void\)", "the knob's moon_ui_init shim")

    # ---- the two event groups: bounded by construction ------------------
    expect(sntp, r"s_sntp_event_group = xEventGroupCreate\(\)",
           "the NTP group creation")
    expect(sntp, r"xEventGroupWaitBits\(s_sntp_event_group, SNTP_SYNC_BIT, "
                 r"pdTRUE, pdFALSE, pdMS_TO_TICKS\(timeout_ms\)\)",
           "the NTP bounded wait")
    expect(wifi, r"#define WIFI_CONNECTION_TIMEOUT_MS 8000",
           "the join timeout")
    expect(wifi, r"xEventGroupWaitBits\(s_wifi_event_group, "
                 r"WIFI_CONNECTED_BIT \| WIFI_FAIL_BIT", "the join wait")
    count(clockc, r"clock_sync_time_from_ntp\(CONFIG_CLOCK_NTP_TIMEOUT_MS\)",
          "the clocks' boot fix", 1)
    actions = _read(os.path.join(ROOT, "components", "ui", "actions.c"))
    count(actions, r"clock_sync_time_from_ntp\(CONFIG_CLOCK_NTP_TIMEOUT_MS\)",
          "the wake sync fix", 1)
    expect(km, r"#define CONFIG_KNOB_TIME_BOOT_SYNC_MS 20000",
           "the knob boot fix seed")
    count(km, r"clock_sntp_sync\(2000\)", "the knob's 2 s retry", 1)
    expect(km, r"wifi_manager_init\(\)",
           "the knob joins through the shared manager")

    # ---- CMake: the knob's x-marks --------------------------------------
    expect(uicm, r"set\(UI_BOARD_SRCS \"alarm_audio.c\" \"demo.c\" "
                 r"\"moon_ui.c\" \"moon_tfcache.c\"\)",
           "the four board-coupled TUs")
    expect(uicm, r"if\(KNOB_BOARD\)\s*\n\s*set\(UI_BOARD_SRCS \"\"\)",
           "the KNOB_BOARD emptying", re.S)

    # ---- the three headers: queue depth, NTP timeout, orbit gate --------
    q3 = re.search(r"#define CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH (\d+)",
                   _read(SDKH["S3"]))
    if not q3 or int(q3.group(1)) != 1:
        raise SystemExit("the S3 trans-queue depth is not 1 — the flush-"
                         "queue invariant (32 KiB SPI DMA cap) changed")
    p4h = _read(SDKH["P4"])
    kh = _read(SDKH["knob"])
    if "CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH" in p4h:
        raise SystemExit("the P4 header grew the S3 BSP's queue symbol — "
                         "re-check which BSP the P4 links")
    for board, txt in (("S3", _read(SDKH["S3"])), ("P4", p4h), ("knob", kh)):
        m = re.search(r"#define CONFIG_CLOCK_NTP_TIMEOUT_MS (\d+)", txt)
        if not m or int(m.group(1)) != 10000:
            raise SystemExit(f"{board}: NTP fix window is not 10000 ms")
        if "CONFIG_UI_MOON_ORBIT_ENABLE" in txt:
            raise SystemExit(f"{board}: the orbit gate is set — the "
                             "'orbit batch' ghost leg would be live; "
                             "re-read before drawing")
    expect(_read(KNOB_SDKCONFIG),
           r"# CONFIG_UI_MOON_ORBIT_ENABLE is not set",
           "the knob's explicit orbit opt-out")

    # ---- comparisons computed at figure time (nothing invented) --------
    watchdog_ms = 3000                           # asserted against the define
    headroom = watchdog_ms / drain_ms            # ~60x under the pacing build
    old_head = watchdog_ms / old_ladder_ms       # the retired ladder's ratio
    mux_ratio = 16 / 1                            # entries per physical take

    # ---- draw ------------------------------------------------------------
    # rows: (label, forever_sites, bounded_sites, policy_note, color,
    #        per-board matrix cells {board: (n_or_None, kind, short_note)})
    #   kind: "n" filled dot with count, "open" zero-with-reason, "x" absent
    rows = [
        ("s_rgb_mtx  (moon_ui.c)", 18, 0,
         "18x portMAX_DELAY — a publish block never fails", VIOLET,
         {"S3": (18, "n", "18 takes"), "P4": (18, "n", "18 takes"),
          "knob": (None, "x", "")}),
        ("s_panel_mtx  (moon_ui.c)", 6, 3,
         "6x forever + witness 2 s + quiesce 500 ms + probe 3 s", VIOLET,
         {"S3": (9, "n", "9 takes"), "P4": (8, "n", "8 — drain #if'd out"),
          "knob": (None, "x", "")}),
        ("s_lvgl_mux  (knob main.c)", 1, 0,
         "1 take, 16 entries — owner re-entry is a depth bump", AMBER,
         {"S3": (None, "x", ""), "P4": (None, "x", ""),
          "knob": (1, "n", "1 + 16")}),
        ("s_sntp_event_group  (clock_sntp.c)", 0, 1,
         "WaitBits bounded by caller: 10 s / 20 s / 2 s", STEEL,
         {"S3": (1, "n", "1 wait"), "P4": (1, "n", "1 wait"),
          "knob": (1, "n", "1 wait")}),
        ("s_wifi_event_group  (wifi.c)", 0, 1,
         "WaitBits(CONNECTED|FAIL, 8 s)", STEEL,
         {"S3": (1, "n", "1 wait"), "P4": (1, "n", "1 wait"),
          "knob": (1, "n", "1 wait")}),
        ("adapter mutex  (VENDORED)", 1, 0,
         "the app takes it exactly once: boot build, -1", GRAY,
         {"S3": (1, "n", "1 app take"), "P4": (1, "n", "1 app take"),
          "knob": (None, "x", "")}),
        ("BSP trans queue  (VENDORED)", 0, 0,
         "not a take: CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH = 1 (S3)", GRAY,
         {"S3": (1, "n", "depth 1"), "P4": (None, "open", ""),
          "knob": (None, "x", "")}),
        ("xQueueCreate  (app)", 0, 0,
         "ZERO — the family hands off on fences + flags", GRAY,
         {"S3": (None, "open", ""), "P4": (None, "open", ""),
          "knob": (None, "open", "")}),
    ]
    fig, axes = plt.subplots(1, 2, figsize=(11.6, 5.2),
                             gridspec_kw={"width_ratios": [1.45, 1]})

    # ---- (a) the wait-site ladder ---------------------------------------
    ax = axes[0]
    ys, ylab = [], []
    for r, (label, forever, bounded, note, color, _cells) in enumerate(rows):
        vendored = "VENDORED" in label
        ys.append(r)
        ylab.append(label)
        style_ls = ":" if vendored else "-"
        ax.barh(r, forever, height=0.52, color="white" if forever == 0
                else color, alpha=0.8, edgecolor=color, linestyle=style_ls,
                linewidth=1.1, zorder=3)
        if forever and bounded:
            ax.barh(r, bounded, left=forever, height=0.52, color="white",
                    edgecolor=RED, linestyle=style_ls, hatch="///",
                    linewidth=1.0, zorder=3)
        elif bounded:
            ax.barh(r, bounded, height=0.52, color="white", edgecolor=RED,
                    linestyle=style_ls, hatch="///", linewidth=1.0, zorder=3)
        # the family's text-vs-bar rule: a long solid bar carries its note
        # IN-bar in white; every short bar (1-wide takes, 1-wide hatches,
        # zero-width ghosts) keeps its note OUTSIDE the bar's right edge so
        # no glyph ever rides a hatch or a fill (assembly-ladder precedent)
        if forever >= 8:
            ax.text(0.3, r, note, va="center", ha="left", fontsize=5.4,
                    color="white", zorder=4)
        else:
            ax.text(forever + bounded + 0.18, r, note, va="center",
                    ha="left", fontsize=5.4, color=MUTED, zorder=4)
    ax.set_yticks(ys)
    ax.set_yticklabels(ylab, fontsize=6.4, color=INK)
    ax.set_xlim(0, 19)
    ax.set_ylim(len(rows) - 0.45, -0.75)
    ax.set_xlabel("app-owned wait sites per board (solid = portMAX_DELAY, "
                  "hatched = bounded timeout)", fontsize=6.6, color=MUTED)
    ax.tick_params(axis="x", labelsize=6, colors=MUTED)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.grid(axis="x", color=HAIR, linewidth=0.5, zorder=0)
    ax.text(12.2, len(rows) - 1.32,
            f"watchdog headroom: drain hold ~{drain_ms} ms against the\n"
            f"{watchdog_ms} ms bounded probe = {headroom:.0f}x "
            f"(one {grace_ms} ms grace;\nthe retired ladder burned "
            f"~{old_ladder_ms} ms: {old_head:.1f}x)",
            fontsize=5.6, color=RED, va="top")
    ax.text(12.2, len(rows) - 0.62,
            f"the knob's one physical take serves {mux_ratio:.0f} wrapper "
            "entries;\nthe clocks' app takes the vendored adapter lock once",
            fontsize=5.6, color=MUTED, va="top")
    ax.set_title("every synchronization object the app owns — 3 mutexes, "
                 "2 event groups,\nzero queues; the bounded takes are all "
                 "on the panel key")

    # ---- (b) the presence matrix ----------------------------------------
    ax = axes[1]
    boards = ("S3", "P4", "knob")
    for r, (label, _f, _b, _n, color, cells) in enumerate(rows):
        for c, board in enumerate(boards):
            n, kind, short = cells[board]
            if kind == "x":
                ax.scatter([c], [r], marker="x", s=34, color=HAIR, zorder=3)
                continue
            if kind == "open":
                ax.scatter([c], [r], marker="o", s=64, facecolor="white",
                           edgecolor=color, linewidth=1.4, zorder=3)
                continue
            ax.scatter([c], [r], marker="o", s=64, color=color,
                       edgecolor=INK, linewidth=0.6, zorder=3)
            ax.text(c, r - 0.34, short, ha="center", va="top", fontsize=5.4,
                    color=INK)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([lab for lab, *_ in rows], fontsize=6.0, color=INK)
    ax.set_xticks(range(len(boards)))
    ax.set_xticklabels(boards, fontsize=7.5, color=INK)
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.set_ylim(len(rows) - 0.45, -0.75)
    ax.set_xlim(-0.55, 2.85)
    ax.set_title("the presence matrix — filled = built and taken; open = "
                 "zero with the\nparsed reason; x = the guard is not built "
                 "on this board\n(moon_ui.c excluded on the knob; the drain "
                 "take excluded on the P4)", fontsize=8)

    fig.tight_layout()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fig.savefig(OUT, dpi=200, bbox_inches="tight")
    print(f"wrote {os.path.relpath(OUT, ROOT)}  (rgb 18 forever; panel 6+3 "
          f"[2000/500/3000 ms]; mux 1 take / 16 entries; fences 6; notify "
          f"1+1; drain 8 sites [S3-only]; watchdog headroom {headroom:.0f}x; "
          f"queue sites 0; P4 drain takes 8)")


if __name__ == "__main__":
    main()
