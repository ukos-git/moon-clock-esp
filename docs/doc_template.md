# ESP32 Moon Phase Clock — Project Documentation

A physically correct moon-phase clock. A 466 px disk of the Moon — the current
phase, from the last new moon to now — is rendered in real time on an AMOLED
panel from LROC Wide-Angle Camera albedo and LOLA relief, lit with
displaced-normal Lambert shading at the exact Sun/Moon/Earth geometry of the
cheap ephemeris (JPL Moon98 + ERFA EPV). The disk is baked on-chip once a day;
the free spin is a fixed-point in-plane re-gather (~40 ms/frame); sleep is
signalled by a pole-axis spin that carries the terminator across the disk to
the fully unilluminated side. Time comes from NTP (brief on-demand WiFi
activation, then back to airplane mode); the PCF85063A RTC holds the clock
between syncs.

_State of this document: 2026-10-30, after the reveal-rotation **direction**
fix (the snapshot is screen-local, and the in-place rotation was 90° CCW
instead of CW — the reveal ghost that showed on the alarm screen) and the gated
2 s press orientation re-sync (stable + on-axis pose only, so a palm released
mid-flip no longer re-anchors the home pose). See the standing rule in
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

- **12 screenshots** of every live screen, rendered by the UI simulator
  (`host_sim/uisim`) from the _real_ device `screens.c` with the vendored LVGL
  9.6 — the exact panel pixels, 466x466 RGB565: `docs/img/screens/*.png`
- **4 films** of the live animations, frames rendered by the device's own moon
  core (`host_sim/build/moon_sim`, the exact device translation units):
  - `docs/img/boot_sweep.mp4` — the boot synodic sweep (336 frames @240 px, new
    moon → now)
  - `docs/img/free_spin.mp4` — the settled free spin (36 frames @466 px, 10°)
  - `docs/img/dusk.mp4` — the pole-axis sleep spin (0° → the fully-dark
    endpoint, 1°/frame)
  - `docs/img/reveal.mp4` — the concentric (center-out) screen-switch reveal
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

No wiring needed — the Waveshare **ESP32-S3 Touch AMOLED 1.75** is a finished
board (16 MB flash variant). Connect USB (the `GPIO19/20` USB-C port) for
flash + serial: `/dev/ttyACM0` @ 115200 on Linux. (If a second board is
attached, the P4 board takes `/dev/ttyACM0` and the S3 moves to `/dev/ttyACM1`
— verify with `esptool.py chip_id`.)

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
(the `*/5` debug rule in `spiffs/alarm.txt` is restored from the tree; user
alarm/settings edits are lost on a full flash).

**Timezone** (optional): `idf.py menuconfig` → _Component config → Clock
Component Configuration → Default timezone_ (POSIX TZ; default
`CET-1CEST,M3.5.0/2,M10.5.0/3`).

### 2.4 Flash + serial

```bash
idf.py flash -p /dev/ttyACM0        # ~40 s at 460800 baud
python3 -m serial.tools.miniterm /dev/ttyACM0 115200   # or pyserial capture
```

**First boot** (≈3.5 min): the clock is acquired by the _blocking_ boot sync
(WiFi + NTP before the display starts), the UI is born on the **airplane** home
screen, and the **synodic boot sweep** plays: 336 frames @240 px from the last
new moon to now (~2.8 min, display-paced ~305 ms/frame, bake ~78 ms), ending on
the current phase; the free spin then re-anchors from the sweep's last frame.
Serial markers to watch: `WiFi connected`, `SNTP sync`,
`synodic boot sweep started: 336 frames`, `sweep N/336 baked in ~79ms @240px`,
`spin started`. A clean boot shows **0 WDT / 0 panic / 0 NO_MEM**.

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

| Name           | Type   | Size                     | Holds                                                                |
| -------------- | ------ | ------------------------ | -------------------------------------------------------------------- |
| nvs / phy_init | data   | 24 K + 4 K               | IDF                                                                  |
| `factory`      | app    | **1392 K** (1,425,408 B) | the firmware — current binary `0x15b280` (1,421,568 B), 3,456 B free |
| `storage`      | spiffs | 64 K                     | `wifi_credentials.txt`, `alarm.txt`, `settings.txt` (~1 KB)          |
| `moon_assets`  | data   | 2560 K                   | 864x432 albedo + 2048x1024 int8 relief DEM                           |

The 2 MB layout in early git history was a config choice, not a hardware limit;
the 2048x1024 relief DEM (2,097,152 B) forced the 16 MB variant.

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
  clock/               PCF85063A RTC, minute/interrupt logic, idle detector,
    clock_sntp.c           NTP sync; clock_alarm_scheduler.c (cron-like alarms,
                           write/reload, */N>59 support); settings_store.c (runtime store)
  ui/                  LVGL 9.6 UI: screens.c (12 screens), moon_ui.c (the moon
    actions.c                pipeline: sweep/spin/dusk/bake tasks), styles.c,
    images.c                 fonts/ (generated lv_font_conv subsets)
  imu/                 QMI8658C driver (I2C), tilt/activity + wake model
  wifi/                ESP WiFi scan (settings screen)
  esp32_s3_touch_amoled_1_75/   the BSP: CO5300 QSPI panel + CST9217 + expander + brightness
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

