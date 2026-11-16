#!/usr/bin/env python3
"""Generate the silicon ladder for docs/verification/performance.rst.

The family's ladders count work (taps, bus bytes, pixels, assembly),
money (memory, flash, storage, writes, loudness) and time (pacing,
patience, response, freshness, resolution). NONE of them counts how
OFTEN the firmware interrogates its silicon — the per-chip ask rate
each hand pays while the screen is awake. This ladder prices exactly
that: interrogations per day, parsed from the live call sites, per
board, plus the per-board presence matrix showing which leg exists at
all and why the absent ones are absent.

The comparison the ladder makes visible is the GAP between what a chip
can say and what the firmware ever asks it: the QMI8658C streams at
1000 Hz (the parsed CTRL2 setting) and is asked 86,400 times/day —
ONE sample in 1000; the EC1 dial is nominally polled every 3 ms but
the knob's 100 Hz tick clamp (pdMS_TO_TICKS(3) == 0, then
poll < 1 ? 1 : poll) lands it at an effective 10 ms — the clamp pays
3x. And the matrix's open circles are the ladder's other half: the
same 1.3 s clock leg is a real 66,462/day I2C interrogation on the S3
(expander INT pin + PCF85063A CTRL2, TWO chip reads per poll), a
compiled-out no-op on the P4, and a software metronome with no chip
behind it on the knob.

Every bar parsed at figure time (the house pattern):

  #define CONFIG_LV_DEF_REFR_PERIOD 33   build/config/sdkconfig.h,
                              build/p4/config/sdkconfig.h,
                              /home/matthias/esp/build/knob/config/sdkconfig.h
      (asserted EQUAL across all three, like the response ladder; the
      indev read timer is created with it — lv_indev.c:105
      "indev->read_timer = lv_timer_create(lv_indev_read_timer_cb,
      LV_DEF_REFR_PERIOD" — so every touch chip is read once a beat)
  CST9217 (lock-only: dependencies.lock entry asserted present, the
      driver itself is never read — no address fabricated), GT911
      @0x5D/0x14 (managed esp_lcd_touch_gt911.h:42-:43, on disk),
      CST816S @0x15 polled (knob_board_touch.c:23 "the vendor demo
      polls too"; its INT 9 is the EXT1 tap-wake, not a poll)
  QMI8658C: probe 0x6A..0x6B + WHOAMI 0x05 (imu_qmi8658.c:205/:45),
      accel "@ 1000 Hz" / "1000 Hz = 3" (the parsed chip ODR), the
      not-found line "QMI8658 not found on the I2C bus (0x6A/0x6B);
      moon tilt disabled" (:254), and the ask side — lv_timer_create
      (moon_tilt_monitor_cb, 1000) at moon_ui.c:2729. P4 zero: the
      chip is not on the board — asserted by docs/devices/p4.rst
      NEVER mentioning QMI8658 while docs/devices/s3.rst does; knob
      zero: imu_stub.c "imu_is_ready(void) { return false; }".
  RTC: #define RTC_POLL_INTERVAL_MS 1300 (main/main.cpp:30) feeding
      lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS)
      (:681); clock_interrupt.c EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3,
      the three #if !CONFIG_CLOCK_USE_PCF85063A legs, PCF85063A_ADDRESS
      0x51 (managed pcf85063a.h:15). P4 zero: the macro UNDEFINED in
      build/p4/config/sdkconfig.h (and DEFINED in the S3 header); knob
      zero: NO pcf85063a token in the knob's OWN sources (the vendored
      managed_components package is never linked) AND the literal
      lv_timer_create(clock_rtc_interrupt_handler_cb, 1300) at
      knob/main/main.c:526 ("RTC_POLL_INTERVAL_MS on the clocks is
      1300", :525).
  microSD: hour centre hour * 3600 + 1800 (moon_ui.c:1808, the hourly
      probe's own epoch); S3 legs SPI3_HOST x2 + GPIO_NUM_41 CS +
      BSP_SD_CMD/D0/CLK (moon_tfcache.c:415-:440, "SPI2 is the QSPI
      panel's"); P4 leg "#if defined(CONFIG_BOARD_P4) ... return
      bsp_sdcard_mount();" (4-bit SDMMC, verbatim); knob absence
      asserted through components/ui/CMakeLists.txt: UI_BOARD_SRCS is
      exactly alarm_audio/demo/moon_ui/moon_tfcache and KNOB_BOARD
      empties it — the module never compiles there ("no SD socket").
  radio: the hourly airplane re-sync gate — current_time.minute == 0
      + clock_sntp_needs_sync + SCREEN_ID_MODEM_SLEEP (actions.c
      ~:1023); P4 crosses to the C6 (CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
      =y in the P4 header, parsed); the knob cell stays open — the
      gate is shared code but fires on the airplane screen only.
  EC1: #define ENC_POLL_MS 3 (knob_board_encoder.c:32 "vendor
      TICKS_INTERVAL") + vTaskDelay(poll < 1 ? 1 : poll) (:73) +
      #define CONFIG_FREERTOS_HZ 100 (knob sdkconfig.h) -> the
      effective poll is 1 tick = 10 ms.
  battery: lv_timer_create(battery_timer_cb, 60000) at
      knob_battery_ui.c:204; adc_oneshot on "GPIO 1 (ADC1_CH0"
      (knob_battery.c header comment).
  classify: xTaskCreatePinnedToCoreWithCaps "moon_event" at
      configMAX_PRIORITIES - 4 with MALLOC_CAP_SPIRAM (moon_ui.c:2755,
      "IDLE1 is not WDT-watched") — the 15-20 s day scan, 1/day; the
      knob has no worker (no moon_event_task token under knob/ —
      moon_ui.c is the excluded side of the CMake branch above).

Zero legs are drawn as open markers AT their group with the parsed
reason — the write ladder's zero-leg precedent — never as missing
bars. The two hatched ghosts are chip-side or nominal-only numbers
(the IMU's 86.4 M/day stream, the dial's un-clamped 28.8 M/day
nominal) and are never summed into a board's bill.

Run:  docs/.venv/bin/python docs/tools/gen_perf_silicon.py
"""
from __future__ import annotations

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
IMU_DRV = os.path.join(ROOT, "components", "imu", "imu_qmi8658.c")
IMU_STUB = os.path.join(ROOT, "knob", "components", "imu", "imu_stub.c")
MOON_UI = os.path.join(ROOT, "components", "ui", "moon_ui.c")
TFCACHE = os.path.join(ROOT, "components", "ui", "moon_tfcache.c")
UI_CMAKE = os.path.join(ROOT, "components", "ui", "CMakeLists.txt")
ACTIONS = os.path.join(ROOT, "components", "ui", "actions.c")
CLOCK_INT = os.path.join(ROOT, "components", "clock", "clock_interrupt.c")
MAIN_CPP = os.path.join(ROOT, "main", "main.cpp")
KNOB_MAIN = os.path.join(ROOT, "knob", "main", "main.c")
KNOB_ENC = os.path.join(ROOT, "knob", "components", "knob_board",
                        "knob_board_encoder.c")
