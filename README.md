# ESP32 Moon Phase Clock

A physically correct moon-phase clock. A 466 px disk of the moon -- the current
phase, from the last new moon to now -- is rendered in real time on an AMOLED
panel from LROC Wide-Angle Camera albedo and LOLA relief, lit with
displaced-normal Lambert shading at the exact Sun/Moon/Earth geometry of the
cheap ephemeris (JPL Moon98 + ERFA EPV). The disk is baked on-chip every day,
the free spin is a fixed-point in-plane re-gather (~8 ms/frame) written
straight to the panel, and sleep is signalled by a pole-axis spin that carries
the terminator across the disk to the fully unilluminated side. Time comes from
NTP (brief hourly WiFi activation, then back to airplane mode); the PCF85063A
RTC holds the clock between syncs.

The product runs on two Waveshare boards from one tree: the **ESP32-S3 Touch
AMOLED 1.75** (466x466 QSPI AMOLED, the default target) and the **ESP32-P4
WIFI6 Touch LCD XC "4C"** (720x720 round MIPI-DSI IPS, remote C6 Wi-Fi over
SDIO, ported as a full sibling). The complete documentation -- the science, the
implementation, the build system, the per-board references, and the per-screen
UI reference with both-panel screenshots -- is the Sphinx site under `docs/`
(build it with `docs/.venv/bin/sphinx-build -b html docs docs/_build/html`;
code links resolve to the locally generated source pages).