| Task           | Core | Job                                                                                                                                    |
| -------------- | ---- | -------------------------------------------------------------------------------------------------------------------------------------- |
| LVGL (adapter) | 0    | UI, input, the 10 ms tick (airplane/WiFi), screen switches, atomic whole-panel flushes; 8 KB stack                                     |
| spin render    | 1    | the free spin's fixed-point gather (~40 ms/frame), rate = active period while in use, phase-true coast (1 turn / 29.53 d) when settled |
| moon bakes     | 1    | boot sweep (336 frames), daily re-bake, dusk shade map + final bake, row-by-row so an interrupt is honored                             |
| wifi sync      | 1    | background `wifi_manager_init` + NTP (never on the LVGL task — the wake-freeze bug's fix)                                              |
| main           | 0    | boot, RTC polling (1.3 s), test hooks                                                                                                  |

All moon frames land on the panel as **one atomic whole-panel `lv_refr_now`
under `s_rgb_mtx`** — the moon pipeline's display invariant (the spin render
and the LVGL display are serialized on the frame buffer; no tearing).

### 4.3 The display pipeline

```
moon_bake (core 1, PSRAM 8-bit gray) -> RGB565 convert
   -> s_frame (PSRAM, whole 466x466)
   -> LVGL task: lv_refr_now(NULL) under s_rgb_mtx
   -> adapter bridge: 25-row strips, PSRAM strip -> internal-RAM DMA staging
      (PSRAM is not SPI-DMA-capable; 32 KiB SPI DMA cap)
   -> CO5300 windowed QSPI write (CASET/RASET/RAMWR, x_gap 0x06)
```

`buffer_height = 25` (23.3 KiB strip) and flush queue depth 1 are hard limits
of this board (the 50-row upstream default exhausts the DMA-capable heap). The
screen-switch reveal (`UI_SCREEN_SWITCH_REVEAL`, §10.4) reorders the same
strips center-out; everything else keeps top-to-bottom.

### 4.4 Screen & state flow