KNOB_TOUCH = os.path.join(ROOT, "knob", "components", "knob_board",
                          "knob_board_touch.c")
KNOB_BATT_UI = os.path.join(ROOT, "knob", "components", "knob_ui",
                            "knob_battery_ui.c")
KNOB_BATT = os.path.join(ROOT, "knob", "components", "knob_battery",
                         "knob_battery.c")
GT911_H = os.path.join(ROOT, "managed_components",
                       "espressif__esp_lcd_touch_gt911", "include",
                       "esp_lcd_touch_gt911.h")
PCF_H = os.path.join(ROOT, "managed_components", "waveshare__pcf85063a",
                     "include", "pcf85063a.h")
LVINDEV = os.path.join(ROOT, "managed_components", "lvgl__lvgl", "src",
                       "indev", "lv_indev.c")
LOCK = os.path.join(ROOT, "dependencies.lock")
P4_RST = os.path.join(ROOT, "docs", "devices", "p4.rst")
S3_RST = os.path.join(ROOT, "docs", "devices", "s3.rst")
KNOB_TREE = os.path.join(ROOT, "knob")
OUT = os.path.join(HERE, "..", "_static", "figs", "perf_silicon.png")

INK = "#1c2128"
MUTED = "#5c6675"
HAIR = "#d9dde3"
GRAY = "#6b7280"    # bookkeeping asks (battery, day scan)
TEAL = "#0e7490"    # the IMU leg
SLATE = "#4b5563"   # the RTC/expander leg (clock-side silicon)
BLUE = "#2563eb"    # the S3 / shared beat
STEEL = "#5c7fb8"   # the radio leg (and what-if)
VIOLET = "#7c3aed"  # the moon's card legs
AMBER = "#b07d10"   # the knob's inputs
# board identity for the grouped bars (the identity colours precedent):
BOARD_C = {"S3": BLUE, "P4": TEAL, "knob": AMBER}