## Build

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py flash
```

- **WiFi credentials** (before the first `idf.py flash`): two lines, SSID then
  password, in `spiffs/wifi_credentials.txt` (the template
  `spiffs/wifi_credentials.example.txt` is the format). The SPIFFS partition is
  re-baked from `spiffs/` on every build.
- **Timezone:** `idf.py menuconfig` -> Component config -> Clock Component
  Configuration -> Default timezone. Default: `CET-1CEST,M3.5.0/2,M10.5.0/3`.
- **Albedo/relief assets** (already in the tree, pre-generated): the
  `moon_assets` partition holds the 864x432 equirect albedo + 2048x1024 int8
  relief DEM. Regenerate with
  `assets/images/moon/.venv/bin/python -m moon.assets` (LROC + LOLA sources).

## Hardware

Waveshare **ESP32-S3 Touch AMOLED 1.75**, with the 16 MB flash variant:

| Block      | Part                                 |
| ---------- | ------------------------------------ |
| Display    | 466x466 AMOLED, RGB565               |
| Display IF | CO5300, QSPI (GPIO4-7, 38)           |
| Touch      | CST9217 (I2C, GPIO14/15/11)          |
| IMU        | QMI8658C (I2C)                       |
| RTC        | PCF85063A behind TCA9554 IO expander |
| Flash      | 16 MB                                |

Flash layout (`partitions.csv`): fixed 80 K (bootloader + table + NVS + PHY),
`factory` 2048 K (measured app 1.44 MB, ~572 KiB margin -- widened from the old
1392 K ceiling by the circular keyboard's widened font), `storage` 64 K SPIFFS,
`moon_assets` 2560 K (albedo + DEM). The P4 4C uses `partitions_p4.csv` (32 MB
chip: `factory` 9216 K, `moon_assets` 5120 K with the 2048-wide per-board
albedo).

## Moon rendering

Per pixel of the disk the bake does: equirect gather of the 864x432 albedo
through the per-frame view basis, displaced-normal (normal from the int8 DEM
gradient) Lambert against the body-frame Sun direction, earthshine ramp for the
night side, and the 8-bit -> RGB565 tone LUT. One full-disk bake at the 2048 px
DEM takes ~1.3 s on the S3; the daily re-bake is the only lighting pass --
everything else (spin, tilt, the dusk) re-projects the baked result with a
fixed-point gather.

`host_sim/moon_sim` builds the **exact device core** on the host and writes
PNGs at the real panel size (`--size 466`); `host_sim/check_gate.py`
image-diffs it against NASA reference images.

## Animations

Rendered by `host_sim/render_demo.py` (PIL + `moon_sim` are the only deps):
`cmake --build host_sim/build && python3 host_sim/render_demo.py --phi-star <deg>`.
Each cell is a downscaled exact panel frame; the `--pole` flag of `moon_sim` is
a 1:1 copy of the device's pole spin (`moon_ui.c moon_pole_geometry`).

**Boot sweep** -- the synodic sweep from the last new moon to now (336 frames
on the device, 12 shown here):

![Boot sweep](docs/img/boot_sweep.png)

**Free spin** -- the settled animation: the disk rotates in-plane at the
current phase (8 rotations at 45 deg shown):

![Free spin](docs/img/free_spin.png)

**Dusk** -- light-sleep entry: a pole-axis spin, 1 deg/frame, until the fully
unilluminated side faces the viewer (phi\* = 139 deg for the demo epoch; the
final frame keeps its earthshine + crater-rim detail, not a flat black disk):

![Dusk](docs/img/dusk.png)

## Measured timings (device)

| Stage      | Value                                                                                                              |
| ---------- | ------------------------------------------------------------------------------------------------------------------ |
| Boot sweep | 336 frames @ 240 px reduced-res bake, ~190 s (display-paced)                                                       |
| Free spin  | ~8 ms gather + direct panel write per frame (8 fps, settled 600 s/turn), 0 WDT resets                              |
| Dusk       | shade-map build 1598 ms once + 156 re-projected frames in 90.7 s + final full-res bake + 30 s deep-sleep countdown |
| Wake       | static current phase (no animation); hourly NTP re-sync on demand                                                  |

## Development

Before pushing, please manually run

```
idf.py save-defconfig
idf.py clang-check --exit-code --run-clang-tidy-options="-header-filter=''" main/main.cpp
```

### Test hooks

Kconfig switches for headless verification (all default off):
`TEST_SETTINGS_DEMO_SEC` (scripted settings run after boot),
`TEST_AUTO_WAKE_SEC`, `TEST_BAKE_NO_RELIEF`, and more -- the full list is the
Kconfig table in the [documentation](docs/reference/kconfig-ui.rst).

## Hardware Differences Affecting RTC-Driven Clocks

This clock component was tested on multiple Waveshare Touch-AMOLED boards.
However, **RTC interrupt and wake capabilities differ by board revision** due
to hardware wiring.

### ESP32-S3 Touch AMOLED 1.75

The external RTC (**PCF85063A**) is **not connected to any ESP32 GPIO**. RTC
interrupt (`RTC_INT`) is routed to the **AXP PMIC (EXIO3)** only, and AXP
interrupt (`AXP_IRQ`) is **not wired to the ESP32**.

**Consequences:** ❌ RTC alarms and timers **cannot generate interrupts**, ❌
RTC cannot wake the ESP32 from sleep, ❌ Minute-tick updates cannot be
interrupt-driven.

**Recommended usage:** Use the RTC **only as a time source**. Perform
**minute-aligned software updates** (e.g. via `esp_timer`). Do not rely on RTC
wake or alarm interrupts.

This board is designed for **continuous operation with display power
management**, not RTC-driven wake.

---

GPIO Pins from the
[Data Sheet](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf):

| ESP32-S3 | LCD       | USB   | RTC     | SD   | ES8311     | QMI8658C  | SYS     | GPS        | OUT    |
| -------- | --------- | ----- | ------- | ---- | ---------- | --------- | ------- | ---------- | ------ |
| GPIO01   |           |       |         | MOSI |            |           |         |            |        |
| GPIO02   |           |       |         | SCK  |            |           |         |            |        |
| GPIO03   |           |       |         | MISO |            |           |         |            |        |
| GPIO04   | QSPI SIO0 |       |         |      |            |           |         |            |        |
| GPIO05   | QSPI SI1  |       |         |      |            |           |         |            |        |
| GPIO06   | QSPI SI2  |       |         |      |            |           |         |            |        |
| GPIO07   | QSPI SI3  |       |         |      |            |           |         |            |        |
| GPIO08   |           |       |         |      | I2S DSDIN  |           |         |            |        |
| GPIO09   |           |       |         |      | I2S SCLK   |           |         |            |        |
| GPIO10   |           |       |         |      | I2S ASDOUT |           |         |            |        |
| GPIO11   | TP INT    |       |         |      |            |           |         |            |        |
| GPIO12   | LCD CS    |       |         |      |            |           |         |            |        |
| GPIO13   | LCD TE    |       |         |      |            |           |         |            |        |
| GPIO14   | TP SCL    |       | RTC SCL |      |            | ESP32 SCL |         | ESP32 SCL  |        |
| GPIO15   | TP SDA    |       | RTC SDA |      |            | ESP32 SDA |         | ESP32 SDA  |        |
| GPIO16   |           |       |         |      |            |           |         |            | GPIO16 |
| GPIO17   |           |       |         |      |            |           |         | NC/GPS RXD | GPIO17 |
| GPIO18   |           |       |         |      |            |           |         | NC/GPS TXD | GPIO18 |
| GPIO19   |           | USB N |         |      |            |           |         |            |        |
| GPIO20   |           | USB P |         |      |            |           |         |            |        |
| GPIO21   |           |       |         |      |            | QMI INT2  |         |            |        |
| GPIO38   | QSPI SCL  |       |         |      |            |           |         |            |        |
| GPIO39   | LCD RESET |       |         |      |            |           |         |            |        |
| GPIO40   | TP RESET  |       |         |      |            |           |         |            |        |
| GPIO41   |           |       |         | SDCS |            |           |         |            |        |
| GPIO42   |           |       |         |      | I2S MCLK   |           |         |            |        |
| GPIO43   |           |       |         |      |            |           |         |            | U0TXD  |
| GPIO44   |           |       |         |      |            |           |         |            | U0RXD  |
| GPIO45   |           |       |         |      | I2S LRCK   |           |         |            |        |
| GPIO46   |           |       |         |      | PA CTRL    |           |         |            |        |
| EXIO0    |           |       |         |      |            |           |         |            |        |
| EXIO1    |           |       |         |      |            |           |         |            |        |
| EXIO2    |           |       |         |      |            |           |         |            |        |
| EXIO3    |           |       | RTC INT |      |            |           |         |            |        |
| EXIO4    |           |       |         |      |            |           | SYS OUT |            |        |
| EXIO5    |           |       |         |      |            |           | AXP IRQ |            |        |
| EXIO6    |           |       |         |      |            | QMI INT1  |         |            |        |
| EXIO7    |           |       |         |      |            |           | GPS RST |            |        |

### RP2350 Touch AMOLED 2.41

The RTC interrupt is **directly wired to the MCU**. `RTC_INT` is connected to
**GPIO8**.

**Consequences:** ✅ RTC alarms and timers **can generate interrupts**, ✅
Minute-tick updates can be interrupt-driven, ✅ MCU wake from RTC events is
possible.

**Recommended usage:** Use RTC interrupts for **minute-aligned UI updates**.
Optional low-power sleep strategies using RTC wake.

This board supports a **traditional alarm-clock style architecture**.

---

GPIO Pins from the
[Data Sheet](https://files.waveshare.com/wiki/RP2350-Touch-AMOLED-2.41/RP2350-Touch-AMOLED-2.41.pdf):

| GPIO   | LCD        | UART | RTC     | SD_Card | IMU      | BAT         | Other  |
| ------ | ---------- | ---- | ------- | ------- | -------- | ----------- | ------ |
| GPIO0  |            | TXD  |         |         |          |             | TXD    |
| GPIO1  |            | RXD  |         |         |          |             | RXD    |
| GPIO2  |            |      |         |         |          |             | GPIO2  |
| GPIO3  |            |      |         |         | IMU INT2 |             |        |
| GPIO4  | TP INT     |      |         |         |          |             |        |
| GPIO5  | TP RESET   |      |         |         |          |             |        |
| GPIO6  | TP SDA     |      | RTC SDA |         | IMU SDA  |             | SDA    |
| GPIO7  | TP SCL     |      | RTC SCL |         | IMU SCL  |             | SCL    |
| GPIO8  |            |      | RTC INT |         | IMU INT1 |             |        |
| GPIO9  | OLED CS    |      |         |         |          |             |        |
| GPIO10 | OLED CLK   |      |         |         |          |             |        |
| GPIO11 | OLED SIO0  |      |         |         |          |             |        |
| GPIO12 | OLED SI1   |      |         |         |          |             |        |
| GPIO13 | OLED SI2   |      |         |         |          |             |        |
| GPIO14 | OLED SI3   |      |         |         |          |             |        |
| GPIO15 | OLED RESET |      |         |         |          |             |        |
| GPIO16 | OLED TE    |      |         |         |          |             |        |
| GPIO17 | PWR EN     |      |         |         |          |             |        |
| GPIO18 |            |      |         | SD SCLK |          |             |        |
| GPIO19 |            |      |         | SD MOSI |          |             |        |
| GPIO20 |            |      |         | SD MISO |          |             |        |
| GPIO21 |            |      |         | SDIO D1 |          |             |        |
| GPIO22 |            |      |         | SDIO D2 |          |             |        |
| GPIO23 |            |      |         | SD CS   |          |             |        |
| GPIO24 |            |      |         |         |          | KEY BAT     |        |
| GPIO25 |            |      |         |         |          | BAT Control |        |
| GPIO26 |            |      |         |         |          |             | GPIO26 |
| GPIO27 |            |      |         |         |          |             | GPIO27 |
| GPIO28 |            |      |         |         |          |             | GPIO28 |
| GPIO29 |            |      |         |         |          |             | GPIO29 |
| GPIO30 |            |      |         |         |          |             | GPIO30 |
| GPIO31 |            |      |         |         |          |             | GPIO31 |
| GPIO32 |            |      |         |         |          |             | GPIO32 |
| GPIO33 |            |      |         |         |          |             | GPIO33 |
| GPIO34 |            |      |         |         |          |             | GPIO34 |
| GPIO35 |            |      |         |         |          |             | GPIO35 |
| GPIO36 |            |      |         |         |          |             | GPIO36 |
| GPIO37 |            |      |         |         |          |             | GPIO37 |
| GPIO38 |            |      |         |         |          |             | GPIO38 |
| GPIO39 |            |      |         |         |          |             | GPIO39 |
| GPIO40 |            |      |         |         |          | BAT ADC     |        |
| GPIO41 |            |      |         |         |          |             | GPIO41 |
| GPIO42 |            |      |         |         |          |             | GPIO42 |
| GPIO43 |            |      |         |         |          |             | GPIO43 |
| GPIO44 |            |      |         |         |          |             | GPIO44 |
| GPIO45 |            |      |         |         |          |             | GPIO45 |
| GPIO46 |            |      |         |         |          |             | GPIO46 |
| GPIO47 |            |      |         |         |          |             | GPIO47 |

### Component Behavior

This clock component adapts its behavior based on the target board:

**On boards without RTC interrupts:** Time display updates are scheduled in
software.

**On boards with RTC interrupts:** Time display updates may be
interrupt-driven.

The API remains the same; only the internal scheduling strategy changes.

### Summary

| Board         | RTC → GPIO | RTC Interrupt | RTC Wake | Strategy                 |
| ------------- | ---------- | ------------- | -------- | ------------------------ |
| ESP32-S3 1.75 | ❌         | ❌            | ❌       | Software-timed updates   |
| RP2350 2.41   | ✅ (GPIO8) | ✅            | ✅       | Interrupt-driven updates |
