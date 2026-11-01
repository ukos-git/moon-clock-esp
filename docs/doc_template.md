# ESP32 Moon Phase Clock — Project Documentation

A physically correct moon-phase clock. A disk of the Moon — 400 px on the S3's
466 px panel, 650 px on the P4's 720 px panel (85.8 % and 90.3 % fill — the P4
default leaves a gap of ~half the seconds-arc thickness to the ring) — the
current phase, from the last new moon to now — is rendered in real time on the
panel from LROC Wide-Angle Camera albedo and LOLA relief, lit with
displaced-normal Lambert shading at the exact Sun/Moon/Earth geometry of the
cheap ephemeris (JPL Moon98 + ERFA EPV). The disk is baked on-chip once a day;
the free spin is a fixed-point in-plane re-gather (~8 ms/frame) written
straight to the panel; sleep is signalled by a pole-axis spin that carries the
terminator across the disk to the fully unilluminated side. Time comes from NTP
(brief on-demand WiFi activation, then back to airplane mode); the PCF85063A
RTC holds the clock between syncs.

_State of this document: 2026-11-01, after the **dual-board build — P4
plumbing, Wi-Fi, moon and UI scale-up** (the repo now also builds for the
Waveshare **ESP32-P4-WIFI6-Touch-LCD-XC "4C"**, 720×720 round IPS — §2.3, §3.2;
the S3 build is unchanged; the P4's remote-C6 Wi-Fi, the 650 px moon disk
(2026-11-01: grown 618 → 650 px so the gap to the seconds arc is ~half the
arc's thickness, §3.2) and the 720 px UI upscale — panel-scaled layout +
regenerated fonts, §6 — are done on-device; the P4 spin's CCW flicker is fixed
at the source — one framebuffer, no scanout flip, §4.3; the P4 clock
degradation (no RTC IC: crystal RTC timer + SNTP + software alarms) is the
follow-on milestone), the **computed (backlit) night side** — the unilluminated
side is now the real lunar surface lit faintly from the anti-solar direction
instead of the flat `MOON_NIGHT_FLOOR` overlay (§5.2 item 4; the `dk_sup` rim
kill is baked into the shade map and `moon_reproject` is a pure gather — §5.2
item 5), and the burn-in spin's **direct panel write** (the render task writes
the MOON_PX² moon area straight to the panel at 8 fps instead of routing
through LVGL's ~175 ms re-render; the settled rate is a constant slow spin, so
the moon no longer freezes before light sleep), the **fade-to-black screen
switch** (replacing the concentric reveal that showed white strip bars; now a
hardware panel-brightness ramp — the CO5300's own dimming command — so the
transition writes no pixels at all), the **alarm weekday prefix** ("Mo 07:00"
on the home screen when the next fire is >24 h away), the **fa-circle-xmark
exit glyph** (U+F057 — the glyph is the button), and the **7:00 Mon–Fri default
alarm** (the `*/5` debug rule burned the alarm screen in), and the **P4
software clock** (M5.1 — no RTC IC: `CLOCK_USE_PCF85063A=n`, the crystal-backed
internal RTC timer + SNTP as the time base with the minute tick / countdown /
alarm arming in software, §2.3). See the standing rule in
[CLAUDE.md](../CLAUDE.md) — this document is regenerated with
`python3 docs/gen_doc.py` and must stay in sync with the code._

---

## Table of contents