def _read(path: str) -> str:
    if not os.path.exists(path):
        raise SystemExit(
            f"missing source for the ladder: {path}\n"
            "managed_components/ holds the LAST-RESOLVED target's set — the "
            "vendor legs here need the P4 side, so run first:\n"
            "  idf.py -D IDF_TARGET=esp32p4 -B build/p4 reconfigure")
    with open(path, encoding="utf-8") as f:
        return f.read()


def expect(src: str, pattern: str, what: str, flags=0) -> re.Match:
    m = re.search(pattern, src, flags)
    if not m:
        raise SystemExit(f"{what}: pattern {pattern!r} not found — "
                         "the fact moved; re-read the source before drawing")
    return m


def header_int(board: str, symbol: str, what: str) -> int:
    m = re.search(rf"#define {symbol}\s+(\d+)", _read(SDKH[board]))
    if not m:
        raise SystemExit(f"{what} ({symbol}): not in the {board} header")
    return int(m.group(1))


def knob_token_absent(token: str, what: str) -> None:
    # the claim is about the knob's OWN sources — managed_components is
    # vendored public API (the knob tree ships a pcf85063a package it
    # never links), never evidence of a shipped leg
    for dirpath, _, files in os.walk(KNOB_TREE):
        if "build" in dirpath or ".git" in dirpath \
                or "managed_components" in dirpath:
            continue
        for fn in files:
            if fn.endswith((".c", ".h")):
                if token in _read(os.path.join(dirpath, fn)):
                    raise SystemExit(
                        f"{what}: token {token!r} found in knob/"
                        f"{os.path.relpath(os.path.join(dirpath, fn), ROOT)} "
                        "— the absence this leg claims is no longer true")