```
boot: WiFi+NTP (blocking) -> airplane home + 336-frame synodic sweep -> free spin
touch/tilt wake: static current phase at the parked spin pose (no animation)
idle (runtime timeout, default 60 s): light sleep = the DUSK
   (pole spin, 1 deg/frame, constant projection, shade map baked once)
   -> last frame arms the RTC deep-sleep countdown (default 60 s)
deep sleep: dark side + clock hidden + backlight OFF
alarm (RTC minute poll): alarm screen (until dismissed by touch); fires from deep sleep too
tap on airplane/WiFi: settings hub (concentric icon ring + central exit)
   sub-screens: alarm, pomodoro, alarm-time, light-sleep, deep-sleep, WiFi
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
   v) of the pixel (disk radius 200 px on the 466 px panel);
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
4. **Night side**: on the device, a **constant night floor**
   (`MOON_NIGHT_FLOOR = 10`/255) instead of the phase-dependent earthshine ramp
   — the ~17-step ramp dithered as "moving earthshine" in the fast animation;
   the floor keeps the far side faintly visible on the AMOLED (flat `0x0000`
   was invisible). The static/spin bakes keep the earthshine; the gate +
   `pole_test` run the earthshine path (`dark_zero = 0`);
5. **`dk_sup` dark-side bound (dusk only)**: the pole spin never changes the
   view latitude, so the _endpoint_ view still carries a geometrically lit rim
   sliver (d up to `sqrt(1 - E·S(φ*)²)`; today +0.1216) which would linger on
   the dark sleep screen as a bright arc hugging the disk edge (the
   user-reported "white ray" — the true root cause was the shade map's ±y
   mirror folding mirrored lighting onto the seam columns; fixed by threading
   `dk_sup` through the shade map, the final bake, and the reproject, all
   three). Every non-dusk caller passes `0.0f` (byte-identical to the pre-fix
   code);
6. **Tone LUT**: 256-entry 8-bit → 8-bit curve (generated, in `moon_consts.h`),
   then 8-bit → RGB565.

**View-independence** (the key architectural fact): a pixel's color depends
only on the body-frame surface direction and the epoch's fixed body-frame Sun —
the camera basis selects _which direction is visible_ but never enters the
color. Consequences: the free spin and the boot sweep need no lighting
recompute (fixed-point gather only), and the dusk is a **constant projection**
— one body-frame shaded equirect map (`moon_shade_map_build`, 864x432, PSRAM,
~1.6 s) is re-projected through the folded camera basis per frame (~50 ms,
`moon_reproject`); the final dark-side frame is the one pixel-exact full
`moon_bake` at φ\* = `atan2(-B, -A)` (today +134.78° orbit-folded).

### 5.3 Rotation (the free spin)

`moon_rotate`: a fixed-point nearest-neighbor in-plane gather ("ball in hand"),
int4-quantized 10°-step orientation applied per screen, angle integrated per
frame with the rate smoothed (EMA) toward the target — active period (default
300 s/turn) while in use, **phase-true coast** (one turn per synodic month,
~12°/day) when settled, so the pose tracks the real Moon phase. ~40 ms/frame;
display ~140 ms (display-bound).

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
  uncompressed): 96 px digits (the clock), 48 px (rollers/labels), 40 px
  weekday letters, 32 px SSID/captions; Font Awesome 7 Free (Solid 96 px:
  xmark/gear/moon/wifi/stopwatch; Regular: moon/alarm-clock) — the icon set is
  the same weight-and-color logic as the type.
- **Geometry — concentric circles.** The home screen _is_ the moon: disk center
  = screen center. The settings hub mirrors it: a central concentric exit
  button (150 px grey ring + black inner circle + white xmark) with the
  sub-screen icons on a 45° diagonal ring at radius ~165. Every sub-screen's
  exit is the same button, smaller, top-center. Weekday selection is a pie of 7
  arc wedges (pressable, square-capped, seam-pixel shared); the day range is a
  two-ended range arc with draggable handles.
- **Motion — the moon's own.** The screens are switched with the **same 25-row
  panel strips the hardware uses, ordered center-out** (the concentric reveal,
  §10.4): a new screen blooms from the center like the moon's disk. The base
  +90° orientation is an LVGL root transform, so "screen center" is the panel
  center. Sleep is motion with meaning: the dusk _is_ the terminator
  travelling, not a fade.
- **Interaction — touch-only, gesture-honest.** Taps open; drags on rollers
  (persist only on RELEASED — SPIFFS/I2C stay off the drag path); the day-range
  handles snap; an idle timeout (runtime) dims into light sleep; waking shows
  the _current_ phase statically — the device never re-animates history.

---

## 7. Screens — every live screen with its settings

All 12 screens, exact panel pixels from the UI simulator. The "settings
displayed" column lists what the screen shows and what each control changes
live.

### 7.1 Airplane (modem sleep) — the home screen

![airplane](img/screens/modem_sleep.png)

- The moon disk at the current phase (the free spin animates it), the clock (96
  px HH:MM, two-digit, the live seconds arc around the disk).
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
  1°/frame, shade map baked once ~1.6 s; final frame a pixel-exact bake at φ\*;
  the dark side keeps its faint contrast — night floor + crater rims, flat
  black would vanish on the AMOLED).
- The clock is **dimmed to ~20% opacity** (the panel has no alpha channel —
  opacity is the only "alpha"; the moon image is a sibling and stays
  full-contrast).
- On the last frame the deep-sleep countdown arms (runtime
  `deep_sleep_countdown_sec`, default 60 s).

### 7.4 Deep sleep

![deep sleep](img/screens/deep_sleep.png)

- The dark side at φ\* (the final dusk bake, with its relief-lit rims), **the
  clock hidden**, and the **backlight off** after the countdown — the panel is
  genuinely dark (AMOLED), the RTC keeps counting; an alarm can fire from here
  and restore the backlight. Clock updates are gated while the backlight is off
  (no live minute updates in deep sleep).

### 7.5 Alarm (fired)

![alarm](img/screens/alarm.png)

- Shown when the scheduler fires (RTC-minute-poll; also from deep sleep,
  backlight restored). Touch dismisses and re-arms. The screen carries the
  alarm indicator icon and the time; there is **no automatic sleep transition**
  while it is up (task: the alarm must not dusk-sleep mid- dismiss).

### 7.6 Settings hub

![hub](img/screens/settings_hub.png)

- Central concentric exit (grey ring, black core, white xmark) → exits to the
  calling level (a WiFi reconnect if entered from a fresh wake — the hub's exit
  lands on the WiFi screen and drops to airplane when done).
- Icon ring (45° diagonals, 96 px glyphs, clickable): alarm settings
  (alarm-clock, regular), light sleep (moon, regular), deep sleep (moon, solid
  — the weight distinguishes the two), WiFi (wifi, solid).
- Grey = present/available, white = selected; the hub's ticks refresh icon
  colors after every save.

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

### 7.9 Alarm time (the weekday pie)

![alarm time](img/screens/settings_alarm_time.png)

- **Day range**: a two-ended range arc across the outer radius with two
  draggable handles (PRESS_LOCK; PRESSED/PRESSING snap to the nearest day
  letter; RELEASED saves) — replacing the per-slice toggle model (task #69); 7
  weekday letters (Montserrat 40 px) mirror the arc's selected/unselected
  color.
- **Time**: 96 px HH:MM preview (two-digit, zero-padded, colon) with two 48 px
  rollers (hours, minutes ×5) below; RELEASED saves
  `clock_alarm_set_clock_alarm(h, m, dow_mask)` (reject empty mask) → the cron
  rule `M H * * <dow>`.
- A self-diagnosing **read-back log** prints after every save
  (`read-back: rule_types=0xNN fixed=ok got HH:MM mask 0xNN MATCH|MISMATCH`) so
  a device regression localizes to the write/re-parse layer vs the UI color
  refresh.
- **Settings displayed:** the fixed time (HH:MM), the day mask (the range arc +
  letters), and — via the save log — the parsed rule the scheduler re-loaded.

### 7.10 Light-sleep settings

![light sleep settings](img/screens/settings_light_sleep.png)

- Header moon (regular). One centered 48 px roller: the **idle timeout**
  (10-300 s, default 60) — `RELEASED` → `settings_set_idle_timeout_sec` + live
  `clock_idle_set_timeout_ms()` + store save (takes effect immediately).
- Both **animation toggles** (glyph + upper-cased caption, white = on / grey =
  off; no switch widget): boot animation (`boot_anim_enable`) and dusk
  animation (`dusk_anim_enable`).
- **Settings displayed:** the idle timeout value + both animation flags.

### 7.11 Deep-sleep settings

![deep sleep settings](img/screens/settings_deep_sleep.png)

- Header moon (solid). One centered 48 px roller: the **deep-sleep countdown**
  (10-300 s, default 60) — `RELEASED` →
  `settings_set_deep_sleep_countdown_sec` + save (takes effect at the next dusk
  arming).
- The same two animation toggles.
- **Settings displayed:** the countdown value + both animation flags.

### 7.12 WiFi settings

![wifi settings](img/screens/settings_wifi.png)

- Header (wifi, solid). A **scan list** (background scan task: `esp_wifi_init`
  → STA → blocking scan → `esp_wifi_stop()` — scan only, credentials untouched,
  back to airplane): up to 12 rows, SSID upper-cased (32 px font), grey =
  available / white = selected (a transparent clickable strip per row).
  Selection logs for now — password entry is a deferred follow-up (circular
  keyboard, task #52's note).
- **Settings displayed:** the live AP list from the radio (RSSI-ordered), the
  selection state.

---

## 8. Burn-in

The product runs an always-on AMOLED with a mostly-static disk; burn-in is a
_design requirement_, not an afterthought. The statements, in the order they
were made and implemented:

1. **The lit pixels must move, continuously, with no fixed period.** The free
   spin is the burn-in mechanism: while the device is in use it rotates at the
   active rate (default 300 s/turn); once it settles the rate _coasts to the
   phase-true speed_ — one full turn per synodic month (29.53 d, ~12°/day) — so
   the pose tracks the real moon phase while the lit pixels drift around the
   panel forever. There is deliberately **no fixed-period spin** (the old
   design's fixed 300 s period parked the same pixels in the same places every
   period; the coast eliminates that). ~12°/day × 30 d = a full circle of the
   panel per lunation.
2. **The sleep state must not park the disk either.** The dusk is a
   _constant-projection pole spin_: the terminator (and the lit crescent's
   relief detail) travels the disk for the whole light-sleep duration (14-95 s
   at 1°/frame), and the dark-side frame the countdown lingers on still carries
   its relief-lit crater rims at the terminator — a moving _pattern_ during the
   sleep, a faintly textured (not flat) disk while dark. (The flat-black
   attempt, `dark_zero = 0` → 0x0000, was rejected: the user could not see the
   far side at all, and a flat disk at the terminator is the worst case for a
   permanently parked edge.)
3. **Orientation is stepped, never ramped.** The screen base orientation is
   applied in **90° steps** (int4-quantized IMU roll, 100 ms samples, 1 s
   windowed mean, sustained confirmation, instant apply on the 12 roots) — a
   continuous ramp would have parked the clock's pixels in intermediate
   positions; the steps put every pixel in one of four known poses, and the
   spin + dusk keep moving them between the settle periods.
4. **The screen-switch reveal does not add a static element** — it rewrites the
   panel with the same center-anchored content; the content's motion (the spin)
   is untouched.
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
10° steps; ~140 ms display + ~40 ms gather per frame on-device):

![free spin montage](img/free_spin.png)
<video src="img/free_spin.mp4" controls width="466"></video>

**Dusk** — light-sleep entry: the pole-axis spin, 1°/frame, to the
fully-unilluminated side (the film stops at the detected fully-dark endpoint;
on-device the final frame is a pixel-exact bake at φ\* = +134.78° for the
2026-10-29 epoch, orbit-folded):

![dusk montage](img/dusk.png)
<video src="img/dusk.mp4" controls width="466"></video>

**Concentric screen-switch reveal** — airplane → settings hub: the same 25- row
QSPI strips the adapter writes, ordered from the vertical center outward
(`UI_SCREEN_SWITCH_REVEAL`, `9dad4fe`). Both frames are rotated +90° CW first —
the device's orientation pipeline applies the base orientation to every screen
root, and the reveal writes the rotated pixels to the panel:

<video src="img/reveal.mp4" controls width="466"></video>

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
  root's ~0.4 MB layer is allocated via CLIB malloc (the 64 KB LVGL pool OOM
  was the original "off-screen + crash").
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

### 10.4 The concentric screen-switch reveal (`9dad4fe`, rotation

direction fix 2026-10-30)

`CONFIG_UI_SCREEN_SWITCH_REVEAL` (default y, selects `LV_USE_SNAPSHOT`): on a
switch **to a moon-less screen** (the settings direction), the new screen is
rendered once into a full-screen RGB565 snapshot via `lv_snapshot_take`,
byte-swapped to the panel's native RGB565 (the adapter bridge does this swap
for its own flushes on the OTHER interface), **rotated in place by the root's
current orientation step**, and written to the panel in the same 25- row
windows (the BSP `buffer_height` budget) **ordered by strip-center distance
from the vertical middle** — 19 strips, center out. The trailing atomic
`lv_refr_now` then re-displays identical pixels and drains LVGL's pending
invalidation (the FADE_IN bug class must not return). Any failure (NULL
snapshot, odd dims/stride, NULL panel, non-90°-multiple transform, write error)
falls back to the plain atomic flush. Moon screens keep the atomic whole-panel
flush alone. The adapter's TEAR_AVOID_NONE panel-IO completion callback (which
fires on every direct QSPI transfer) only clears an already-cleared `flushing`
flag — verified harmless. No speed change: the same bytes reach the panel, only
the light-up order differs.

**The rotation (the 2026-10-30 fix).** The first version assumed
`lv_snapshot_take` renders through the root's LVGL transform — it does **not**:
it renders the screen in its _local_ coordinates (proven by the host probe
`uisim --rottest`: with a +90° root rotation the snapshot's header icon stayed
top-centre). The panel, meanwhile, holds the content rotated by the orientation
pipeline's step (base +90° CW mount offset included). The reveal therefore
wrote _unrotated_ content center-out — a 90°-off bloom that read as
"intermediate lines". The fix: `reveal_rotate_cw90` rotates the 466×466 buffer
in place by `lv_obj_get_style_transform_rotation(root) / 900`, taken modulo
3600 (90° CW = transpose then reverse each row; 180° = reverse the array; 270°
CW = reverse each row then transpose).

**The direction bug (caught 2026-10-30).** The case bodies of that fix were
swapped: "90° CW" actually computed 90° **CCW**. At the base orientation the
reveal therefore wrote the previous screen as a full panel-sized ghost at the
wrong orientation on top of the new one — the "draw artifacts" the user saw on
the alarm screen (which the `*/5` debug rule visits every 5 minutes). The 3×3
matrix check (transpose+row-reverse maps the top row to the right column)
pinned it; the host probe showed it directly: pre-fix, `--reveal 1` on the
alarm screen differed from the real rotated flush in 21,270/217,156 px (a whole
mis-oriented copy of the content); post-fix the hub differs in 5,730/
6,783/5,593 px (q=1/2/3) and the alarm in 2,947 px — confined to glyph AA
edges, every solid area byte-exact. The device's own transform render
(`lv_draw_sw_transform.c`) re-samples those edges, and the trailing
`lv_refr_now` replaces them with the exact transform render a moment later. At
q=0 (rotation 0) the diff is 0/217,156. So the bloom shows the right pixels in
the right place from strip one.

### 10.5 Alarms, settings store, cron

- **Alarm scheduler**: cron-like rules (`*/N * * * *` intervals incl. N > 59
  via a total-minutes grid across the day with `mktime` date rollover;
  `M H * * <dow>` fixed times), written to `/spiffs/alarm.txt` and
  re-parsed+re-scheduled on every write (`clock_alarm_scheduler_ reload`; 0
  rules → `clock_clear_alarm()` disarms the PCF85063A's persistent ALM — a
  stale armed time would otherwise fire a phantom alarm screen on a 0-rule
  boot). Firing is the 1.3 s RTC minute poll.
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

| Stage                                         | Value                                                                                                                                                                                           |
| --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Relief gradient cache (boot, 4,096 KB packed) | **2,913 ms** once                                                                                                                                                                               |
| Boot sweep                                    | 336 frames @240 px; bake **~78-82 ms**, display **~305 ms**/frame → **~2.8 min** total                                                                                                          |
| Free spin                                     | **~140 ms** display (display-bound) + **~40 ms** gather/render per frame; 0 WDT resets                                                                                                          |
| Dusk                                          | shade-map build **~1.6 s** once (abortable) + re-projected frames **~50 ms** each (1°/frame; 14-95 s to φ\*) + final full-res bake + 30 s default countdown                                     |
| Wake                                          | static current phase at the parked pose (no animation); background WiFi reconnect (no display freeze)                                                                                           |
| Screen switch (moon)                          | one atomic whole-panel flush, ~175 ms of core-0 work (display-bound)                                                                                                                            |
| Screen switch (settings, reveal)              | snapshot render + 19 center-out strips + trailing identical atomic flush                                                                                                                        |
| Settings hub idle CPU                         | **7-9 % of one core** (after the 10 ms adapter `task_min_delay_ms` fix; the old 1 ms floor made `vTaskDelay(0)` a yield and the LVGL task spun ~3500 Hz at 73-91 % of core 0), UI loop 93-95 Hz |
| Image gate (host)                             | C vs NASA: max MAE **8.90** ≤ 12, SSIM **0.908**; C vs Python: max **1.31** ≤ 2 (32-frame set)                                                                                                  |
| Binary                                        | `0x15b280` = 1,421,568 B in the 1,425,408 B partition (**3,456 B free**)                                                                                                                        |

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
  gray).
- `host_sim/uisim/` — the **real device `screens.c`** (unmodified) on host LVGL
  9.6 (SW renderer, RGB565, 466x466, ESP stubs) → per-screen PNG snapshots
  (`build.sh run`) — the source of every screenshot in §7. Two diagnostic
  probes (2026-10-30): `--rottest` proves `lv_snapshot_take` renders in the
  screen's **local** coordinates (the root's LVGL rotation is ignored — the
  finding behind the reveal's rotation fix, §10.4); `--reveal <q> [screen]`
  runs the device's exact reveal permutation (copied verbatim into `main.c`)
  and compares it pixel-by-pixel against the real rotated `lv_refr_now` flush
  of the same screen (default the settings hub; the alarm screen is the
  artifact repro) — q=0 gives 0/217156 differing pixels; q=1..3 differ only at
  glyph anti-aliasing edges (LVGL's transform re-samples them; the trailing
  flush corrects them). The probe also dumps the raw pre-rotation snapshot
  (`out/snap_raw_qN.png`) — that image exposed the direction bug: the rotated
  snapshot held a full mis-oriented ghost of the previous screen.
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
  (hence of the concentric reveal).
- `TEST_AUTO_WAKE_SEC` — fires the idle-wake transition after N s of sleep (the
  wake path: WiFi reconnect, screen switch, moon re-bake).
- `TEST_ROLL_TILT_SEC` — scripted tilt-roll steps (the orientation pipeline).
- `TEST_CPU_STATS_SEC` — idle-CPU stats loop (the 73-91 % root-cause probe;
  caveat: its idle-reset forces the ACTIVE spin, and its 1 s wait counts
  lv_timer ticks, ~510 s real).
- `TEST_BAKE_NO_RELIEF` — relief-free bakes (the PSRAM-load-latency floor A/B:
  bake ~1.3 s with relief vs ~0.95 s without).

Plus serial capture (pyserial 115200) of the boot/sleep/wake/alarm cycles (0
WDT/panic/NO_MEM is the pass criterion), and the **user visual pass** on the
physical device for the things only a human can judge (the reveal's bloom, the
weekday-pie gestures, the roller feel).

---

## 13. ESP32-S3: capabilities & limits

What this project uses, and what the chip/board would not do:

**Used to the edge**

| Capability                          | Use                                                                                                                                                                                     |
| ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 240 MHz dual-core Xtensa            | core 0 = UI/display (LVGL adapter, pinned); core 1 = moon bakes + spin render (bounded, self-cancelling — the core-1 WDT idle check is disabled for it, core 0's stays as the hang net) |
| 8 MB octal PSRAM @ 80 MHz           | frame buffers, shade map, the 4 MB slope cache, LVGL layers (CLIB malloc backend → PSRAM); **not** DMA-accessible                                                                       |
| 16 MB flash                         | 1392 K app (3,456 B free — the app sits at 99.7 % of its partition; every feature costs bytes, the LVGL monitor removal bought the reveal's ~5 KiB) + 64 K SPIFFS + 2560 K assets       |
| QSPI @ high pclk + 16-bit QSPI data | the 466x466x2 B full-panel flush in ~175 ms (display-bound, not CPU-bound)                                                                                                              |
| FreeRTOS (100 Hz)                   | the 10 ms UI tick, the bounded bake tasks, the background sync task                                                                                                                     |
| SNTP + WiFi (on demand)             | the airplane-modem-sleep power model; the hourly 1-minute sync                                                                                                                          |

**Hard limits the design works around**

| Limit                                              | Consequence in this project                                                                                                                                                                                                                                                                                                              |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PSRAM is not SPI-DMA-capable (below `SOC_DMA_LOW`) | every display strip is staged into internal RAM; the S3 SPI DMA transaction cap is **32 KiB** → `buffer_height` 25 (23.3 KiB) + flush queue depth 1; a 50-row strip (46.6 KiB) → `ESP_ERR_NO_MEM` on every full-panel flush (the vendored BSP fix)                                                                                       |
| 1392 K app partition                               | the flash budget is a standing constraint (current: 3,456 B free); feature landings must reclaim (the monitor removal) or defer                                                                                                                                                                                                          |
| LVGL 8 KB task stack                               | whole-panel `lv_refr_now` overflows the 3.5 KB main task at boot → the atomic flush must run on the LVGL task                                                                                                                                                                                                                            |
| 64 KiB built-in LVGL pool                          | a full-screen transformed layer (~0.9 MB) OOMs it → CLIB malloc (PSRAM); the original tilt crash                                                                                                                                                                                                                                         |
| Core-1 CPU starvation by long bakes                | the core-1 WDT idle-task check is off (bakes are bounded; core 0's check stays)                                                                                                                                                                                                                                                          |
| No GPU / no hardware 2-D                           | the spin is a CPU fixed-point gather (~40 ms), the reveal snapshot is the SW renderer — both fit, neither is fast; the display, not the CPU, is the bottleneck (~175 ms/frame); LVGL's glTF 3D extension is unusable on this chip (and on the P4) for the same reason — investigated 2026-10-30, see `docs/research/gltf-feasibility.md` |
| FreeRTOS 100 Hz                                    | a 1 ms `vTaskDelay` floor is 0 ticks → the adapter's 1 ms minimum-delay made the LVGL task spin at ~3500 Hz (73-91 % of core 0) — the BSP `task_min_delay_ms = 10` fix (7-9 % now)                                                                                                                                                       |
| RTC INT not wired to a GPIO                        | all RTC-driven behavior is software-polled (1.3 s); the PCF85063A ALM persists across boots → the 0-rule disarm                                                                                                                                                                                                                          |
| AMOLED burn-in                                     | the design-level constraint of §8                                                                                                                                                                                                                                                                                                        |

**Not used (and why):** the ES8311 audio (no product need), PPA acceleration
(off — the reveal/snapshot paths don't need it), LVGL themes other than the
custom grey/white, LVGL's built-in allocator, RTC interrupts, deep-sleep modem
(the modem sleeps; the MCU stays up — a true deep sleep would lose the display
and the spin's pose, which the wake path needs to resume seamlessly).

---

## 14. Configuration reference

### 14.1 `sdkconfig.defaults` (the build defaults; `sdkconfig` is gitignored)

Highlights (full file in §17): target esp32s3, 16 MB flash, octal PSRAM 80 MHz,
240 MHz, `CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH=1`, `CONFIG_LV_USE_CLIB_MALLOC=y`
(PSRAM layers), `CONFIG_LV_COLOR_DEPTH_16=y`, `CONFIG_LV_USE_SNAPSHOT=y` (the
reveal), `CONFIG_LV_USE_SYSMON=n` / `CONFIG_LV_USE_PERF_MONITOR=n` (dead
weight, removed for flash), `CONFIG_LV_USE_ROLLER=y`,
`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`.

### 14.2 UI component (`components/ui/Kconfig`)

| Option                                                                        | Default         | Meaning                                                                                                                                                                                             |
| ----------------------------------------------------------------------------- | --------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CONFIG_UI_MOON_ANIM_ENABLE`                                                  | y               | the boot synodic sweep (336 frames); `#ifndef` fallback in `moon_ui.c` re-enables it if the symbol is undefined — disable by _setting 0_                                                            |
| `CONFIG_UI_MOON_ANIM_FRAMES`                                                  | 336             | sweep length                                                                                                                                                                                        |
| `CONFIG_UI_MOON_ANIM_BAKE_PX`                                                 | 240             | sweep bake resolution (the reduced-res fast path)                                                                                                                                                   |
| `CONFIG_UI_MOON_ANIM_ON_WAKE`                                                 | n               | (obsolete test hook — the sweep is boot-only)                                                                                                                                                       |
| `CONFIG_UI_MOON_DUSK_ENABLE`                                                  | y               | the light-sleep pole spin (constant projection)                                                                                                                                                     |
| `CONFIG_UI_MOON_SPIN_ENABLE`                                                  | y               | the free spin (phase-anchored)                                                                                                                                                                      |
| `CONFIG_UI_MOON_SPIN_PERIOD_S`                                                | 300             | active spin: seconds per full turn while in use (range 30-7200); settled → phase-true coast (1 turn / synodic month)                                                                                |
| `CONFIG_UI_MOON_SPIN_SETTLE_S`                                                | 10              | activity→coast settle window                                                                                                                                                                        |
| `CONFIG_UI_MOON_ORBIT_ENABLE`                                                 | n               | tilt-driven viewport orbit (off on the S3 — the relief re-bake cadence doesn't fit; retry on the P4). Kconfig-`n` leaves the macro UNDEFINED — the `#ifndef` fallback in `moon_ui.c` is `#define 0` |
| `CONFIG_UI_MOON_ORBIT_MAX_DEG` / `_STEP_DEG` / `_DEADZONE_DEG` / `_LPF_ALPHA` | 30 / 1 / 2 / 30 | orbit viewport bounds, step, dead zone, low-pass %                                                                                                                                                  |
| `CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC`                                          | 60              | deep-sleep countdown (runtime-overridable)                                                                                                                                                          |
| `CONFIG_UI_LIGHT_SLEEP_TICK_MS`                                               | 100             | LVGL tick cadence in light sleep                                                                                                                                                                    |
| `CONFIG_UI_IMU_ENABLE`                                                        | y               | QMI8658C tilt (drive the clock roll + activity)                                                                                                                                                     |
| `CONFIG_UI_IMU_ACTIVITY_DEG` / `_WAKE_DEG`                                    | 5 / 20          | activity bar / wake bar (departure-from-settled model)                                                                                                                                              |
| `CONFIG_UI_IMU_ANGLE_SIGN` / `_ROLL_OFFSET_DEG`                               | 1 / 0           | sign/offset calibration for the mount                                                                                                                                                               |
| `CONFIG_UI_CLOCK_ROLL_SIGN`                                                   | 1               | flips the tilt→clock-roll sign for a mirrored mount                                                                                                                                                 |
| `CONFIG_UI_SCREEN_SWITCH_REVEAL`                                              | y               | the concentric (center-out) screen-switch reveal (selects `LV_USE_SNAPSHOT`)                                                                                                                        |

### 14.3 Clock component (`components/clock/Kconfig`)

| Option                          | Default                        | Meaning                                  |
| ------------------------------- | ------------------------------ | ---------------------------------------- |
| `CLOCK_TIME_FORMAT`             | 24H                            | display format (the RTC stores 24 h)     |
| `CLOCK_DEFAULT_TIMEZONE`        | `CET-1CEST,M3.5.0/2,M10.5.0/3` | POSIX TZ                                 |
| `CLOCK_NTP_SERVER`              | `pool.ntp.org`                 | SNTP server                              |
| `CLOCK_NTP_SYNC_INTERVAL_HOURS` | 1                              | the on-demand sync interval              |
| `CLOCK_NTP_TIMEOUT_MS`          | 10000                          | SNTP timeout                             |
| `CLOCK_IDLE_TIMEOUT_SEC`        | 60                             | idle → light sleep (runtime-overridable) |

Test hooks (all default off, §12.3): `TEST_SETTINGS_DEMO_SEC`,
`TEST_AUTO_WAKE_SEC`, `TEST_ROLL_TILT_SEC`, `TEST_CPU_STATS_SEC`,
`TEST_BAKE_NO_RELIEF`.

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
lost on flash (the tree keeps the `*/5` debug rule; `spiffs/settings.txt` must
NOT be in the tree — it is runtime-owned).

---

## 15. Project structure

```
CMakeLists.txt               IDF project (managed_components, partitions.csv)
dependencies.lock            pinned managed components
partitions.csv               16 MB layout (§3.1)
sdkconfig.defaults           the build defaults (sdkconfig is gitignored)
README.md                    the front page (build/hardware/demo/timings)
CLAUDE.md                    this project's agent instructions (the docs rule)
main/
  main.cpp                   app_main: boot sync -> display -> UI; test hooks
components/
  moon/                      the portable core (§5): astro/bake/fixp/render + ERFA
    include/moon.h           the public API (geometry/bake/reproject/rotate)
    include/moon_consts.h    generated single source of truth (pole, DEM, earthshine, tone LUT, night floor)
    include/moon_assets.h    the asset partition access
  clock/                     RTC/minute/idle/sntp/alarm-scheduler/settings-store
  ui/                        the LVGL UI: screens.c (12 screens) + moon_ui.c
    (the pipeline) + actions.c (flow) + styles.c (the theme) + fonts/ (generated)
  imu/                       QMI8658C driver
  wifi/                      scan (settings)
  esp32_s3_touch_amoled_1_75/  the BSP (CO5300 QSPI + CST9217 + TCA9554 + brightness)
host_sim/                    moon_sim + check_gate.py + pole_test.c + uisim/ + render_demo.py
assets/                      LROC/LOLA sources, the Python prototype, the 8,760
                             NASA SVS 5415 frames, the generated device assets
spiffs/                      wifi_credentials.example.txt + alarm.txt (*/5 debug rule)
docs/                        this document + the media (§1)
```

Line counts (tracked, 2026-10-29): moon core ~5,400; ui ~10,100 (incl. the
generated font subsets ~4,100); clock ~3,100; host_sim ~3,500; BSP ~1,200; main
~450; wifi ~430; imu ~410 — **~24,600 lines** of tracked code.

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