1. [This page](#1-this-page)
2. [Quick start — detailed ESP setup](#2-quick-start--detailed-esp-setup)
3. [Hardware](#3-hardware)
4. [Architecture](#4-architecture)
5. [The moon calculation](#5-the-moon-calculation)
6. [Design-driven language](#6-design-driven-language)
7. [Screens — every live screen with its settings](#7-screens--every-live-screen-with-its-settings)
8. [Burn-in](#8-burn-in)
9. [Animations & films](#9-animations--films)
10. [Implementation notes](#10-implementation-notes)
11. [Measured timings](#11-measured-timings)
12. [Test system (with the NASA reference files)](#12-test-system-with-the-nasa-reference-files)
13. [ESP32-S3: capabilities & limits](#13-esp32-s3-capabilities--limits)
14. [Configuration reference](#14-configuration-reference)
15. [Project structure](#15-project-structure)
16. [Files of docs/ and CLAUDE.md](#16-files-of-docs-and-claudemd)
17. [Complete source code](#17-complete-source-code)

---

## 1. This page

This is the single hosted document for the project. It is GitHub-compatible
Markdown and renders the project's own media:

- **17 screenshots of every live screen** on the S3's 466x466 panel (the
  circular wheel keyboard, §7.13, ships its at-rest, touched-down, 123, ABC and
  symbol modes), rendered by the UI simulator (`host_sim/uisim`) from the
  _real_ device `screens.c` with the vendored LVGL 9.6 — the exact panel
  pixels, 466x466 RGB565: `docs/img/screens/*.png`. The **same 17 shots at the
  P4's 720x720 panel** (the 650 px moon disk, the 720 px UI — panel-scaled
  layout + the P4's 148/74/50 px fonts): `docs/img/screens/p4/*.png`
  (`build.sh run --panel 720` builds into its own `build-720/` dir).
- **4 films** of the live animations — the first three with frames rendered by
  the device's own moon core (`host_sim/build/moon_sim`, the exact device
  translation units):
  - `docs/img/boot_sweep.mp4` — the boot synodic sweep (336 frames @240 px, new
    moon → now)
  - `docs/img/free_spin.mp4` — the settled free spin (36 frames @466 px, 10°)
  - `docs/img/dusk.mp4` — the pole-axis sleep spin (0° → the fully-dark
    endpoint, 1°/frame)
  - `docs/img/fade.mp4` — the fade-to-black screen switch (airplane → settings
    hub; uisim frames blended through the linear brightness ramp — the panel's
    hardware dimming)
  - plus the committed montages `docs/img/boot_sweep.png`,
    `docs/img/free_spin.png`, `docs/img/dusk.png` (the README demo).
- **5 sample frames** of the NASA SVS 5415 reference set the image gate runs
  against: `docs/img/nasa/nasa_*.png` (the full set of 8,760 frames lives at
  `assets/images/moon/final_400x400/` in the working tree — gitignored, see
  §12).
- **The complete source code** of the project, verbatim from the tracked tree,
  embedded in [§17](#17-complete-source-code) (generated, so it cannot drift
  from the code it documents).

Regeneration: `python3 docs/gen_media.py` (films, needs `host_sim/.venv` with
PIL + ffmpeg), `host_sim/uisim/build.sh run` then copy
`host_sim/uisim/out/*.png` → `docs/img/screens/`, `python3 docs/gen_doc.py`
(this document).

---

## 2. Quick start — detailed ESP setup

### 2.1 Toolchain

- **ESP-IDF 5.5.1** (the tree was built and validated with it):
  ```bash
  git clone -b v5.5.1 --depth 1 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
  cd ~/esp/esp-idf && ./install.sh esp32s3
  source ~/esp/esp-idf/export.sh      # in every new shell
  ```
- **Python venv for the test/asset pipeline** (host gate, asset generation):
  ```bash
  cd assets/images/moon && uv venv .venv && uv pip install -e .
  cd host_sim && uv venv .venv && uv pip install astropy scikit-image imagecodecs
  ```
  (`imagecodecs` is required for tifffile's LZW TIFF decode of the LROC albedo;
  the system python may lack astropy/skimage.)
- **ffmpeg** (films for this document) and **cmake/ninja/gcc** (host sims).

### 2.2 Board wiring

No wiring needed — both boards are finished. Connect USB for flash + serial (@
115200 on Linux). The confirmed port map: **`/dev/ttyACM0` = the P4 board**,
**`/dev/ttyACM1` = the S3 board** — re-verified 2026-11-01 by a flash + sweep
boot of the P4 on `/dev/ttyACM0` (the S3 had been re-plugged onto a different
USB path; the map flipped from the 2026-10-31 probe — re-probe with esptool
after any re-plug).

### 2.3 First build

```bash
cd <this repo>
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3          # once
idf.py build
```

`idf.py set-target` fetches the managed components (LVGL 9.6.0,
`esp_lcd_co5300`, `esp_lvgl_adapter`, `esp_lcd_touch_cst9217`, …) into
`managed_components/` (gitignored; pinned by `dependencies.lock`).

**Before the first flash — WiFi credentials** (two lines in
`spiffs/wifi_credentials.txt`, SSID then password):

```
<your SSID>
<your password>
```

(the template `spiffs/wifi_credentials.example.txt` shows the format; the real
file is gitignored). The SPIFFS partition is re-baked from `spiffs/` on every
build, so this file must exist in the tree before `idf.py build`. **Warning:**
a full `idf.py flash` re-bakes `storage.bin` and clobbers runtime-written
`/spiffs/alarm.txt` and `/spiffs/settings.txt` — the device re-derives them
(the `spiffs/alarm.txt` default — 7:00 Mon–Fri — is restored from the tree;
user alarm/settings edits are lost on a full flash).

**Timezone** (optional): `idf.py menuconfig` → _Component config → Clock
Component Configuration → Default timezone_ (POSIX TZ; default
`CET-1CEST,M3.5.0/2,M10.5.0/3`).

### 2.3b The P4 (4C) build

The same repo builds for the Waveshare **ESP32-P4-WIFI6-Touch-LCD-XC "4C"**
(720×720 round IPS; §3.2) without call-site changes — the board choice is the
`BOARD` Kconfig (`CONFIG_BOARD_S3` default / `CONFIG_BOARD_P4`, in
`components/board/Kconfig.projbuild` — a root `Kconfig.projbuild` is not
processed by ESP-IDF 5.5), and the per-target defaults are picked up
automatically: alongside `sdkconfig.defaults` the build loads
`sdkconfig.defaults.esp32s3` / `sdkconfig.defaults.esp32p4` (present =
appended; later file wins). The two targets resolve different managed-component
sets (different BSP + display/touch/Wi-Fi stacks), so the P4 build runs with
its own lock (`dependencies.lock.p4`, gitignored) and never touches the tracked
S3 `dependencies.lock`; the vendored S3 BSP is excluded from the P4 build
(`EXCLUDE_COMPONENTS`), and the root `CMakeLists.txt` `FATAL_ERROR`s on a
target/board mismatch. The partition table is **not** a CMake choice: the
`partition_table` component re-derives `PARTITION_CSV_PATH` from Kconfig at
configure time, so a plain `set(PARTITION_CSV_PATH …)` before `project()` is
clobbered. The S3 keeps its default `partitions.csv`; the P4 picks
`partitions_p4.csv` (32 MB chip, §3.2) through the Kconfig value
`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"` in
`sdkconfig.defaults.esp32p4` (the shared file sets
`CONFIG_PARTITION_TABLE_CUSTOM=y`).

```bash
idf.py -D IDF_TARGET=esp32p4 -B build/p4 build
idf.py -D IDF_TARGET=esp32p4 -B build/p4 flash -p /dev/ttyACM1
```

**State (2026-10-31):** the P4 build is green and flashed — the app boots clean
on the board (rev v1.3 profile; XC BSP 3.0.1 + JD9365 + GT911 + LVGL-adapter,
32 MB PSRAM @ 200 MHz; 0 WDT / 0 panic / 0 assert), associates over the
**remote C6** (the `esp_hosted` 1.4.7 / `esp_wifi_remote` 0.14.5 pair over
SDIO, ~7.5 s to IP) and syncs the clock by SNTP (the first clock smoke; the
clock label then shows real time). The P4's remaining product path is the clock
degradation (M5 — §10.6's P4 half); the software-clock half (M5.1) landed
2026-11-01 behind the new `CLOCK_USE_PCF85063A` Kconfig option. Three
P4-specific code facts:

- **The remote radio is initialized exactly once.** The host-side LWIP netif
  attach of the `esp_hosted` radio is not re-runnable: a second
  `esp_wifi_init()` re-adds the persistent netif and asserts
  `netif_add (netif already added)`. So on the P4 the manager inits the radio
  once (`s_wifi_inited`) and every teardown (airplane screen, scan) is
  `esp_wifi_stop()` only — no `esp_wifi_deinit()`. This is what the first P4
  boot crash-looped on (the airplane→sync cycle was deinit+init).
- **The time base is the system clock.** The P4 has no PCF85063A / no I2C bus /
  no IO expander, so the whole PCF block in `clock_init` is S3-only;
  `clock_get_time`/`clock_set_time` read/write the SNTP-synced system clock
  (`time()`/`localtime_r`, `settimeofday`), `clock_is_interrupt_pending` is a
  P4 no-op (no PCF CTRL2 to poll), and the SNTP path fills the time it writes
  from `clock_sntp_get_system_time` (the earlier "Invalid time values" log was
  an unfilled stack var on the NTP-success path). The software clock path lands
  in M5.1 (2026-11-01), behind the new `CLOCK_USE_PCF85063A` Kconfig option
  (default `y`; `n` in `sdkconfig.defaults.esp32p4` — the S3 build is
  unchanged, the code gates on `#if CONFIG_CLOCK_USE_PCF85063A`): the time base
  is the system clock over the **external 32 kHz crystal**
  (`CONFIG_RTC_CLK_SRC_EXT_CRYS=y`; the RC oscillator is only a few % accurate
  and unusable as a time base); the **minute tick** is a wall-clock minute
  rollover detected in the 1.3 s poll (the poll interval is far shorter than a
  minute, so at most one rollover can occur between polls — never missed, never
  doubled; an SNTP/manual time step re-bases without firing, matching the S3's
  TF semantics for a written time); the **deep-sleep countdown** is an
  `esp_timer` one-shot whose callback only sets a flag — the poll (LVGL task)
  consumes it and runs the deep-sleep transition, the same context split as the
  S3's TF poll; the **alarm arming** is a second `esp_timer` one-shot for the
  scheduled epoch (the PCF ALM+AIE analog — re-armed on every SNTP/manual time
  SET via `clock_alarm_scheduler_reload`, the poll's wall-clock backup check is
  the backstop), gated on `clock_sntp_time_trusted()` so the default/unsynced
  clock can never ring a phantom alarm; the 0-rule disarm +
  stale-`s_next_alarm`-cache drop is the software half of the S3's
  persistent-ALM guard. `clock_set_alarm`/`clock_enable_alarm`/
  `clock_clear_alarm` are no-ops on such builds (the scheduler's `s_next_alarm`
  cache is the source of truth); `clock_get_alarm` returns
  `ESP_ERR_NOT_SUPPORTED`. The **NVS epoch backup** (M5.2, also 2026-11-01): a
  full power loss kills the crystal domain (VBAT coverage unverified), so the
  system-clock epoch is snapshotted to NVS (`clock`, `epoch`) on every SNTP
  success and every 15 min from the software minute tick
  (`clock_nvs_maybe_save_epoch`); the SNTP init restores it (pre-2021 snapshots
  are rejected as junk) in place of the 2026-01-24 default — but the restored
  time is still NOT trusted: the flag stays down until the next SNTP sync or a
  manual entry. Two P4 UI/flow halves ride on the flag: the clock label **dims
  to 50 % opacity while unsynced** (the per-widget tick converges on its next
  update after the flag flips), and the **deep-sleep transition is skipped
  while an alarm is pending** (the P4's "deep sleep" is app-level — no
  `esp_deep_sleep`, so the crystal domain and the software one-shot stay alive;
  with no RTC-alarm wake the device holds the dim light-sleep screen instead of
  going to the backlight-off state). Only the P4 manual time-entry screen
  (calls `clock_sntp_set_time_trusted`) remains.
- **The DSI panel's strip writes are async.** The P4 DPI panel copies each
  strip via DMA2D and rejects a new draw while the previous copy is in flight
  (`dpi_panel_draw_bitmap: previous draw operation is not finished`); the S3
  QSPI panel writes synchronously, so the 16 back-to-back raw panel writes of
  the burn-in spin were rejected on the P4. Both panel-write sites (LVGL flush
  wrapper + spin) now retry through `moon_panel_draw_wait` (~100 ms window, 1
  ms apart) — the retries make the rejection a logged, re-issued strip instead
  of a dropped one.
- **The AP scan owns the radio quiescently.** Because the manager's SSID config
  persists across the P4's stop-only deinit (the remote radio is never
  `esp_wifi_deinit`'ed — see above), the manager's still-registered event
  handler used to auto-connect on `STA_START` whenever the scan's own bring-up
  ran (`s_wifi_event_group == NULL`): the radio started associating and the
  blocking `esp_wifi_scan_start` failed with `ESP_ERR_WIFI_STATE`. The
  auto-connect and the disconnect-retry are now gated on the manager owning the
  radio (`s_wifi_event_group != NULL`), which also kills the zombie "Retry
  connecting / connection failed" pair the airplane/light-sleep deinit left
  behind.
- **The sync-complete idle re-arm is sweep-aware.** The `ui_tick` pickup that
  drops the transient WiFi screen to airplane re-armed the idle timer
  unconditionally — but the boot sweep owns the idle lifecycle (it disables the
  monitoring at start and re-arms it at the end). A background sync that
  completes while the sweep plays would let the 60 s idle timeout fire
  mid-sweep and cut the boot animation with the light-sleep transition. On the
  P4 the on-demand sync runs ~30 s through the remote C6 and regularly
  overlapped the sweep (the sweep was interrupted at frame 285/336); the re-arm
  now skips `clock_idle_set_enabled(true)` while `moon_ui_sweeping()`.

**State (2026-11-01):** the spin's CCW flicker is fixed at its source — the
panel now runs **one framebuffer, no flip** (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1`

- the adapter's NONE tear-avoid mode, §4.3): with the BSP default of three
  panel framebuffers, every LVGL refresh flipped the continuous MIPI-DPI
  scanout to an FB whose moon area still held the spin pose from its last visit
  — an older θ (the spin's θ only advances CW, so older reads CCW) — until the
  next spin frame corrected it: the "sporadic counter-clockwise images that
  jump back to the current frame."

**State (M4, 2026-10-31):** the P4's UI is now fully upscaled — every screen
root is panel-sized (`UI_PANEL_PX`, `components/ui/ui_px.h`:
`UI_PX(x) = x·BSP_LCD_H_RES/466`, exact at 466 so the S3 layout is bit-for-bit
unchanged), the seconds arcs ride the panel edge and the type is the P4's
regenerated font set (`components/ui/fonts_p4/`: the same six `lv_font_conv`
subsets at 148/74/50 px instead of 96/48/32 — identical `lv_font_t` symbols, so
`fonts.h` and every consumer are unchanged; `components/ui/CMakeLists.txt`
swaps the font dir on `CONFIG_BOARD_P4`). Verified: uisim 720 shots of all 17
screens (`docs/img/screens/p4/`, the scripted `--wheel` flow panel-scaled), the
S3 binary behaviourally unchanged (the uisim 466 pass renders byte-identical
screens), the spin's keep-mask band 326-357 lands on the upscaled home seconds
arc (ring r 329-344, knob to r ~350; RMIN sits just outside the 650 px moon
edge at r 325, so the band never overlaps the disk). The rotated orientation
layer grows with the root (720² RGB565 ≈ 1.0 MB, clib → PSRAM). P4 binary
1,468,784 B (9,437,184 B factory); 2026-11-01 after M5.1: P4 **1,477,472 B**
(`0x168b60`), S3 **1,450,352 B** (`0x162170`, +176 B over the 1,450,176
baseline — the shared clock headers + the trusted-time flag).

Two P4-only build facts: LVGL's fast-math functions go to PSRAM XIP rather than
internal IRAM (`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n` — on RISC-V
`IRAM_ATTR` would land ~113 KB of LVGL in the scarce `sram_low` and overflow
the link), and the CPU1 idle-task WDT check is disabled
(`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`) because the moon sweep/spin owns
a core for seconds (same as the S3). The P4 software clock (M5.1 — no RTC IC:
crystal-backed internal RTC timer + SNTP + `esp_timer` alarm/countdown
one-shots, the P4 half of §10.6) is code-complete 2026-11-01 (build-verified
both targets; on-device verification pending); the NVS epoch backup (M5.2's
power-loss half) landed the same day, leaving only the P4 manual time-entry
screen. The S3 build flow is unchanged; the shim + call-site migration add ~15
KB to the S3 binary (§3.1).

### 2.4 Flash + serial

```bash
idf.py flash -p /dev/ttyACM0        # ~40 s at 460800 baud
python3 -m serial.tools.miniterm /dev/ttyACM0 115200   # or pyserial capture
```

**First boot** (≈4 min): the clock is acquired by the _blocking_ boot sync
(WiFi + NTP before the display starts), the UI is born on the **airplane** home
screen, and the **synodic boot sweep** plays: 336 frames @240 px from the last
new moon to now, ending on the current phase; the free spin then re-anchors
from the sweep's last frame. The sweep is display-paced and the per-frame bake
cost is now _flat across the synodic cycle_: with the backlit night side (§5.2
item 4) every in-disk pixel loads the albedo + relief gradient, so a dark frame
costs about what a bright one does — measured 2026-11-01 on the P4 at a 22 d
moon age, 180–183 ms @240 px (previously 64–138 ms on the S3's flat-floor
model, which skipped the albedo on the dark side) and the final frame bakes at
full 650 px in **959 ms** (was ~595 ms — the dark pixels now load albedo too) —
**~125 s total** (336 × ~370 ms display-paced). The S3's older figures (79–541
ms @240 px median 306, 231 s total, 2026-10-31) predate the backlit change and
will shift the same way; re-measure on the S3 device. Serial markers to watch:
`WiFi connected`, `SNTP sync`, `synodic boot sweep started: 336 frames`,
`sweep N/336 baked in ~180ms @240px`, `spin started`. A clean boot shows **0
WDT / 0 panic / 0 NO_MEM**.

### 2.5 Regenerating the moon assets (optional)

The `moon_assets` partition holds the 864x432 equirect albedo + 2048x1024 int8
relief DEM (2,470,400 B < 2,621,440 B), pre-generated and baked from
`assets/images/moon/moon_assets.bin` into `build/storage.bin`… (no — into
`build/` and flashed at `0x17c000`). Regenerate from LROC + LOLA sources:
`assets/images/moon/.venv/bin/python -m moon.assets`. The albedo **must be
loaded 16-bit** (tifffile), and the texture carries an 180° longitude offset
(`MOON_TEX_LON_OFFSET`).

---

## 3. Hardware

Waveshare **ESP32-S3 Touch AMOLED 1.75**, 16 MB flash variant:

| Block   | Part                   | Notes                                                                                                                                                                                               |
| ------- | ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MCU     | ESP32-S3               | dual-core Xtensa @ 240 MHz, 8 MB octal PSRAM @ 80 MHz, 16 MB flash                                                                                                                                  |
| Display | 466x466 AMOLED, RGB565 | CO5300 driver, QSPI interface (GPIO4-7, 38), CS GPIO12, TE GPIO13, RESET GPIO39; panel x_gap 0x06                                                                                                   |
| Touch   | CST9217                | I2C (GPIO14/15), INT GPIO11, RESET GPIO40                                                                                                                                                           |
| IMU     | QMI8658C               | I2C (shares the RTC bus), INT on EXIO6/QMI INT2                                                                                                                                                     |
| RTC     | PCF85063A              | I2C behind the **TCA9554** IO expander (GPIO14/15, EXIO pins); **RTC_INT is NOT wired to any ESP32 GPIO** (AXP EXIO3 only) — the clock is time-source + 1.3 s software poll, never interrupt-driven |
| Audio   | ES8311                 | I2S, unused by this product                                                                                                                                                                         |
| USB     | USB-C                  | GPIO19/20, serial + flash                                                                                                                                                                           |

Key S3 board quirk (full GPIO map in the README): the external RTC's interrupt
line does not reach the MCU, so all minute updates, the deep-sleep countdown,
and the alarm firing are driven by the ESP32's own `esp_timer`-based polling +
the PCF85063A as the time-of-truth source.

### 3.1 Flash layout (`partitions.csv`)

| Name           | Type   | Size                     | Holds                                                                  |
| -------------- | ------ | ------------------------ | ---------------------------------------------------------------------- |
| nvs / phy_init | data   | 24 K + 4 K               | IDF                                                                    |
| `factory`      | app    | **2048 K** (2,097,152 B) | the firmware — current binary `0x162170` (1,450,352 B), 646,800 B free |
| `storage`      | spiffs | 64 K                     | `wifi_credentials.txt`, `alarm.txt`, `settings.txt` (~1 KB)            |
| `moon_assets`  | data   | 2560 K                   | 864x432 albedo + 2048x1024 int8 relief DEM                             |

The 2 MB layout in early git history was a config choice, not a hardware limit;
the 2048x1024 relief DEM (2,097,152 B) forced the 16 MB variant. The factory
partition was widened 1392 K → 2048 K when the app outgrew the old ceiling —
the circular wheel keyboard (the widened full-ASCII SSID font, the 13th screen,
and the save→connect flow, §7.13) pushed the binary past 1,425,408 B. Consumers
find every partition by label, not offset, so the widening is layout-invisible;
646,800 B of headroom remain for the future text-input family.

### 3.2 The P4 board (Waveshare ESP32-P4-WIFI6-Touch-LCD-XC "4C")

| Block     | Part                | Notes                                                                                                                                                                                                                               |
| --------- | ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MCU       | ESP32-P4 (rev v1.3) | RISC-V single-core @ 400 MHz, 32 MB in-package PSRAM @ 200 MHz (on IDF 5.5.1 the 200 MHz speed is `CONFIG_IDF_EXPERIMENTAL_FEATURES`-gated), 32 MB flash                                                                            |
| Display   | 720×720 round IPS   | JD9365, MIPI-DSI 2-lane @ 1500 MBPS; reset GPIO27, backlight GPIO26 (PWM, output inverted); **one framebuffer, no flip** (the spin's CCW flicker, §4.3)                                                                             |
| Touch     | GT911-class         | I2C @ 0x5D; the indev registers during BSP display start (no app change)                                                                                                                                                            |
| Wi-Fi     | ESP32-C6 (remote)   | over SDIO via `esp_hosted` 1.4.7 / `esp_wifi_remote` 0.14.5: CLK 18 / CMD 19 / D0–D3 14–17, 4-bit @ 40 MHz, slave reset GPIO54; 5 GHz-capable                                                                                       |
| Time base | none (no RTC IC)    | the internal RTC timer on the **external 32 kHz crystal** (`CONFIG_RTC_CLK_SRC_EXT_CRYS`, M5.1 — the RC oscillator is a few % inaccurate, unusable as a time base) + SNTP; VBAT battery header (power-loss survival to be verified) |
| IMU       | none                | the tilt path soft-fails: no tilt→spin / tilt→roll, orientation fixed at the base +90° step                                                                                                                                         |
| Audio     | ES8311 codec        | the planned alarm ring (volume ramp + touch snooze)                                                                                                                                                                                 |

Unlike the S3, the P4 PSRAM **is** DMA-capable — the S3's 32 KiB SPI-DMA strip
staging (buffer_height 25, flush queue depth 1) is a QSPI-S3 artifact and does
not apply (the P4 flush geometry is set by its BSP). The `components/board/`
shim normalizes the two BSPs' small API differences (`board_panel()` — S3
`bsp_display_get_panel()` / P4 `bsp_display_get_panel_handle()`;
`board_io_expander_init()` — real TCA9554 on the S3, NULL on the P4);
everything else is identical-named BSP API.

**Flash layout (`partitions_p4.csv`, 32 MB chip — the binary header stays 16
MB, so esptool's size-mismatch warning is expected):**

| Name           | Type   | Size       | Holds                                                                                                                                                   |
| -------------- | ------ | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| nvs / phy_init | data   | 24 K + 4 K | IDF (the PHY runs on the C6 slave; `phy_init` is kept anyway — the xiaozhi v2.5.2 4C merged binary and the esp_hosted host-side examples both carry it) |
| `factory`      | app    | **9216 K** | the firmware — current binary `0x168b60` (1,477,472 B), ~7.6 MB free — huge headroom vs the S3's 2048 K                                                 |
| `storage`      | spiffs | 64 K       | `wifi_credentials.txt`, `alarm.txt` (same SPIFFS re-bake warning as the S3, §2.3)                                                                       |
| `moon_assets`  | data   | 2560 K     | the same size-independent albedo + DEM blob as the S3                                                                                                   |

---

## 4. Architecture

### 4.1 Component tree

```
main/                  app_main: boot sync (WiFi+NTP) -> display -> UI; test hooks
components/
  moon/                the portable core (pure C, no ESP/LVGL deps):
    core/moon_astro.c      ephemeris (vendored ERFA eraEpv00 + eraMoon98) -> body-frame geometry
    core/moon_bake.c       displaced-normal Lambert bake + shade map + constant-projection reproject
    core/moon_fixp.c       fixed-point sin/cos tables
    core/moon_render.c     fixed-point in-plane gather (the free spin)
    core/erfa/             vendored public-domain ERFA subset
    include/               moon.h, moon_consts.h (generated), moon_assets.h
    host/ esp/             host-asset + device asset glue
  clock/               PCF85063A RTC (or the P4 software clock, M5.1), minute/
                           interrupt logic, idle detector; clock_sntp.c (NTP sync +
                           the time-trusted flag); clock_alarm_scheduler.c (cron-like
                           alarms, write/reload, */N>59); settings_store.c (store)
  ui/                  LVGL 9.6 UI: screens.c (13 screens), moon_ui.c (the moon
    actions.c                pipeline: sweep/spin/dusk/bake tasks), styles.c,
    images.c                 fonts/ + fonts_p4/ (per-panel lv_font_conv subsets)
  imu/                 QMI8658C driver (I2C), tilt/activity + wake model
  wifi/                ESP WiFi scan (settings screen)
  board/               the board shim: board_display.{c,h} — the app-side
                       surface over both BSPs (panel handle, the S3-only IO
                       expander) + Kconfig.projbuild (the BOARD choice)
  esp32_s3_touch_amoled_1_75/   the S3 BSP: CO5300 QSPI panel + CST9217 + expander
                                + brightness (P4: the registry
                                waveshare/esp32_p4_wifi6_touch_lcd_xc 3.0.1,
                                JD9365 MIPI-DSI + GT911 — resolved, not vendored)
host_sim/              the host test/rendering layer (same core, host-built)
  moon_sim (main.c)    exact device core -> PNGs at panel size
  check_gate.py        the image gate vs NASA + Python model
  pole_test.c          dusk geometry proof (ad-hoc build)
  uisim/               real screens.c on host LVGL -> per-screen PNGs
  render_demo.py       README montages
assets/                LROC/LOLA sources + the Python prototype + 8,760 NASA SVS 5415 frames
spiffs/                baked filesystem (gitignored, except the example)
docs/                  this document + media
```

### 4.2 Task & core model

| Task           | Core | Job                                                                                                                                                                                                                                                                   |
| -------------- | ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| LVGL (adapter) | 0    | UI, input, the 10 ms tick (airplane/WiFi), screen switches, the fade-to-black switch legs, atomic whole-panel flushes; 8 KB stack                                                                                                                                     |
| spin render    | 1    | the free spin: one nearest-neighbor gather of the baked MOON_PX² frame (~8 ms on the S3) + a direct panel write of the moon area (8 fps default), rate = active period while in use, slow settled period (default 600 s/turn) at rest                                 |
| moon bakes     | 1    | boot sweep (336 frames), daily re-bake, dusk shade map + final bake, row-by-row so an interrupt is honored                                                                                                                                                            |
| wifi sync      | 1    | background `wifi_manager_init` + NTP (never on the LVGL task — the wake-freeze bug's fix)                                                                                                                                                                             |
| panel watchdog | 0    | sleeps 3 s at a time, then probes `s_panel_mtx` with a 3 s bound: if the panel mutex is still held, a QSPI bus lock was lost (a strip DMA's completion never landed) and the log says `PANEL IO STUCK` → `esp_restart()` — the settings-freeze bug's backstop (§10.4) |
| main           | 0    | boot, RTC polling (1.3 s), test hooks                                                                                                                                                                                                                                 |

All moon-pipeline frames (bake, sweep, dusk, wake) land on the panel as **one
atomic whole-panel `lv_refr_now` under `s_rgb_mtx`** — the display invariant.
Two sanctioned exceptions, both serialized on the panel's SPI bus lock: the
burn-in spin writes its MOON_PX² moon area (400×400 S3, 650×650 P4) **straight
to the panel** from the render task (§8.1), and the screen-switch fade is a
**hardware panel-brightness ramp** (the CO5300 0x51 command) that writes no
pixels at all — its one atomic whole-panel flush of the new screen runs while
the panel is still black, and the ramp's 0x51 steps plus the spin's panel
writes are serialized on `s_panel_mtx` (the spin holds its writes while the
fade runs, §10.4). No tearing either way.

### 4.3 The display pipeline

```
moon_bake (core 1, PSRAM 8-bit gray) -> RGB565 convert
   -> s_frame (PSRAM, MOON_PX^2: 400^2 on the S3, 650^2 on the P4)
   -> LVGL task: lv_refr_now(NULL) under s_rgb_mtx
   -> adapter bridge: 25-row strips (S3), 50-row strips (P4)
      S3: PSRAM strip -> internal-RAM DMA staging (PSRAM is not SPI-DMA-capable;
      32 KiB SPI DMA cap) -> CO5300 windowed QSPI write (CASET/RASET/RAMWR, x_gap 0x06)
      P4: DMA2D copy to the MIPI-DSI DPI controller (PSRAM IS DMA-capable)
```

On the S3, `buffer_height = 25` (23.3 KiB strip) and flush queue depth 1 are
hard limits (the 50-row upstream default exhaustes the DMA-capable heap); the
P4's BSP uses 50-row buffers freely. The burn-in spin bypasses this strip path
entirely: a wrapped `draw_bitmap` writes the MOON_PX² moon area straight to the
panel — **one continuous 400×400 QSPI window write on the S3** (a single
CASET/RASET, then 23,300 B data chunks with CS held between them), 10×65-row
DSI strips on the P4 — under the panel's own bus lock, keeping the LVGL core
free (§8.1). The S3 chunk size is the 25-row strip size by construction: the
PSRAM is not SPI-DMA-capable, so the io driver stages every chunk through
internal RAM, and the largest DMA-capable free block is ~31 KiB — the bus
`max_transfer_sz` is set to exactly 466×25×2 = 23,300 B in the vendored BSP
(the LVGL flush strips stay single-transaction at exactly that size; the io
driver's default 32 KiB chunking would stage one 32 KiB internal alloc that
does not fit — `ESP_ERR_NO_MEM` on the single-window write, 2026-11-01). The
per-strip variant (one CASET/RASET re-latch + CS toggle per strip) left a
visible seam line at every 25-row strip boundary (2026-11-01). The P4's spin
strips are deliberately SMALL: the P4's PSRAM is X16 and XIP (both cores fetch
instructions from the same PSRAM the strips read out of), so one large 206-row
(255 KB) strip read occasionally starves the DPI controller's FIFO ("can't
fetch data from external memory fast enough, underrun" — 1 per ~28 s of settled
spin at 206 rows, 0 across ~140 s at 103 rows, measured 2026-10-31; the 650 px
disk uses 65 rows / 84.5 KB — the same proven-safe class, 0 underruns on the
2026-11-01 boot).

**P4: one framebuffer, no flip (the spin's CCW flicker, fixed 2026-11-01).**
The P4's MIPI-DPI panel scans its framebuffer out _continuously_ (a 60 Hz
top-to-bottom DMA, no frame-complete on MIPI), and the DPI driver supports up
to three panel framebuffers (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS`, BSP default 3):
a `draw_bitmap` whose source is one of the panel FBs just flips `cur_fb_index`,
and the scanout DMA re-binds to it at the next wrap. The XC BSP's
`bsp_display_start()` default (the LVGL adapter's **TRIPLE_PARTIAL** tear-avoid
mode) runs exactly this: every LVGL refresh (the seconds knob's once-per-second
invalidation, a label change, a screen switch) full-blits from a _different_
panel FB and flips the scanout to it. But the spin's direct strip burst only
writes the FB that is being scanned _at that instant_ (every 125 ms), so the
other two FBs keep the spin pose from their **last visit** — an older θ. The
spin's θ only ever advances clockwise, so "older" reads as
**counter-clockwise**: each LVGL flip briefly shows the moon a few poses back,
then the next spin frame (≤125 ms) corrects it. That is the reported "sporadic
counter-clockwise images that jump back to the current frame" — sporadic (only
on LVGL refreshes, not every frame), CCW (stale = older θ), self-correcting.
P4-only by construction (the S3's QSPI panel is a single GRAM written
synchronously, no flip). Fix: the P4 runs the panel with **one** framebuffer
(`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1` in `sdkconfig.defaults.esp32p4`) and the
adapter's **NONE** tear-avoid mode (`bsp_display_start_with_config` in
`main.cpp`), so LVGL's strips and the spin's strips all write the one surface
the panel is scanning. The residual tearing is at most one frame — sub-pixel at
the settled spin rate, ~1 px at the rim at the active rate — invisible. The
one-FB change also frees the two unused 2.1 MB surfaces. (It does **not**
remove the spin's "previous draw operation is not finished" strip rejects —
measured 3855 pre-fix, ~3890 post-fix on the 650 px disk: those are the panel's
async-DMA throughput floor, a separate pre-existing phenomenon absorbed by
`moon_panel_draw_wait`'s retry — §3.2 — and the one-FB change fixed the scanout
flip, which was the flicker.)

### 4.4 Screen & state flow

```
boot: WiFi+NTP (blocking) -> airplane home + 336-frame synodic sweep -> free spin
touch/tilt wake: static current phase at the parked spin pose (no animation)
idle (runtime timeout, default 60 s): light sleep = the DUSK
   (pole spin, 1 deg/frame, constant projection, shade map baked once)
   -> last frame arms the RTC deep-sleep countdown (default 60 s)
deep sleep: dark side + clock hidden + backlight OFF
alarm (RTC minute poll): alarm screen (until dismissed by touch); fires from deep sleep too
tap on airplane/WiFi: settings hub (icon ring + central circle-xmark exit)
   sub-screens: alarm, pomodoro, alarm-time, light-sleep, deep-sleep, WiFi
   (the WiFi list's AP rows open the circular wheel keyboard, §7.13)
hourly (on the minute, at rest on airplane only): transient WiFi screen +
   NTP sync -> back to airplane
```

---

## 5. The moon calculation

### 5.1 Geometry

`moon_astro.c` computes, for a UTC epoch (ERFA `eraEpv00` + `eraMoon98` — the
same models the validated astropy pipeline uses internally):

- barycentric positions → Moon→Sun **S** and Moon→geocenter **E** (unit, ICRF);
- the **calibrated IAU_Moon body frame**: pole (RA 269.839977°, dec 68.110423°)
  and prime meridian `W0 + 13.17617251°/day × (JD - 2451545)` — this is where
  the **libration** enters: the body-frame basis carries the Moon's full
  libration in latitude/longitude automatically;
- the **camera basis** (celestial-north up, east left) rotated by the exact
  **posangle** — the on-screen orientation of the lit limb;
- outputs in `moon_geom_t`: S, E, right, up, sub-solar/sub-earth lon/lat,
  posangle, phase angle, illumination `(1 + cos i)/2`, distance.

Gate validation vs NASA: max **0.43°** sub-solar / **0.51°** sub-earth /
**0.44°** posangle over the 32-frame gate set — well inside the panel's 1-px
angular scale.

### 5.2 The bake (per pixel of the disk)

`moon_bake` (and the row-range `moon_bake_rows`) does, per panel pixel:

1. **Base-sphere direction** `r = u·right + v·up + w·E` from the on-screen (u,
   v) of the pixel (disk radius = MOON_PX/2: 200 px on the S3's 466 px panel,
   325 px on the P4's 720 px panel);
2. **Equirect albedo** (864x432, LROC WAC, row 0 = north, 180° longitude
   offset) gathered through the _live_ camera basis (libration + posangle exact
   per frame);
3. **Displaced-normal Lambert**: the surface normal is perturbed by the LOLA
   relief (2048x1024 int8 DEM, -8518…+10416 m, 254 steps) — a 4-neighbor
   central-difference gradient, pre-baked once into a packed uint16 slope cache
   (4,096 KB PSRAM, ~2.9 s at boot; one 16-bit load per pixel instead of a
   4-neighbor DEM gather) — and lit with the **unclipped** model
   `L = clip(lns, 0, 1)` with `lns = (d - G·S)/sqrt(1 + |G|²)`: mountain slopes
   brighten, crater walls shadow, exactly as the relief dictates;
4. **Night side (the backlit far side)**: on the device the unilluminated side
   is the **computed surface, not a flat overlay** — the real lunar surface
   (albedo + displaced relief) lit faintly from the _anti-solar_ direction
   (`moon_backlit_night`: `A · clip(−lns, 0, 1) · MOON_NIGHT_BACKLIT_K` with
   `K = 0.10`, peak ≈ 25/255 — about the old flat floor's level, now with the
   surface structure; plain linear gray, deliberately NOT the tone LUT, whose
   bin-1 shadow lift would over-brighten it). It is body-frame constant (the
   epoch's S is fixed), so the shade map and the spin stay a static image and
   only the projection rotates. The validated earthshine path (`dark_zero = 0`:
   `TONE_LUT[0] + earthshine(d)`) is byte-identical to the old model — it is
   what the gate and the host film run; `pole_test` proves BOTH the earthshine
   path and the backlit one (§12.2);
5. **`dk_sup` dark-side bound (dusk only)**: the pole spin never changes the
   view latitude, so the _endpoint_ view still carries a geometrically lit rim
   sliver (d up to `sqrt(1 - E·S(φ*)²)`) which would linger on the dark sleep
   screen as a bright arc hugging the disk edge (the user-reported "white ray"
   — the true root cause was the shade map's ±y mirror folding mirrored
   lighting onto the seam columns). The kill is now baked into the _map_: the
   shade map writes the backlit night side for `d < dk_sup + MOON_DK_MARGIN`
   (margin 0.010 — the re-project's nearest-neighbor gather can round a
   boundary pixel to a cell up to ~half a theta-cell + half a lat-cell away in
   d, ≈ 0.0073 at 864x432, so a lit-center cell would re-light the sliver
   without the erosion), and `moon_reproject` is a **pure gather** (no `dk_sup`
   parameter). The final bake writes the backlit night side for `d < dk_sup`.
   The sliver itself is sun-lit (lns > 0), so the backlit color clips to **pure
   black** there — exactly the old flat-floor kill, and the white ray stays
   dead. At the endpoint the visible disk is entirely d < dk_sup, so the band
   between the two bounds is never on screen (no pop). Every non-dusk caller
   passes `0.0f` (the margin is inert there — the night test reduces to
   `lns ≤ 0`, byte-identical to the pre-fix code);
6. **Tone LUT**: 256-entry 8-bit → 8-bit curve (generated, in `moon_consts.h`),
   then 8-bit → RGB565.

**View-independence** (the key architectural fact): a pixel's color depends
only on the body-frame surface direction and the epoch's fixed body-frame Sun —
the camera basis selects _which direction is visible_ but never enters the
color. Consequences: the free spin and the boot sweep need no lighting
recompute (fixed-point gather only), and the dusk is a **constant projection**
— one body-frame shaded equirect map (`moon_shade_map_build`, 864x432, PSRAM,
~1.6 s on the S3 / 282 ms on the P4, 2026-11-01) is re-projected through the
folded camera basis per frame (~50 ms, `moon_reproject`); the final dark-side
frame is the one pixel-exact full `moon_bake` at φ\* = `atan2(-B, -A)` (today
+134.78° orbit-folded).

### 5.3 Rotation (the free spin)

`moon_rotate`: a fixed-point nearest-neighbor in-plane gather ("ball in hand"),
int4-quantized 10°-step orientation applied per screen, angle integrated per
frame with the rate smoothed (EMA) toward the target — active period (default
300 s/turn) while in use, **constant slow spin** (600 s/turn,
`UI_MOON_SPIN_SETTLED_PERIOD_S`) when settled, so the moon never reads as
frozen before light sleep. ~8 ms gather per frame, then the composited MOON_PX²
composite is written **straight to the panel** (one continuous 400×400 QSPI
window write on the S3; 10×65-row DSI strips on the P4) at 8 fps
(`UI_MOON_SPIN_FPS`) — the render bypasses LVGL's re-render path (the wrapped
`esp_lcd_panel_draw_bitmap`), so the frame cost is gather + one panel write,
not the ~175 ms re-render.

### 5.4 The clock

PCF85063A (I2C behind the TCA9554): time-of-truth source; 1.3 s software poll
(the board's RTC interrupt does not reach the MCU — §3); minute alignment
drives the display update, the deep-sleep countdown (`clock_timer`), the alarm
scheduler, and the hourly NTP trigger. SNTP (`clock_sntp`, `pool.ntp.org`, 10 s
timeout) syncs at boot (blocking) and on demand (background task) —
wake/manual/hourly — after which the modem drops back to airplane.

---

## 6. Design-driven language

The UI's design system is a deliberate reduction to what the hardware is: a
round AMOLED showing the Moon.

- **Palette — moon grey / white on black, only.** `SETTINGS_GREY` (mid grey,
  unselected/inactive/structure) and `SETTINGS_WHITE` (active/value) on black.
  No other hue: the Moon has no color, so neither does the UI. Blue exists in
  no themed state (an early design's blue knob dots was explicitly removed —
  task #55). The clock digits and the moon share the white; the grey carries
  the "you can press this" affordance.
- **Type — Montserrat**, generated subsets (`lv_font_conv`, 4-bit,
  uncompressed; the S3's `fonts/` is the source of truth): 96 px digits (the
  clock), 48 px (rollers/labels), 32 px general text (SSID rows/captions/_
  password entry_ — full printable ASCII 32–126, so one font serves the list
  and the wheel keyboard; it was widened from an `A-Z`/digits subset for the
  keyboard) + a 32 px range subset (the "Mo-Fr" day summary, whose font needs
  only digits-free lowercase + hyphen) + 14 px (the wheel's register keys); the
  P4 builds the _same six subsets_ from the same Opts at ×720/466 (148/74/50
  px) in `fonts_p4/` with identical symbol names (§2.3b M4 state). Font Awesome
  7 Free (Solid 96 px: circle-xmark/moon/wifi/stopwatch; Regular:
  moon/alarm-clock) — the icon set is the same weight-and-color logic as the
  type.
- **Geometry — concentric circles.** The home screen _is_ the moon: disk center
  = screen center. The settings hub mirrors it: a central circle-xmark exit
  (the fa-circle-xmark glyph alone — the glyph _is_ the button, U+F057) with
  the sub-screen icons on a 45° diagonal ring at radius ~165; every
  sub-screen's exit is the same glyph, smaller, top-center. The alarm day range
  is a two-ended range arc with draggable handles — the same thin outer ring as
  the home seconds arc (outer radius 223, 10 px from the panel edge), the range
  in the moon colour.
- **Motion — the moon's own.** Screen switches **fade to black and back** via
  the panel's own hardware brightness (dim to black, load, brighten back —
  §10.4); the moon screens themselves switch atomically as always. The base
  +90° orientation is an LVGL root transform, so "screen center" is the panel
  center. Sleep is motion with meaning: the dusk _is_ the terminator
  travelling, not a fade.
- **Interaction — touch-only, gesture-honest.** Taps open; drags on rollers
  (persist only on RELEASED — SPIFFS/I2C stay off the drag path); the day-range
  handles snap; an idle timeout (runtime) dims into light sleep; waking shows
  the _current_ phase statically — the device never re-animates history.

---

## 7. Screens — every live screen with its settings

All 13 screens, exact panel pixels from the UI simulator. The "settings
displayed" column lists what the screen shows and what each control changes
live.

### 7.1 Airplane (modem sleep) — the home screen

![airplane](img/screens/modem_sleep.png)

- The moon disk at the current phase (the free spin animates it), the clock (96
  px HH:MM, two-digit), the live seconds arc around the disk, and **below the
  clock the next alarm** — the HH:MM of the next scheduled fire, preceded by
  the two-letter weekday of that fire ("Mo 07:00", 32 px range font, grey)
  whenever the fire is more than 24 h away; within 24 h the prefix drops and
  the time stands alone (the weekday only carries meaning across a day the rule
  skips).
- **No icons** — the moon stays in the foreground (user decision).
- Tapping anywhere (the clock container is non-clickable so the tap falls
  through to the screen root) opens the settings hub.
- This screen is also the **sleep state**: idle timeout → light sleep. While it
  hosts the boot, the synodic sweep plays here.

### 7.2 WiFi (transient)

![wifi](img/screens/wifi.png)

- Shown only while a sync is in flight (wake / manual / hourly / NTP-failed
  boot); drops back to airplane on completion.
- Same moon + clock as airplane, plus the **connection indicator below the
  clock** (grey = connecting, white = connected; non-clickable).
- The boot sweep plays on this screen when the boot clock is still being
  acquired (the seamless hand-off: both screens share the one moon image).

### 7.3 Light sleep — the dusk

![light sleep](img/screens/light_sleep.png)

- The pole-axis spin to the fully unilluminated side (constant projection,
  1°/frame, shade map baked once — S3 ~1.6 s / P4 282 ms, 2026-11-01; final
  frame a pixel-exact bake at φ\*; the dark side is the **faint backlit
  surface** — the real lunar terrain lit from the anti-solar direction, peak ≈
  25/255, so the far side stays visible on the AMOLED without a flat overlay).
- The clock is **dimmed to ~20% opacity** (the panel has no alpha channel —
  opacity is the only "alpha"; the moon image is a sibling and stays
  full-contrast).
- On the last frame the deep-sleep countdown arms (runtime
  `deep_sleep_countdown_sec`, default 60 s).

### 7.4 Deep sleep

![deep sleep](img/screens/deep_sleep.png)

- The dark side at φ\* (the final dusk bake, its backlit far-side surface),
  **the clock hidden**, and the **backlight off** after the countdown — the
  panel is genuinely dark (AMOLED), the RTC keeps counting; an alarm can fire
  from here and restore the backlight. Clock updates are gated while the
  backlight is off (no live minute updates in deep sleep). The backlight off/on
  (here, on wake, and at boot) goes through `moon_ui_panel_brightness_set` —
  the 0x51 write is serialized on `s_panel_mtx` so it is never queued behind a
  spin strip DMA that could stall the bus (§10.4).

### 7.5 Alarm (fired)

![alarm](img/screens/alarm.png)

- Shown when the scheduler fires (RTC-minute-poll; also from deep sleep,
  backlight restored). Touch dismisses and re-arms. The screen carries the
  alarm indicator icon and the time; there is **no automatic sleep transition**
  while it is up (task: the alarm must not dusk-sleep mid- dismiss).

### 7.6 Settings hub

![hub](img/screens/settings_hub.png)

- Central **circle-xmark exit** — the fa-circle-xmark glyph alone (U+F057; the
  glyph is the button, no ring chrome; 150 px tap box) → back to the **home
  (airplane) screen**, no sync kicked off.
- Icon ring (45° diagonals, 96 px glyphs, clickable): alarm settings
  (alarm-clock, regular), light sleep (moon, regular), deep sleep (moon, solid
  — the weight distinguishes the two), WiFi (wifi, solid).
- Grey = present/available, white = selected; the sub-screens' bottom-centre
  exit is the same glyph at 96 px and returns to the **calling level** (the
  hub, or the alarm screen for its two sub-screens).

### 7.7 Alarm settings

![alarm settings](img/screens/settings_alarm.png)

- Header (alarm-clock, grey). Three glyphs: the **large central alarm-clock**
  is the master deactivate-all toggle (white when any rule exists, grey when
  none; tap → `clock_alarm_deactivate_all()` — the 0-rule boot also disarms the
  PCF85063A's persistent ALM); the **stopwatch** (pomodoro) and the
  **alarm-clock** (real alarm) are mutually exclusive — activating one replaces
  the other's rule. Colors refreshed on entry and after every save.

### 7.8 Pomodoro

![pomodoro](img/screens/settings_pomodoro.png)

- Two `lv_roller`s (hours 0-23, minutes 0-59), 48 px digits,
  `visible_row count(1)`, grey text / white selected row (LV_PART_INDICATOR),
  transparent bg; HH:MM preview centered like the clock label (two-digit,
  zero-padded, with the colon — task #68).
- `RELEASED` persists `minutes_total = H*60+M` (reject 0) via
  `clock_alarm_set_pomodoro_interval()` → the cron rule `*/N * * * *` (N up to
  1440 — the scheduler's total-minute grid across the day). Seeded from the
  current interval rule.
- **Settings displayed:** the current interval (HH:MM), which the two rollers
  edit; the write + scheduler re-parse are logged
  (`Parsed alarm rule: every N minutes`, `Next alarm scheduled for ...`).

### 7.9 Alarm time (the day-range arc)

![alarm time](img/screens/settings_alarm_time.png)

- **Day range**: a two-ended range arc on the **same thin outer ring as the
  home screen's seconds arc** (a 466×466 object at (0,0); ring outer radius
  223, 10 px from the panel edge) — the former 7 weekday letters are gone (task
  #91), so the ring owns the outer band. The dark track (LV_PART_MAIN) spans
  the upper half-circle (Monday left, Sunday right); the **moon-colour
  (SETTINGS_GREY) indicator** spans the range; two 34 px grey handles at r 218
  are the range endpoints (PRESS_LOCK; PRESSED/PRESSING snap the endpoint to
  the nearest of the 7 day angles 181+30°·d; RELEASED saves).
- **Day summary**: the "Mo-Fr" range text (32 px range font — lowercase +
  hyphen subset) above the time carries the day meaning (a single day shows
  "We"; wrap included, "Fr-Mo").
- **Time**: 96 px HH:MM at the clock position with two 48 px rollers (hours,
  minutes ×5) and the colon between; RELEASED saves
  `clock_alarm_set_clock_alarm(h, m, dow_mask)` → the cron rule
  `M H * * <dow>`.
- A self-diagnosing **read-back log** prints after every save
  (`read-back: rule_types=0xNN fixed=ok got HH:MM mask 0xNN MATCH|MISMATCH`) so
  a device regression localizes to the write/re-parse layer vs the UI color
  refresh.
- **Settings displayed:** the fixed time (HH:MM), the day range (ring + "Mo-Fr"
  summary), and — via the save log — the parsed rule the scheduler re-loaded.

### 7.10 Light-sleep settings

![light sleep settings](img/screens/settings_light_sleep.png)

- Header moon (regular). One 48 px roller (in **minutes**, 1-30) + one
  icon-only animation toggle (96 px solid moon, white = on / grey = off; no
  switch widget) on one horizontal line. The roller is the **idle timeout** —
  `RELEASED` → `settings_set_idle_timeout_sec` + live
  `clock_idle_set_timeout_ms()`
  - store save (takes effect immediately). The toggle is the **sleep-transition
    animation** (the dusk) — `dusk_anim_enable` (the boot-sweep flag
    `boot_anim_enable` stays in the store but is not exposed here).
- **Settings displayed:** the idle timeout value + the dusk flag.

### 7.11 Deep-sleep settings

![deep sleep settings](img/screens/settings_deep_sleep.png)

- Header moon (solid). One 48 px roller (in **minutes**, 1-30) + the same
  icon-only dusk toggle. The roller is the **deep-sleep countdown** —
  `RELEASED` → `settings_set_deep_sleep_countdown_sec` + save (takes effect at
  the next dusk arming).
- **Settings displayed:** the countdown value + the dusk flag.

### 7.12 WiFi settings

![wifi settings](img/screens/settings_wifi.png)

- Header (wifi, solid). A **scan list** (background scan task: `esp_wifi_init`
  → STA → blocking scan → `esp_wifi_stop()` — scan only, credentials untouched,
  back to airplane): 7 rows at 24 px (the old 12 ran off the bottom of the
  panel), SSID upper-cased for font display (32 px font), grey = available /
  white = selected + a left-margin white bar on the selected row. **A tap on a
  row opens the circular wheel keyboard** (§7.13) with that AP's SSID prefilled
  (case kept — the upper-casing is display-only, the credential is written
  raw).
- **Settings displayed:** the live AP list from the radio (RSSI-ordered), the
  selection state.

### 7.13 WiFi password entry — the circular wheel keyboard

![at rest](img/screens/settings_wifi_password.png)
![touched down — outer frame revealed, hot letter](img/screens/settings_wifi_password_touched.png)
![123 mode](img/screens/settings_wifi_password_123.png)

Reached by tapping an AP row on the WiFi screen (§7.12). A 13th screen
(`SCREEN_ID_SETTINGS_WIFI_PASSWORD`) built around the panel centre (466×466
centre on the S3; the wheel's radii are `UI_PX()`-scaled, §2.3b M4 state); no
moon image (settings are moon-less by design).

- **Two concentric letter rings.** The **inner frame** (at rest) shows the 26
  slots of the active register, **grouped in threes** (a bold divider at each
  group start, thin within; the alphabet is 8 groups of 3 + a final group of 2)
  at r 150–186 — the grouping is the wayfinding device: the eye aims at 9
  groups, not 26 targets. **On touch-down anywhere in the ring band (r 140–
  233, radially forgiving), the outer frame reveals** at r 192–226 where the
  letter under the finger is lit over a **2× the inner arc angle** (27°) — the
  adjacent 2× sectors overlap by one inner letter width and the hit-test
  resolves them as **nearest letter centre**, so a deliberate sweep is
  unambiguous. Release commits the letter under the finger (no auto-commit on
  zone crossing, so a slow sweep can never double-enter).
- **Register/shift arc:** 4 individually-tappable switch keys
  `abc / ABC / 123 / #$` at r 88–116 (LVGL 42°–138°), the active one lit; a tap
  snaps to the nearest key centre and re-strings the 26 slots (digits:
  `0123456789`; symbols: `!@#$%^&*()-_+=[]{}:;.,?/`). Letters keep the full
  360° in every mode; case is separate (lower/upper) because it matters for
  passwords.
- **Centre:** the running password **unmasked** (the user is physically
  present) — the last 6 characters with a `…` prefix when longer, the `…`
  placeholder when empty — plus the selected letter large while sweeping. **A
  short tap on the centre commits**: `wifi_credentials_write(ssid, pw)` (two
  lines to `/spiffs/wifi_credentials.txt`, the password never logged) →
  `action_start_wifi_sync()` (the 6144 B background task: `wifi_manager_init`
  - NTP) and the screen swaps to the transient WiFi screen (§7.2); on success
    the app leaves airplane mode. **A long press (400 ms, repeating)
    backspaces.** An empty password is not written.
- **Exit:** a small (26 px) in-ring circle-xmark at the bottom of the wheel — a
  documented deviation from the house 90 px bottom-centre exit, which would sit
  under the full-360° ring. It returns to the WiFi list (no write, no sync).
  The screen's angular picking uses the inverse-transform idiom
  (`lv_obj_transform_point`, `LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE`, the
  `alarm_time_point_to_day` pattern), so it stays correct under the 90°
  orientation roll.
- **Known interaction (stated, not suppressed):** the global 2 s press
  orientation re-sync also fires on long sweeps; it is pose-stability gated and
  only re-anchors the home reference, and the wheel is transform-aware, so a
  mid-sweep re-pose cannot move the letter under the finger.
- **LVGL note:** the clickable wheel arcs carry `LV_OBJ_FLAG_ADV_HITTEST` —
  without it `lv_arc` falls back to a plain bounding-box hit test (its 466×466
  box swallows every press) and the last-created arc wins the search.
- **Alternatives surveyed (2026-11-01):** the research note
  `docs/research/circular-keyboard-alternatives.md` evaluates the circular
  wheel against the published round-display text-entry literature (C-QWERTY,
  COMPASS, Crownboard, Tap'nSwipe, in-car dial studies) for this hardware — the
  sweep wheel is confirmed as the right family; a 12-sector DTMF-style
  multi-tap ring and hold-to-commit dwell are the ranked low-cost additions.
- **Settings displayed:** the prefilled SSID, the running password, the active
  register, the selected letter.

---

## 8. Burn-in

The product runs an always-on AMOLED with a mostly-static disk; burn-in is a
_design requirement_, not an afterthought. The statements, in the order they
were made and implemented:

1. **The lit pixels must move, continuously, at rest as well as in use.** The
   free spin is the burn-in mechanism: while the device is in use it rotates at
   the active rate (default 300 s/turn); once it settles the rate eases to a
   **constant slow spin** (default 600 s/turn) that never stops — the
   phase-true coast it replaced (one turn per synodic month) was effectively
   invisible and read as the moon _freezing a minute before light sleep_. The
   continuous slow rotation keeps moving the lit pixels around the panel
   forever while the device sits on a table.
2. **The sleep state must not park the disk either.** The dusk is a
   _constant-projection pole spin_: the terminator (and the lit crescent's
   relief detail) travels the disk for the whole light-sleep duration (14-95 s
   at 1°/frame), and the dark-side frame the countdown lingers on still carries
   the **computed backlit surface** (the far-side terrain lit faintly from the
   anti-solar direction, §5.2 item 4) — a moving _pattern_ during the sleep, a
   faintly textured (not flat) disk while dark. (The earlier flat variants were
   both rejected: `dark_zero = 0` → 0x0000 left the far side invisible, and the
   constant `MOON_NIGHT_FLOOR = 10` overlay it replaced had no surface
   structure; the backlit model keeps the floor's level with the terrain
   added.)
3. **Orientation is stepped, never ramped.** The screen base orientation is
   applied in **90° steps** (int4-quantized IMU roll, 100 ms samples, 1 s
   windowed mean, sustained confirmation, instant apply on the 12 roots) — a
   continuous ramp would have parked the clock's pixels in intermediate
   positions; the steps put every pixel in one of four known poses, and the
   spin + dusk keep moving them between the settle periods.
4. **The screen-switch fade does not add a static element** — the panel's OWN
   hardware brightness steps the whole panel to/from black (the CO5300 0x51
   command, the same dimming the deep-sleep backlight off/on uses); no overlay
   is drawn, and while the fade ramp runs the spin holds its panel writes
   (`s_fade_busy`) — the 0x51 ramp is the only panel-io traffic in flight, so
   the moon's image freezes for the ~240 ms ramp (its angle keeps integrating,
   so it resumes from the exact pose).
5. **Residual risk, stated plainly:** the _clock digits_ are the only truly
   static elements (they move with the 90° steps and with the tilt-driven roll,
   but between rolls they sit still). At the AMOLED's always-on duty cycle the
   digits are the long-term burn-in candidate; the mitigation is the stepped
   re-orientation (a roll of the device re-poses them) plus the low digit
   opacity in sleep (dimmed to ~20% in light sleep, hidden in deep sleep with
   the backlight off).

---

## 9. Animations & films

Frames are the device's exact panel pixels (the portable core, RGB565),
rendered on the host and encoded here.

**Boot sweep** — one synodic month, new moon → now (336 frames @240 px, the
device's own sweep; ~2.8 min on-device, 67 s at 5 fps here):

![boot sweep montage](img/boot_sweep.png)
<video src="img/boot_sweep.mp4" controls width="466"></video>

**Free spin** — the settled animation at the current phase (36 frames @466 px,
10° steps; on-device each frame is one ~8 ms nearest-neighbor gather of the
baked 400×400 phase + a direct panel write of the whole 400×400 window (one
CASET/RASET, 23,300 B chunks with CS held — no per-strip re-latch), at 8 fps by
default — the frame lands on the panel without an LVGL re-render in between):

![free spin montage](img/free_spin.png)
<video src="img/free_spin.mp4" controls width="466"></video>

**Dusk** — light-sleep entry: the pole-axis spin, 1°/frame, to the
fully-unilluminated side (the film stops at the detected fully-dark endpoint;
on-device the final frame is a pixel-exact bake at φ\* = +134.78° for the
2026-10-29 epoch, orbit-folded):

![dusk montage](img/dusk.png)
<video src="img/dusk.mp4" controls width="466"></video>

**Fade-to-black screen switch** — airplane → settings hub: the panel's OWN
hardware brightness dims the outgoing screen to black (a linear 0x51 ramp — no
pixels written; the spin holds its panel writes for the ramp's ~240 ms, so the
moon's image freezes while its angle keeps integrating), the new screen loads
and is flushed atomically while the panel is still black, then the brightness
ramps back and the new screen comes up at once (`UI_SCREEN_SWITCH_FADE`,
default 240 ms per leg). EVERY screen switch takes the fade — home ↔ settings
(sub-menus included) and to/from the alarm screen; the one exception is the
both-moon switch (airplane / WiFi / the sleep screens share the one moon
image), which stays the plain atomic whole-panel display:

<video src="img/fade.mp4" controls width="466"></video>

---

## 10. Implementation notes

### 10.1 Fixed point & PSRAM

All per-pixel hot paths are fixed-point or table-driven (the spin's
int4-quantized sin/cos; the slope cache's packed uint16 cells; the 256-entry
tone LUT). The large buffers (s_frame, the 8-bit frame, the shade map, the 4 MB
slope cache) live in **PSRAM**; LVGL's allocator is the C-library backend (the
built-in 64 KB pool cannot hold a full-screen transformed layer — the original
tilt-crash). PSRAM is **not SPI-DMA-capable** on this board → every display
strip is staged through internal RAM (§4.3).

### 10.2 The atomic-flush invariant & its history

Every moon frame is one whole-panel `lv_refr_now` under `s_rgb_mtx`. History:
the first design used LVGL's `FADE_IN` screen-load — the fade's pending
invalidation re-invalidated the panel for 200 ms and rode along in the spin's
atomic full-frame flushes, producing the "nasty top-to-bottom redraw artifacts"
on wake. Fix: plain `lv_scr_load` + one atomic flush. The wake path
additionally froze the display ~1.7 s when the _synchronous_ WiFi reconnect
blocked the LVGL task — fixed with a background reconnect task and the screen
upgrade from the UI tick.

### 10.3 Tilt & orientation

- **Tilt drives the clock roll** (orbit off on the S3 — the ~1.7-2.4 s relief
  re-bake cannot serve the tilt cadence): `clock_roll_apply` measures in-plane
  roll (shortest-path EMA a=0.2), keeps a settled-pose reference (updated only
  while the tilt monitor reports settled), dead- zones 1°, and counter-rotates
  the clock containers via LVGL style transforms. Wake fires on **departure
  from the settled-pose EMA** (two bars + a 2-sample streak), not raw
  per-sample deltas — the fix for "wakes right after sleep".
- **Base orientation** (task #59/T1): +90° step offset applied to the 12 screen
  roots; the 180°-flip reference re-tracks only on `step == 0` (T4). A rotated
  root's panel-sized layer (~0.4 MB at 466², ~1.0 MB at the P4's 720²) is
  allocated via CLIB malloc (the 64 KB LVGL pool OOM was the original
  "off-screen + crash").
- **2 s press re-sync** (2026-10-30): the home-pose anchor latches from the
  first 1 s of IMU samples after boot; if the device was flipped while that
  window filled, the anchor is 180° off and the UI stays upside down — and the
  T4 rule (re-track only on step 0) means no tilt recovers it. A deliberate
  press of 2 s or more (`action_register_orientation_touch`, the indev
  PRESSED/RELEASED pair) re-anchors the reference to the pose held while
  pressing and restores the base orientation on the next sample (~100 ms). The
  press is the user's "this pose is the home pose".
- **The re-sync gates (2026-10-30, the "flipping unreliable" fix):** the
  un-gated request re-anchored home to _whatever_ pose was held at the end of
  any ≥2 s press — a roller drag in settings, a palm resting on the screen
  while the device was being flipped. Mid-flip, that adopted the new pose as
  home and cancelled the flip correction. The request is now actioned only when
  the 1 s filtered gravity has been stable (within 1 int4 level per 100 ms
  sample for 15 samples ≈ 1.5 s) **and**, once an anchor exists, sits on the
  anchor's home axis (within ~35° of upright or upside-down,
  `|dot|² ≥ 3|cross|²` on the integer quantized vectors). The boot anchor seed
  got the same stability gate, so a device still being placed at boot cannot
  latch a transient pose as home.

### 10.4 The screen switch: fade-to-black (replaced the concentric reveal)

`CONFIG_UI_SCREEN_SWITCH_FADE` (int ms, default 240, 0 = plain atomic):
**every** screen switch (home ↔ settings including the sub-menu switches, and
to/from the alarm screen) dims the panel's OWN hardware brightness to black,
loads the new screen, and ramps the brightness back:

- **The dimming** is the CO5300's brightness command (`0x51`,
  `bsp_display_brightness_set`) — the same hardware dimming the deep-sleep
  backlight off/on uses. It scales everything on screen at once (the moon, the
  labels, the spin's direct panel writes) and costs one ~100 µs SPI param
  transaction per step. A fade step therefore writes **no pixels**: the
  transition costs nothing on the panel bus and shows no strip crawl.
- **Fade-out** (`moon_ui_fade_out`, in `loadScreen` _before_ `lv_scr_load`):
  sets `s_fade_busy` (the spin keeps integrating its angle but **skips its
  panel writes** — see the freeze fix below) and ramps the brightness 100 → 0
  in `FADE_STEPS` = 12 steps paced `UI_SCREEN_SWITCH_FADE / 12` ms (12 small
  luminance steps stay smooth), each step under `s_panel_mtx`. The flag
  `s_fade_pending` is left set.
- **Fade-in** (`moon_ui_screen_switched`): if `s_fade_pending`, the loaded
  screen is displayed as ONE atomic whole-panel `lv_refr_now` under `s_rgb_mtx`
  **while the panel is still black** (invisible — the invariant's one atomic
  display, off the visible path; the per-strip panel lock comes from the flush
  wrapper itself), then the brightness ramps 0 → 100 (under `s_panel_mtx`) and
  the fully rendered new screen comes up at once. `s_fade_busy` clears, and the
  spin resumes its panel writes from the exact pose it was at.
- **No fade:** the one exception is the both-moon switch (airplane / WiFi / the
  sleep screens share the one moon image), which keeps the plain atomic
  `lv_scr_load` + one whole-panel flush — a fade there would only dim the moon,
  which is the same disk on both sides. (The settings sub-menu switches fade
  too: a fade leg is screen-agnostic — the fade-in's atomic flush works on any
  loaded screen, moon or not.)

Why it replaced the old concentric reveal (the center-out strip reorder,
`9dad4fe`): the reveal's visible failure mode was "white bars updating the
screen" (the strip crawl at the panel's QSPI rate) plus a rotated ghost —
`lv_snapshot_take` renders the screen in its _local_ coordinates, NOT through
the root's LVGL transform, so the center-out write showed unrotated content
until the trailing atomic flush fixed it (the rotation fix `5550a6d` and its
direction bug `9c96a12` — "90° CW" computed 90° CCW, painting the previous
screen as a mis-oriented full-panel ghost; the alarm screen, visited every 5
minutes by the then-active `*/5` debug rule, showed it). The fade has no strip
ordering, no snapshot-rotation coupling to the orientation pipeline, and no
intermediate frames that aren't simply "the screen, darker", so nothing a user
can see is ever a ghost or a bar.

**The overlay fade → the hardware ramp (2026-10-31):** the first fade
implementation composited a full-screen black overlay in LVGL and re-rendered
the whole 466×466 screen for each of its 4 steps (one atomic `lv_refr_now` per
step). That was still a _display-bound_ transition: every step charged ~175 ms
of QSPI strip traffic, and the panel's RAMWR fill sweeps each 25-row strip
left-to-right — the user saw each grey step "update left to right for about a
second". The fix uses the panel's own brightness command instead, so a fade
step writes no pixels at all: the ramp is free, and the panel traffic of the
whole switch is the ONE atomic flush of the new screen (which happens while the
panel is black and is invisible).

**The settings-entry freeze → serialization + watchdog (2026-10-31):** the
first device field test of the hardware ramp found that the home → settings
switch could freeze the device PERMANENTLY (white stripes stuck mid-draw). The
coredump (UART, decoded with GDB against the app ELF + ROM ELF) showed the
mechanism: `lvgl` and `moon_spin` both parked in
`spi_device_acquire_bus(portMAX_DELAY)` on the SAME GPSPI bus-lock semaphore,
and NO task held the bus inside the driver — the bus lock's background (REQ/
PENDING) bit was stuck. A QSPI strip DMA's completion was lost, so the lock's
FSM (`spi_bus_lock.c`: task-acquire vs background-queued ownership) routed
ownership to a background owner that would never release: every later
`acquire_bus` blocked forever with the panel mutex held, and the parked tasks
kept the idle tasks alive, so no WDT ever fired. lvgl was inside the fade's
`0x51` write; the spin inside a RAMWR strip. The fix, all app-side (no vendor
files touched):

- **Serialize the 0x51 writes on `s_panel_mtx`** — the fade's ramp steps and
  the backlight paths (wake, deep-sleep off/on, boot) all take the panel mutex
  around `bsp_display_brightness_set` (the boot/wake/deep-sleep call sites use
  the new `moon_ui_panel_brightness_set` wrapper for exactly this), so a
  brightness write is never queued while a strip DMA is in flight.
- **The spin yields the bus to the fade** — while `s_fade_busy` the spin keeps
  integrating its angle but skips `moon_spin_write_panel`; the 0x51 ramp is
  then the ONLY panel-io traffic in flight during the ~240 ms ramp (the user's
  original ask: stop the heavy panel work before the transition).
- **A backstop against the next lost completion** — a `panel_wdg` task (core 0,
  prio 3, 4 KB) sleeps 3 s at a time, then probes `s_panel_mtx` with a 3 s
  bound: if it is still held, the log says
  `PANEL IO STUCK: … the QSPI bus lock is stuck (a DMA completion was lost); restarting`
  and the device `esp_restart()`s — the silent dead screen becomes a logged,
  recoverable reboot. Legitimate `s_panel_mtx` holds are <100 ms (spin
  composite + strip write; LVGL flush wrapper per strip), so the 3 s bound has
  ~30× margin.

Verified on device: the deterministic repro (home → hub fade with the free spin
active) passes cleanly, repeatedly; 4 light-sleep/dusk/wake cycles, the
deep-sleep backlight off and the wake-from-deep-sleep backlight on (now
`moon_ui_panel_brightness_set`), and a full settings demo all clean — 0
`PANEL IO STUCK`, 0 WDT, 0 panics, 0 reboots across ~18 min of runtime.

### 10.5 Alarms, settings store, cron

- **Alarm scheduler**: cron-like rules (`*/N * * * *` intervals incl. N > 59
  via a total-minutes grid across the day with `mktime` date rollover;
  `M H * * <dow>` fixed times), written to `/spiffs/alarm.txt` and
  re-parsed+re-scheduled on every write (`clock_alarm_scheduler_ reload`; 0
  rules → `clock_clear_alarm()` disarms the PCF85063A's persistent ALM — a
  stale armed time would otherwise fire a phantom alarm screen on a 0-rule
  boot; on no-PCF builds (the P4) the guard is `clock_alarm_disarm_esp_timer()`
  - dropping the scheduler's `s_next_alarm` cache — esp_timer is not
    persistent, but the cache is). Firing is the 1.3 s RTC minute poll (on the
    P4: the one-shot's flag + the wall-clock backup check, gated on
    `clock_sntp_time_trusted()`).
- **Settings store** (`/spiffs/settings.txt`, `key = value`):
  `idle_timeout_sec`, `deep_sleep_countdown_sec`, `boot_anim_enable`,
  `dusk_anim_enable` — `#ifndef`-seeded from Kconfig, live overrides (idle
  timeout applies immediately; countdown at the next arming; animation flags at
  the next boot/dusk).

### 10.6 Power flow

Airplane = modem sleep (WiFi deinit'd). On-demand sync: wake/manual/hourly arms
a background task (LVGL never blocks on the radio — the wake-freeze fix); the
hourly trigger fires at the minute-0 tick **only at rest on the airplane
screen** (no waking a sleeping device, no interfering with the boot sweep or a
settings session). Deep sleep turns the backlight off; the RTC alarm (if any)
fires from there.

---

## 11. Measured timings

Device captures (pyserial 115200, unattended runs with the test hooks):

| Stage                                         | Value                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Relief gradient cache (boot, 4,096 KB packed) | **2,913 ms** once (S3; the P4 measures **~1.0 s** for the same cache)                                                                                                                                                                                                                                                                                                                                                                          |
| Boot sweep (S3)                               | 336 frames @240 px; bake **~78-82 ms**, display **~305 ms**/frame → **~2.8 min** total                                                                                                                                                                                                                                                                                                                                                         |
| Boot sweep (P4)                               | 336 frames; intermediates @240 px bake **~64-138 ms** (illumination-dependent), display **~155 ms**/frame; the FINAL frame bakes at full **650 px in ~595 ms** (the 618 px bake measured ~599 ms on 2026-10-31); **~121 s** bake total (21 d moon age)                                                                                                                                                                                         |
| Free spin                                     | **~8 ms** fixed-point gather + direct panel write per frame (wrapped `draw_bitmap`, LVGL bypassed) — one continuous 400×400 QSPI window write on the S3 (CASET/RASET once, 23,300 B chunks CS-held), 10×65-row DSI strips on the P4 — 8 fps; settled 600 s/turn; 0 WDT resets (the P4's small strips kill the DPI FIFO underrun a 206-row strip caused — 65 rows on the 650 px disk, §4.3)                                                     |
| Dusk                                          | shade-map build **~1.6 s** once (abortable) + re-projected frames **~50 ms** each (1°/frame; 14-95 s to φ\*) + final full-res bake + 30 s default countdown (P4 2026-10-31: 104 frames / **43.1 s** / +104°, final 650 px bake clean, 0 DSI underruns; P4: the countdown arm previously soft-failed `ESP_ERR_INVALID_STATE` on the absent PCF85063A — M5.1 (2026-11-01) re-routes it to the `esp_timer` one-shot, device verification pending) |
| Wake                                          | static current phase at the parked pose (no animation); background WiFi reconnect (no display freeze)                                                                                                                                                                                                                                                                                                                                          |
| Screen switch (moon)                          | one atomic whole-panel flush, ~175 ms of core-0 work (display-bound)                                                                                                                                                                                                                                                                                                                                                                           |
| Screen switch (every switch fades)            | two hardware brightness ramps (the CO5300 `0x51` command, 12 steps each × ~100 µs SPI transactions, `UI_SCREEN_SWITCH_FADE` 240 ms per ramp, each step under `s_panel_mtx` with the spin's panel writes held) + ONE atomic whole-panel flush of the new screen — it runs while the panel is still black, so the fade itself writes no pixels and costs zero strip traffic                                                                      |
| Settings hub idle CPU                         | **7-9 % of one core** (after the 10 ms adapter `task_min_delay_ms` fix; the old 1 ms floor made `vTaskDelay(0)` a yield and the LVGL task spun ~3500 Hz at 73-91 % of core 0), UI loop 93-95 Hz                                                                                                                                                                                                                                                |
| Image gate (host)                             | C vs NASA: max MAE **8.90** ≤ 12, SSIM **0.908**; C vs Python: max **1.31** ≤ 2 (32-frame set)                                                                                                                                                                                                                                                                                                                                                 |
| Binary (2026-11-01, after M5.1)               | S3: `0x162170` = 1,450,352 B in the 2,097,152 B partition (646,800 B free — the factory was widened 1392 K → 2048 K for the circular wheel keyboard). P4: `0x168b60` = 1,477,472 B in the 9,437,184 B partition (~7.6 MB free)                                                                                                                                                                                                                 |

---

## 12. Test system (with the NASA reference files)

The product's physical-correctness claim is gated, not asserted:

### 12.1 The image gate — `host_sim/check_gate.py`

Full path: `host_sim/check_gate.py`. Run:
`host_sim/.venv/bin/python host_sim/check_gate.py [--sim host_sim/build/moon_sim] [--out /tmp/moon-sim-gate]`.

Two diffs over the 32-frame gate set, both at 5-bit panel fidelity (the
device's exact gray→RGB565 quantization):

1. **C vs NASA** — the runtime path (eraEpv00+eraMoon98 ephemeris, fixed-point
   rotation, gray→RGB565) against the **NASA SVS 5415** reference frames. Gate:
   per-frame MAE ≤ 12 (the prototype's metric); mean content SSIM reported.
2. **C vs Python** — porting fidelity against the validated Python prototype
   (float astropy ephemeris, the same banded relief-DEM lighting, 5-bit
   quantized). Gate: per-frame MAE ≤ 2. Both renders are pixel-aligned
   (identical disk geometry), so the diff is direct.

If check 2 passes, check 1 inherits the prototype's NASA agreement — the
residual is exactly the porting error.

**NASA reference files** (full paths, working tree; the set is 8,760 frames of
the SVS 5415 400x400 lunar image series, gitignored):

- `assets/images/moon/final_400x400/moon.0001.png` … `moon.8760.png` — the
  complete reference series (32 frames selected by
  `moon.validate.select_frames` for the gate, spread across phases).
- `docs/img/nasa/nasa_00000.png`, `nasa_02190.png`, `nasa_04380.png`,
  `nasa_06570.png`, `nasa_08759.png` — 5 committed samples (beginning / 25 % /
  50 % / 75 % / end of the series) so the hosted document shows the references
  the gate runs against.
- `assets/images/moon/moon/` — the Python prototype package (`astro.py`
  geometry, `renderer.py`, `textures.py`, `metrics.py`, `register.py`,
  `validate.py`, `constants.py`, `assets.py` — LROC/LOLA download + the
  `moon.assets` generator, `calibrate.py`, `animation.py`), and
  `assets/images/moon/moon_assets_gen.c` + `moon_assets.bin` — the generated
  device assets.

### 12.2 The host simulators

- `host_sim/main.c` (`moon_sim`) — the **exact device core** built for the
  host; writes PNGs at the real panel size (`--size 466` device / `--size 400`
  gate geometry; `--rot`, `--pole` (a 1:1 copy of the device's dusk spin),
  `--darkzero`, `--list`/`--frames` batch modes). This is also the film
  renderer (`docs/gen_media.py`).
- `host_sim/pole_test.c` — the dusk geometry proof (ad-hoc build, not a CMake
  target): right-sign fold beats wrong-sign 2.5×; φ\* analytic == grid minimum
  to 1e-5; the dark-side frame keeps contrast; map-once vs per-frame re-bake
  spatial MAE with the no-flicker proof (temporal frame-delta within 0.02-0.14
  gray); and the **backlit night-side section** (the device's `dark_zero = 1`
  path): per epoch, the shade map (backlit night side + `dk_sup + margin`)
  re-projected through a fold must match the pixel-exact backlit bake to
  cell-center sampling (MAE < 7, endpoint folds at ~0.2–0.5), with zero lit
  pixels and > 50% backlit pixels at the endpoint — the no-pop proof for the
  computed dark side.
- `host_sim/uisim/` — the **real device `screens.c`** (unmodified) on host LVGL
  9.6 (SW renderer, RGB565, 466x466 by default, ESP stubs) → per-screen PNG
  snapshots (`build.sh run`); `build.sh run --panel 720` renders the P4's
  720x720 panel (its own `build-720/` dir, the 650 px moon disk, the panel-
  scaled UI + the `fonts_p4/` font set — the font dir and `-DPANEL_SIZE` both
  follow the `--panel` arg, mirroring the device's `CONFIG_BOARD_P4` switch) —
  the source of every screenshot in §7 (the P4 set under
  `docs/img/screens/p4/`). Two diagnostic probes from the reveal era
  (2026-10-30), kept as the verification record: `--rottest` proves
  `lv_snapshot_take` renders in the screen's **local** coordinates (the root's
  LVGL rotation is ignored — the fact the spin's UI keep-mask now relies on:
  the snapshot is rotated by the panel step in-place before it masks);
  `--reveal <q> [screen]` runs the former concentric-reveal permutation (copied
  verbatim into `main.c`) against the real rotated `lv_refr_now` flush and
  dumped the raw pre-rotation snapshot (`out/snap_raw_qN.png`) that exposed its
  direction bug.
- `host_sim/render_demo.py` — the README montages (PIL + `moon_sim` only).
- Venvs: `host_sim/.venv` (astropy, scikit-image, imagecodecs, numpy, PIL) and
  `assets/images/moon/.venv` — both gitignored; recreate with `uv venv` +
  `uv pip install`.

### 12.3 Device verification

Headless captures drive the state machine through the **Kconfig test hooks**
(all default off, §14.3):

- `TEST_SETTINGS_DEMO_SEC` — the scripted settings run (hub → pomodoro (90 min,
  the cron N>59 sanity) → alarm-time (12:35 Mon-Fri, the range-arc default) →
  light-sleep (store writes) → airplane, reading both SPIFFS files back); the
  settings stack's acceptance path, and the exercise of every screen switch
  (hence of the fade-to-black switch).
- `TEST_AUTO_WAKE_SEC` — fires the idle-wake transition after N s of sleep (the
  wake path: WiFi reconnect, screen switch, moon re-bake).
- `TEST_ROLL_TILT_SEC` — scripted tilt-roll steps (the orientation pipeline).
- `TEST_CPU_STATS_SEC` — idle-CPU stats loop (the 73-91 % root-cause probe;
  caveat: its idle-reset forces the ACTIVE spin, and its 1 s wait counts
  lv_timer ticks, ~510 s real).
- `TEST_BAKE_NO_RELIEF` — relief-free bakes (the PSRAM-load-latency floor A/B:
  bake ~1.3 s with relief vs ~0.95 s without).
- `TEST_SPIN_BANDS` — the spin's seam diagnostic (the direct panel write; one
  distinct gray level per strip, so a glitch at a strip seam — a stray line, a
  shifted boundary, a row that does not land — shows on a clean pattern with no
  moon texture or keep-mask to confound it; the write path is unchanged, only
  the content). `1` = the MOON_PX² moon area as horizontal bands written with
  the normal spin geometry (400-wide strips at x=33 on the S3). `2` = the same
  band pattern written as full-panel-width strips (x=0..465, the LVGL flush
  geometry) — the A/B that isolates the 400-wide strip window as the seam-line
  cause (the black edge margins cover the arc-ring slivers for the duration of
  the test). 0 (default) = the normal moon.
- `TEST_WIFI_SCAN_SEC` — opens the settings WiFi screen N s after boot (waiting
  out the boot sweep and disabling idle monitoring), then holds while the
  background scan completes — the headless scan-path check for the remote-C6
  board (the `esp_hosted` RPC scan through the C6, including 5 GHz APs).

Plus serial capture (pyserial 115200) of the boot/sleep/wake/alarm cycles (0
WDT/panic/NO_MEM is the pass criterion), and the **user visual pass** on the
physical device for the things only a human can judge (the fade-to-black
switch's smoothness, the day-range-arc handle gestures, the roller feel).

---

## 13. ESP32-S3: capabilities & limits

What this project uses, and what the chip/board would not do:

**Used to the edge**

| Capability                          | Use                                                                                                                                                                                     |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 240 MHz dual-core Xtensa            | core 0 = UI/display (LVGL adapter, pinned); core 1 = moon bakes + spin render (bounded, self-cancelling — the core-1 WDT idle check is disabled for it, core 0's stays as the hang net) |
| 8 MB octal PSRAM @ 80 MHz           | frame buffers, shade map, the 4 MB slope cache, LVGL layers (CLIB malloc backend → PSRAM); **not** DMA-accessible                                                                       |
| 16 MB flash                         | 2048 K app (the factory was widened 1392 K → 2048 K when the app outgrew the old ceiling — currently 646,800 B free) + 64 K SPIFFS + 2560 K assets                                      |
| QSPI @ high pclk + 16-bit QSPI data | the 466x466x2 B full-panel flush in ~175 ms (display-bound, not CPU-bound)                                                                                                              |
| FreeRTOS (100 Hz)                   | the 10 ms UI tick, the bounded bake tasks, the background sync task                                                                                                                     |
| SNTP + WiFi (on demand)             | the airplane-modem-sleep power model; the hourly 1-minute sync                                                                                                                          |

**Hard limits the design works around**

| Limit                                              | Consequence in this project                                                                                                                                                                                                                                                                                                      |
| -------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PSRAM is not SPI-DMA-capable (below `SOC_DMA_LOW`) | every display strip is staged into internal RAM; the S3 SPI DMA transaction cap is **32 KiB** → `buffer_height` 25 (23.3 KiB) + flush queue depth 1; a 50-row strip (46.6 KiB) → `ESP_ERR_NO_MEM` on every full-panel flush (the vendored BSP fix)                                                                               |
| 2048 K app partition (widened from 1392 K)         | the flash budget is a standing constraint (current: 646,800 B free); the widening was forced by the app outgrowing 1,425,408 B and is what houses the circular wheel keyboard (the widened SSID font + the screen + the save→connect flow)                                                                                       |
| LVGL 8 KB task stack                               | whole-panel `lv_refr_now` overflows the 3.5 KB main task at boot → the atomic flush must run on the LVGL task                                                                                                                                                                                                                    |
| 64 KiB built-in LVGL pool                          | a full-screen transformed layer (~0.9 MB) OOMs it → CLIB malloc (PSRAM); the original tilt crash                                                                                                                                                                                                                                 |
| Core-1 CPU starvation by long bakes                | the core-1 WDT idle-task check is off (bakes are bounded; core 0's check stays)                                                                                                                                                                                                                                                  |
| No GPU / no hardware 2-D                           | the spin is a CPU fixed-point gather (~8 ms) + a direct panel write; the SW renderer owns everything else; the display, not the CPU, is the bottleneck (~175 ms/frame); LVGL's glTF 3D extension is unusable on this chip (and on the P4) for the same reason — investigated 2026-10-30, see `docs/research/gltf-feasibility.md` |
| FreeRTOS 100 Hz                                    | a 1 ms `vTaskDelay` floor is 0 ticks → the adapter's 1 ms minimum-delay made the LVGL task spin at ~3500 Hz (73-91 % of core 0) — the BSP `task_min_delay_ms = 10` fix (7-9 % now)                                                                                                                                               |
| RTC INT not wired to a GPIO                        | all RTC-driven behavior is software-polled (1.3 s); the PCF85063A ALM persists across boots → the 0-rule disarm                                                                                                                                                                                                                  |
| AMOLED burn-in                                     | the design-level constraint of §8                                                                                                                                                                                                                                                                                                |

**Not used (and why):** the ES8311 audio (no product need), PPA acceleration
(off — the spin's keep-mask snapshot is a SW render), LVGL themes other than
the custom grey/white, LVGL's built-in allocator, RTC interrupts, deep-sleep
modem (the modem sleeps; the MCU stays up — a true deep sleep would lose the
display and the spin's pose, which the wake path needs to resume seamlessly).

---

## 14. Configuration reference

### 14.1 The build defaults (three files; `sdkconfig` is gitignored)

The defaults are split shared / per-target — for each defaults file the build
also appends the `<file>.<idf_target>` variant if it exists (later file wins):

- **`sdkconfig.defaults`** (shared): target esp32s3, 16 MB flash, `SPIRAM=y`,
  the UI Kconfig block (`CONFIG_UI_SCREEN_SWITCH_FADE=240`, the spin periods /
  fps), the LVGL block (`CONFIG_LV_USE_CLIB_MALLOC=y` — PSRAM layers,
  `CONFIG_LV_COLOR_DEPTH_16=y`, `CONFIG_LV_USE_SNAPSHOT=y` — the spin's UI
  keep-mask, `CONFIG_LV_USE_SYSMON=n` / `CONFIG_LV_USE_PERF_MONITOR=n` — dead
  weight removed for flash, `CONFIG_LV_USE_ROLLER=y`), the codecs-off block,
  `CONFIG_IO_EXPANDER_ENABLE_GPIO_API_WRAPPER=n`.
- **`sdkconfig.defaults.esp32s3`** (S3-only, moved out of the shared file):
  octal PSRAM 80 MHz, 240 MHz, `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`
  (the bounded core-1 bakes — §13), `CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH=1` (the
  32 KiB SPI-DMA strip staging — §13).
- **`sdkconfig.defaults.esp32p4`** (P4-only): `CONFIG_IDF_TARGET="esp32p4"`,
  `CONFIG_BOARD_P4=y` (the `BOARD` Kconfig choice, §15), `ESP32P4_REV_MIN_100`
  (rev v1.3 — no `REV_MIN_300`/`SELECTS_REV_LESS_V3` in IDF 5.5.1),
  `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` + `SPIRAM_SPEED_200M` (200 MHz PSRAM —
  gated: without the experimental flag it silently falls back to 20 MHz and the
  DSI underruns), L2 cache 256 KB / 128 B line, 10 KB main task stack, 1000 Hz
  tick, `CONFIG_BSP_LCD_TYPE_720_720_4_INCH=y`. (No esp_hosted / SDIO-pins
  block yet — that lands with the P4 Wi-Fi milestone.)

The `BOARD` choice itself (`BOARD_S3` default / `BOARD_P4`) lives in
`components/board/Kconfig.projbuild` — a root `Kconfig.projbuild` is **not**
processed by ESP-IDF 5.5 (only per-component `Kconfig`/`Kconfig.projbuild`
files are globbed), so the choice sits in the always-built board-shim
component. The root `CMakeLists.txt` `FATAL_ERROR`s if `IDF_TARGET` and the
`BOARD` choice disagree.

### 14.2 UI component (`components/ui/Kconfig`)

| Option                                                                        | Default         | Meaning                                                                                                                                                                                                                                                                                                                             |
| ----------------------------------------------------------------------------- | --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CONFIG_UI_MOON_ANIM_ENABLE`                                                  | y               | the boot synodic sweep (336 frames); `#ifndef` fallback in `moon_ui.c` re-enables it if the symbol is undefined — disable by _setting 0_                                                                                                                                                                                            |
| `CONFIG_UI_MOON_ANIM_FRAMES`                                                  | 336             | sweep length                                                                                                                                                                                                                                                                                                                        |
| `CONFIG_UI_MOON_ANIM_BAKE_PX`                                                 | 240             | sweep bake resolution (the reduced-res fast path)                                                                                                                                                                                                                                                                                   |
| `CONFIG_UI_MOON_ANIM_ON_WAKE`                                                 | n               | (obsolete test hook — the sweep is boot-only)                                                                                                                                                                                                                                                                                       |
| `CONFIG_UI_MOON_DUSK_ENABLE`                                                  | y               | the light-sleep pole spin (constant projection)                                                                                                                                                                                                                                                                                     |
| `CONFIG_UI_MOON_SPIN_ENABLE`                                                  | y               | the free spin (direct panel write via the wrapped `esp_lcd_panel_draw_bitmap`; paused while the orbit re-bake, the boot sweep, or the dusk owns the buffer — the screen-switch fade also holds the spin's panel writes (`s_fade_busy`, the angle keeps integrating) so the 0x51 ramp is the only panel-io traffic in flight, §10.4) |
| `CONFIG_UI_MOON_SPIN_PERIOD_S`                                                | 300             | active spin: seconds per full turn while in use (range 30-7200)                                                                                                                                                                                                                                                                     |
| `CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S`                                        | 600             | settled spin: seconds per full turn at rest (range 60-7200) — the constant slow burn-in spin (the phase-true coast it read as a freeze)                                                                                                                                                                                             |
| `CONFIG_UI_MOON_SPIN_FPS`                                                     | 8               | spin frame rate: gathered + panel-written frames per second (range 2-30)                                                                                                                                                                                                                                                            |
| `CONFIG_UI_MOON_SPIN_SETTLE_S`                                                | 10              | activity→settled-spin window                                                                                                                                                                                                                                                                                                        |
| `CONFIG_UI_MOON_ORBIT_ENABLE`                                                 | n               | tilt-driven viewport orbit (off on the S3 — the relief re-bake cadence doesn't fit; retry on the P4). Kconfig-`n` leaves the macro UNDEFINED — the `#ifndef` fallback in `moon_ui.c` is `#define 0`                                                                                                                                 |
| `CONFIG_UI_MOON_ORBIT_MAX_DEG` / `_STEP_DEG` / `_DEADZONE_DEG` / `_LPF_ALPHA` | 30 / 1 / 2 / 30 | orbit viewport bounds, step, dead zone, low-pass %                                                                                                                                                                                                                                                                                  |
| `CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC`                                          | 60              | deep-sleep countdown (runtime-overridable)                                                                                                                                                                                                                                                                                          |
| `CONFIG_UI_LIGHT_SLEEP_TICK_MS`                                               | 100             | LVGL tick cadence in light sleep                                                                                                                                                                                                                                                                                                    |
| `CONFIG_UI_IMU_ENABLE`                                                        | y               | QMI8658C tilt (drive the clock roll + activity)                                                                                                                                                                                                                                                                                     |
| `CONFIG_UI_IMU_ACTIVITY_DEG` / `_WAKE_DEG`                                    | 5 / 20          | activity bar / wake bar (departure-from-settled model)                                                                                                                                                                                                                                                                              |
| `CONFIG_UI_IMU_ANGLE_SIGN` / `_ROLL_OFFSET_DEG`                               | 1 / 0           | sign/offset calibration for the mount                                                                                                                                                                                                                                                                                               |
| `CONFIG_UI_CLOCK_ROLL_SIGN`                                                   | 1               | flips the tilt→clock-roll sign for a mirrored mount                                                                                                                                                                                                                                                                                 |
| `CONFIG_UI_SCREEN_SWITCH_FADE`                                                | 240             | the fade-to-black screen switch (ms per leg; 0 = the plain atomic switch)                                                                                                                                                                                                                                                           |

### 14.3 Clock component (`components/clock/Kconfig`)

| Option                          | Default                        | Meaning                                                                                                                                                                                                                                                     |
| ------------------------------- | ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CLOCK_USE_PCF85063A`           | `y` (n in the P4 defaults)     | the PCF85063A hardware path (S3). `n` (the P4) = the M5.1 software clock: system-clock time base + software minute tick + `esp_timer` countdown/alarm one-shots (§2.3); `n` leaves the macro UNDEFINED — the code gates on `#if CONFIG_CLOCK_USE_PCF85063A` |
| `CLOCK_TIME_FORMAT`             | 24H                            | display format (the RTC stores 24 h)                                                                                                                                                                                                                        |
| `CLOCK_DEFAULT_TIMEZONE`        | `CET-1CEST,M3.5.0/2,M10.5.0/3` | POSIX TZ                                                                                                                                                                                                                                                    |
| `CLOCK_NTP_SERVER`              | `pool.ntp.org`                 | SNTP server                                                                                                                                                                                                                                                 |
| `CLOCK_NTP_SYNC_INTERVAL_HOURS` | 1                              | the on-demand sync interval                                                                                                                                                                                                                                 |
| `CLOCK_NTP_TIMEOUT_MS`          | 10000                          | SNTP timeout                                                                                                                                                                                                                                                |
| `CLOCK_IDLE_TIMEOUT_SEC`        | 60                             | idle → light sleep (runtime-overridable)                                                                                                                                                                                                                    |

Test hooks (all default off, §12.3): `TEST_SETTINGS_DEMO_SEC`,
`TEST_AUTO_WAKE_SEC`, `TEST_ROLL_TILT_SEC`, `TEST_CPU_STATS_SEC`,
`TEST_BAKE_NO_RELIEF`, `TEST_WIFI_SCAN_SEC`, `TEST_SPIN_BANDS`.

### 14.4 Runtime settings store (`/spiffs/settings.txt`)

`key = value` lines (the live overrides; absent keys fall back to the
Kconfig-seeded defaults; written by the settings screens, read at boot):

```
idle_timeout_sec = 60
deep_sleep_countdown_sec = 60
boot_anim_enable = 1
dusk_anim_enable = 1
```

`/spiffs/alarm.txt` (the cron rules, one per line, `#` comments): `*/N * * * *`
(interval, N ≤ 1440) and `M H * * <dow>` (dow = comma list / `*` = all days;
bit 0 = Sunday). `/spiffs/wifi_credentials.txt` (two lines, gitignored). A full
`idf.py flash` re-bakes the SPIFFS partition from the tree — runtime edits are
lost on flash (the tree keeps the `spiffs/alarm.txt` default — 7:00 Mon–Fri;
`spiffs/settings.txt` must NOT be in the tree — it is runtime-owned).

---

## 15. Project structure

```
CMakeLists.txt               IDF project (managed_components, the target-select:
                             partitions.csv vs partitions_p4.csv, EXCLUDE_COMPONENTS
                             + the per-target dependencies lock for the P4)
dependencies.lock            pinned managed components (S3; the P4 build uses the
                             gitignored dependencies.lock.p4 — different BSP + stacks)
partitions.csv               16 MB layout (§3.1)
partitions_p4.csv            32 MB P4 layout (§3.2)
sdkconfig.defaults           the shared build defaults (sdkconfig is gitignored)
sdkconfig.defaults.esp32s3   the S3-only defaults (octal PSRAM 80 M, 240 MHz, WDT,
sdkconfig.defaults.esp32p4     flush queue depth 1) + the P4 defaults (BOARD_P4,
                               rev1_3, 200 MHz PSRAM, 720×720 LCD type)
README.md                    the front page (build/hardware/demo/timings)
CLAUDE.md                    this project's agent instructions (the docs rule)
main/
  main.cpp                   app_main: boot sync -> display -> UI; test hooks
components/
  board/                     the board shim: board_display.{c,h} (the panel handle,
    Kconfig.projbuild          the S3-only IO expander) + the BOARD Kconfig choice
  moon/                      the portable core (§5): astro/bake/fixp/render + ERFA
    include/moon.h           the public API (geometry/bake/reproject/rotate)
    include/moon_consts.h    generated single source of truth (pole, DEM, earthshine, tone LUT)
    include/moon_assets.h    the asset partition access
  clock/                     RTC/minute/idle/sntp/alarm-scheduler/settings-store
  ui/                        the LVGL UI: screens.c (13 screens) + moon_ui.c
    (the pipeline) + actions.c (flow) + styles.c (the theme) + fonts/ (generated)
  imu/                       QMI8658C driver
  wifi/                      scan (settings)
  esp32_s3_touch_amoled_1_75/  the S3 BSP (CO5300 QSPI + CST9217 + TCA9554 +
                               brightness); the P4 uses the registry
                               waveshare/esp32_p4_wifi6_touch_lcd_xc 3.0.1
host_sim/                    moon_sim + check_gate.py + pole_test.c + uisim/ + render_demo.py
assets/                      LROC/LOLA sources, the Python prototype, the 8,760
                             NASA SVS 5415 frames, the generated device assets
spiffs/                      wifi_credentials.example.txt + alarm.txt (the 7:00 Mon-Fri default)
docs/                        this document + the media (§1)
```

Line counts (tracked, 2026-10-31): moon core ~5,400; ui ~10,100 (incl. the
generated font subsets ~4,100); clock ~3,100; host_sim ~3,500; BSP ~1,200;
board shim ~115; main ~450; wifi ~430; imu ~410 — **~26,400 lines** of tracked
code.

---

## 16. Files of docs/ and CLAUDE.md

`docs/` holds this document, its generator, the media, and the research notes:

DOCS_INVENTORY

The standing rule (full text embedded below) is what keeps this document from
going stale:

CLAUDE_MD

---

## 17. Complete source code

Every tracked source file of the project, verbatim (generated by
`docs/gen_doc.py` from `git ls-files`; regenerated with the document). Vendored
`managed_components/` (LVGL 9.6.0, esp_lcd_co5300, esp_lvgl_adapter, …) and the
gitignored `assets/` data (the NASA series, the LROC/LOLA sources) are excluded
— the managed components are pinned by `dependencies.lock` (embedded below in
the project config group).

CODE