def main() -> None:
    DAY = 86400

    # ---- the shared beat -------------------------------------------------
    refr = {}
    for board in ("S3", "P4", "knob"):
        refr[board] = header_int(board, "CONFIG_LV_DEF_REFR_PERIOD",
                                 "LVGL refresh period")
    if len(set(refr.values())) != 1:
        raise SystemExit(f"the three headers disagree on the beat: {refr}")
    beat = refr["S3"]
    expect(_read(LVINDEV),
           r"indev->read_timer = lv_timer_create\(lv_indev_read_timer_cb, "
           r"LV_DEF_REFR_PERIOD", "the indev read timer is no longer the "
           "refresh period — the touch leg's cadence moved")
    touch_day = DAY * 1000 // beat

    # touch chips: addresses parsed where they are on disk, never invented
    expect(_read(GT911_H), r"#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS\s+"
           r"\(0x5D\)", "GT911 primary address")
    expect(_read(GT911_H), r"#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_"
           r"BACKUP\s+\(0x14\)", "GT911 backup address")
    expect(_read(KNOB_TOUCH), r"#define TOUCH_I2C_ADDR 0x15",
           "CST816S address")
    expect(_read(KNOB_TOUCH), r"the vendor demo polls too",
           "the knob's touch is no longer described as polled")
    expect(_read(LOCK), r"waveshare/esp_lcd_touch_cst9217",
           "CST9217 lock entry (driver is lock-only — no address drawn)")

    # ---- the IMU leg -----------------------------------------------------
    imu_src = _read(IMU_DRV)
    expect(imu_src, r"for \(uint8_t addr = 0x6A; addr <= 0x6B",
           "the IMU probe pair 0x6A..0x6B")
    expect(imu_src, r"#define IMU_WHOAMI_VAL 0x05", "the IMU WHO_AM_I")
    expect(imu_src, r"1000 Hz = 3", "the CTRL2 1000 Hz ODR setting")
    expect(imu_src, r"moon tilt disabled", "the probe-failure line the P4 "
           "leg cites")
    expect(_read(MOON_UI), r"lv_timer_create\(moon_tilt_monitor_cb, 1000",
           "the 1 Hz tilt monitor")
    expect(_read(IMU_STUB), r"imu_is_ready\(void\) \{ return false; \}",
           "the knob's imu shim posture")
    if "QMI8658" in _read(P4_RST):
        raise SystemExit("docs/devices/p4.rst now mentions the QMI8658 — "
                         "the P4 IMU zero-leg needs re-reading")
    if "QMI8658C" not in _read(S3_RST):
        raise SystemExit("docs/devices/s3.rst no longer mounts the QMI8658C")
    imu_day = DAY                      # the parsed 1000 ms lv_timer
    imu_chip_day = DAY * 1000          # the parsed 1000 Hz stream (ghost)

    # ---- the RTC leg -----------------------------------------------------
    main_src = _read(MAIN_CPP)
    m = expect(main_src, r"#define RTC_POLL_INTERVAL_MS (\d+)",
               "the RTC poll interval")
    rtc_ms = int(m.group(1))
    expect(main_src, r"lv_timer_create\(clock_rtc_interrupt_handler_cb, "
           r"RTC_POLL_INTERVAL_MS", "the clock tick timer")
    ci = _read(CLOCK_INT)
    expect(ci, r"#define EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3",
           "the expander INT pin leg")
    if ci.count("#if !CONFIG_CLOCK_USE_PCF85063A") != 3:
        raise SystemExit("the P4 legs of clock_interrupt.c are no longer "
                         "three — re-read the RTC gating")
    expect(_read(PCF_H), r"#define PCF85063A_ADDRESS 0x51",
           "the PCF85063A address")
    if re.search(r"^#define CONFIG_CLOCK_USE_PCF85063A\b", _read(SDKH["P4"]),
                 re.M):
        raise SystemExit("the P4 header now DEFINES CONFIG_CLOCK_USE_PCF85063A"
                         " — the compiled-out zero-leg is no longer zero")
    if not re.search(r"^#define CONFIG_CLOCK_USE_PCF85063A\b",
                     _read(SDKH["S3"]), re.M):
        raise SystemExit("the S3 header no longer defines the RTC path — "
                         "the S3 RTC leg needs re-reading")
    km = _read(KNOB_MAIN)
    expect(km, r"lv_timer_create\(clock_rtc_interrupt_handler_cb, 1300",
           "the knob's 1300 ms software metronome")
    expect(km, r"RTC_POLL_INTERVAL_MS on the clocks is 1300",
           "the knob's own citation of the clocks' interval")
    knob_token_absent("pcf85063a", "the knob carries no RTC driver")
    rtc_day = DAY * 1000 // rtc_ms

    # ---- the card leg ----------------------------------------------------
    expect(_read(MOON_UI), r"hour \* 3600 \+ 1800",
           "the hour-centre epoch (the card's schedule)")
    tf = _read(TFCACHE)
    expect(tf, r"slot\.host_id = SPI3_HOST", "the S3's SPI3 sdspi host")
    expect(tf, r"slot\.gpio_cs = GPIO_NUM_41", "the card CS pin")
    expect(tf, r"\.mosi_io_num = BSP_SD_CMD", "the SPI-wired socket's CMD line")
    expect(tf, r"#if defined\(CONFIG_BOARD_P4\)\s*\n\s*return "
           r"bsp_sdcard_mount\(\);", "the P4's 4-bit SDMMC leg")
    ui_cmake = _read(UI_CMAKE)
    m = expect(ui_cmake, r'set\(UI_BOARD_SRCS "([^"]+)" "([^"]+)" '
               r'"([^"]+)" "([^"]+)"\)', "UI_BOARD_SRCS (exactly four)")
    if set(m.groups()) != {"alarm_audio.c", "demo.c", "moon_ui.c",
                           "moon_tfcache.c"}:
        raise SystemExit(f"UI_BOARD_SRCS is no longer the four this ladder "
                         f"assumes: {m.groups()}")
    expect(ui_cmake, r'if\(KNOB_BOARD\)\s*\n\s*set\(UI_BOARD_SRCS ""\)',
           "the KNOB_BOARD branch no longer empties the board sources — the "
           "knob's 'no SD socket' zero-leg needs re-reading")
    card_day = 24

    # ---- the radio leg ---------------------------------------------------
    ac = _read(ACTIONS)
    expect(ac, r"current_time\.minute == 0", "the top-of-minute gate")
    expect(ac, r"clock_sntp_needs_sync\(\)", "the hourly NTP gate")
    expect(ac, r"SCREEN_ID_MODEM_SLEEP", "the airplane-screen gate")
    if not re.search(r"#define CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE 1",
                     _read(SDKH["P4"])):
        raise SystemExit("the P4 no longer runs the C6 over SDIO — the "
                         "radio leg's provenance changed")
    radio_day = 24

    # ---- the dial leg ----------------------------------------------------
    enc = _read(KNOB_ENC)
    m = expect(enc, r"#define ENC_POLL_MS (\d+)", "the nominal dial poll")
    enc_nom_ms = int(m.group(1))
    expect(enc, r"vTaskDelay\(poll < 1 \? 1 : poll\)",
           "the tick clamp that makes the poll honest")
    hz = header_int("knob", "CONFIG_FREERTOS_HZ", "the knob's tick rate")
    tick_ms = 1000 // hz
    enc_eff_ms = max(enc_nom_ms, tick_ms)
    enc_day = DAY * 1000 // enc_eff_ms
    enc_nom_day = DAY * 1000 // enc_nom_ms
    if enc_eff_ms == enc_nom_ms:
        raise SystemExit("the encoder's nominal poll equals its effective "
                         "poll — the 3x clamp story is stale")

    # ---- the battery leg -------------------------------------------------
    m = expect(_read(KNOB_BATT_UI),
               r"lv_timer_create\(battery_timer_cb, (\d+)",
               "the battery gauge timer")
    batt_s = int(m.group(1)) // 1000
    batt_day = DAY // batt_s
    expect(_read(KNOB_BATT), r"GPIO 1 \(ADC1_CH0", "the gauge's ADC pin note")

    # ---- the day scan ----------------------------------------------------
    expect(_read(MOON_UI),
           r"xTaskCreatePinnedToCoreWithCaps\(moon_event_task, "
           r"\"moon_event\", \d+, NULL, configMAX_PRIORITIES - 4",
           "the classify worker's spawn (MAX-4, SPIRAM)")
    expect(_read(MOON_UI), r"IDLE1 is not WDT-watched",
           "the worker's own watchdog posture comment")
    knob_token_absent("moon_event_task", "the knob carries no classify "
                      "worker")

    # ======================================================================
    # the model: (leg label, cadence text, colour, {board: (asks/day, note,
    # short-zero-reason) or (0, long-zero-reason, short) or None = absent})
    LEGS = [
        ("touch chip read", f"every {beat} ms beat", BLUE, {
            "S3": (touch_day, "CST9217 (lock-only driver)", None),
            "P4": (touch_day, "GT911 @0x5D/0x14 — INT NC, the beat is all",
                   None),
            "knob": (touch_day, "CST816S @0x15 polled; INT 9 = EXT1 wake",
                     None)}),
        ("EC1 dial GPIO poll", f"effective {enc_eff_ms} ms", AMBER, {
            "S3": None, "P4": None,
            "knob": (enc_day, f"nominal {enc_nom_ms} ms clamped to 1 tick "
                              f"@ {hz} Hz", None)}),
        ("IMU gravity ask", "every 1000 ms", TEAL, {
            "S3": (imu_day, "1 burst read per ask", None),
            "P4": (0, "no QMI8658C mounted — 'moon tilt disabled' (probe "
                      ":254)", "no chip on the board"),
            "knob": (0, "the imu shim answers ready=false", "shim: false")}),
        ("RTC + expander poll", f"every {rtc_ms} ms", SLATE, {
            "S3": (rtc_day, "2 chip reads/poll: TCA9554 P3 + PCF @0x51",
                   None),
            "P4": (0, "compiled out — timer runs the software tick",
                   "compiled out"),
            "knob": (0, "no RTC — the 1300 ms timer is the metronome",
                     "no chip, software tick")}),
        ("battery ADC", f"every {batt_s} s", GRAY, {
            "S3": None, "P4": None,
            "knob": (batt_day, "adc_oneshot GPIO1/ADC1_CH0", None)}),
        ("radio re-sync", "hourly, airplane gate", STEEL, {
            "S3": (radio_day, "own radio", None),
            "P4": (radio_day, "over SDIO (esp_hosted)", None),
            "knob": (0, "the gate fires on the airplane screen only",
                     "airplane-screen gate")}),
        ("microSD hour cache", "hour centre h*3600+1800", VIOLET, {
            "S3": (card_day, "SPI3 sdspi (CMD1/D0 3/CLK 2/CS 41)", None),
            "P4": (card_day, "SDMMC 4-bit, bsp_sdcard_mount verbatim", None),
            "knob": None}),
        ("classify day scan", "1/day + boot", GRAY, {
            "S3": (1, "moon_event MAX-4, 15-20 s scan", None),
            "P4": (1, "moon_event MAX-4, 15-20 s scan", None),
            "knob": (0, "no worker in the knob build", "no worker")}),
    ]

    fig, axes = plt.subplots(1, 2, figsize=(11.6, 5.4),
                             gridspec_kw={"width_ratios": [1.35, 1]})

    # ---- (a) the ask ladder: grouped per leg, one bar per board ----------
    ax = axes[0]
    ys, ylab = [], []
    y = 0
    for label, cadence, color, boards in LEGS:
        ys.append(y + 1)
        ylab.append(f"{label} — {cadence}")
        offsets = [0.85, 1.28, 1.71]
        for i, board in enumerate(("S3", "P4", "knob")):
            v = boards[board]
            if v is None:
                continue
            asks, note, _short = v
            ax.barh(y + offsets[i], max(asks, 0.55), height=0.34,
                    color="white" if asks == 0 else BOARD_C[board],
                    alpha=0.85, edgecolor=BOARD_C[board],
                    linestyle=":" if asks == 0 else "-", linewidth=1.1,
                    zorder=3)
            if asks > 0:
                ax.text(asks * 1.18, y + offsets[i],
                        f"{asks:,}  {note}", va="center", fontsize=5.3,
                        color=MUTED, zorder=4)
            else:
                ax.text(1.25, y + offsets[i], "ZERO — " + note,
                        va="center", fontsize=5.3, color=MUTED, style="italic",
                        zorder=4)
        y += 2.6
    # the two hatched ghosts ride BELOW their group's three bars
    ax.barh(ys[2] + 1.21, imu_chip_day, height=0.28, color="white",
            edgecolor=TEAL, linestyle=":", hatch="///", linewidth=1.0,
            zorder=2)
    ax.text(imu_chip_day * 1.18, ys[2] + 1.21, f"{imu_chip_day:,} — the chip "
            "STREAMS (1000 Hz ODR): the firmware asks 1 in 1000",
            va="center", fontsize=5.3, color=TEAL, zorder=4)
    ax.barh(ys[1] + 1.21, enc_nom_day, height=0.28, color="white",
            edgecolor=AMBER, linestyle=":", hatch="///", linewidth=1.0,
            zorder=2)
    ax.text(enc_nom_day * 1.18, ys[1] + 1.21, f"{enc_nom_day:,} — nominal "
            f"{enc_nom_ms} ms un-clamped: the 100 Hz tick clamp pays 3x",
            va="center", fontsize=5.3, color=AMBER, zorder=4)
    ax.set_yticks(ys)
    ax.set_yticklabels(ylab, fontsize=6.4, color=INK)
    ax.set_xscale("log")
    ax.set_xlim(0.45, 6e8)
    ax.set_ylim(y - 0.7, -1.6)
    ax.set_xlabel("interrogations per day (log) — while the hand is on; "
                  "the sleep ladder owns the dark hours", fontsize=6.6,
                  color=MUTED)
    ax.tick_params(axis="x", labelsize=6, colors=MUTED)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.grid(axis="x", color=HAIR, linewidth=0.5, zorder=0)
    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=BOARD_C[b], alpha=0.85,
                             edgecolor=BOARD_C[b]) for b in ("S3", "P4",
                             "knob")]
    ax.legend(handles, ("S3", "P4", "knob"), loc="lower right", fontsize=6,
              frameon=False)
    ax.text(0.6, -1.5,
            "open dotted bar = the leg is ZERO there (reason in italics); "
            "no bar at all = the leg does not exist for that board; "
            "hatched = chip-side or nominal-only, never billed",
            fontsize=5.6, color=MUTED, va="top")
    ax.set_title("how often each hand interrogates its silicon — parsed "
                 "call sites, one log count axis\n(the 1000x IMU gap and "
                 "the 3x dial clamp are the ladder's two comparisons)")

    # ---- (b) the presence matrix ----------------------------------------
    ax = axes[1]
    boards = ("S3", "P4", "knob")
    for r, (label, cadence, color, board_vals) in enumerate(LEGS):
        yy = r * 1.0
        for c, board in enumerate(boards):
            v = board_vals[board]
            if v is None:
                ax.scatter([c], [yy], marker="x", s=34, color=HAIR, zorder=3)
                continue
            asks, _note, short = v
            if asks > 0:
                ax.scatter([c], [yy], marker="o", s=64, color=color,
                           edgecolor=INK, linewidth=0.6, zorder=3)
                ax.text(c, yy - 0.33, f"{asks:,}/day", ha="center",
                        va="top", fontsize=5.4, color=INK)
            else:
                # open circle only — the parsed reason for the zero lives
                # in italics on the LEFT panel's bar row (the cell text
                # here collided with the neighbouring cells' counts)
                ax.scatter([c], [yy], marker="o", s=64, facecolor="white",
                           edgecolor=color, linewidth=1.4, zorder=3)
    ax.set_yticks([r for r in range(len(LEGS))])
    ax.set_yticklabels([f"{l} — {c}" for l, c, _, _ in LEGS], fontsize=6.0,
                       color=INK)
    ax.set_xticks(range(len(boards)))
    ax.set_xticklabels(boards, fontsize=7.5, color=INK)
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.set_ylim(len(LEGS) - 0.35, -0.75)
    ax.set_xlim(-0.55, 2.85)
    ax.set_title("the presence matrix — filled = asked at the printed rate;\n"
                 "open = zero with the parsed reason; x = the leg itself "
                 "is absent", fontsize=8)

    fig.tight_layout()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fig.savefig(OUT, dpi=200, bbox_inches="tight")
    print(f"wrote {os.path.relpath(OUT, ROOT)}  (beat {beat} ms -> "
          f"{touch_day:,}/day; imu {imu_day:,} vs chip {imu_chip_day:,}; "
          f"rtc {rtc_day:,}; enc {enc_eff_ms} ms eff -> {enc_day:,}; "
          f"batt {batt_day:,})")


if __name__ == "__main__":
    main()
