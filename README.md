# Moon Phase Clock

A round desk clock that shows the Moon as it looks right now. A microcontroller
computes where the Sun, the Earth and the Moon are, then draws the Moon pixel
by pixel from NASA maps of the lunar surface: the right phase, the crater
shadows along the day/night line, and the Moon's tilt as seen from your time
zone's city. An automated test checks every change against NASA's own rendered
Moon; on average the two differ by about 7 brightness steps out of 255.

![The clock's home screen in its round case (simulator render)](docs/_static/photos/sim_home.webp)

Around the Moon it is a complete clock: Wi-Fi time sync, a daily alarm or
interval timer with four sounds, a round letter-wheel keyboard for the Wi-Fi
password, and two sleep stages (dimmed, then dark) that a touch or a tilt
undoes.

## Start here

The documentation lives at
[ukos-git.github.io/moon-clock-esp](https://ukos-git.github.io/moon-clock-esp/).

- **Understand it:**
  [How the clock works](https://ukos-git.github.io/moon-clock-esp/how-it-works.html)
  explains moon phases and how a small chip draws the Moon. School maths is
  enough.
- **Try it without a board:**
  [Draw the Moon on your computer](https://ukos-git.github.io/moon-clock-esp/try-the-simulator.html)
  runs the clock's Moon code on a Linux PC in about 15 minutes.
- **Build one:**
  [Getting started](https://ukos-git.github.io/moon-clock-esp/getting-started.html)
  goes from a new board to a clock on your desk.

It runs on three Waveshare boards. You need one:

| Board                                | Screen                 | Notes                                                            |
| ------------------------------------ | ---------------------- | ---------------------------------------------------------------- |
| ESP32-S3-Touch-AMOLED-1.75           | 466 × 466 round AMOLED | The default target. Real-time clock chip, motion sensor, audio.  |
| ESP32-P4-WIFI6-Touch-LCD-XC ("4C")   | 720 × 720 round LCD    | Largest screen, fastest chip. No clock chip.                     |
| ESP32-S3-Knob-Touch-LCD-1.8 ("knob") | 360 × 360 round LCD    | Turning dial and battery. No motion sensor, clock chip or audio. |

## Build & flash

Verified end to end on a fresh clone, on Linux. The S3 1.75 is the default
target; the P4 4C differs only in the pinned target/build dir. Full details
(including port identification and per-board defaults) are in the
[getting-started guide](https://ukos-git.github.io/moon-clock-esp/getting-started.html).

```bash
# one-time: install ESP-IDF 5.5.1 (the version this tree builds against)
git clone --recursive https://github.com/espressif/esp-idf.git && cd esp-idf
git checkout v5.5.1 && ./install.sh esp32s3        # add esp32p4 for the 4C

# every session starts here
source ~/esp/esp-idf/export.sh

# find the board's port and confirm the chip on it (never assume the mapping)
ls /dev/ttyACM*                        # /dev/ttyUSB* on some hosts
esptool.py --port <port> chip_id       # ESP32S3 -> the 1.75; ESP32-P4 -> the 4C

# build + flash the S3 (the default target)
git lfs install
git clone https://github.com/ukos-git/moon-clock-esp.git moon-phase && cd moon-phase

# the NASA ground truth (kit TIFs + gate/quarter frames) ships with the
# repo (LFS) — this bakes the per-board blobs from it (writes gitignored
# out/ output, bootstraps its own .venv; --refs adds the answer-key crops):
python3 assets/images/moon/get_assets.py --refs

idf.py build
idf.py flash -p <port>                 # app + SPIFFS + moon assets
idf.py monitor -p <port>               # serial console, 115200
```

For the **P4 4C**, pin the target, build dir and live config on every command:
`idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build` …
`flash -p <port>` (the flag gives the P4 its own gitignored `sdkconfig.p4` —
the root `sdkconfig` pins the S3).

On first boot the clock has no network yet: it shows the Wi-Fi screen for about
20 seconds, then the home screen. Tap the screen, tap the Wi-Fi icon, pick your
network and type the password on the round keyboard. The credentials are saved
on the board.

## Notes

- **WiFi credentials (optional, first flash):** two lines — SSID then password
  — in `spiffs/wifi_credentials.txt` (template
  `spiffs/wifi_credentials.example.txt`). The SPIFFS partition re-bakes from
  `spiffs/` on every build. Otherwise just enter them on the round keyboard.
- **Timezone:** `idf.py menuconfig` → _Component config → Clock Component
  Configuration → Default timezone_.
- **Moon assets:** the `moon_assets` partition holds the per-board equirect
  albedo + relief DEM. The **NASA ground truth ships with the repo** (the SVS
  4720 kit TIFs + the 36 SVS 5415 gate/quarter frames via LFS, and
  `mooninfo_2025.json` — proven static, sha256-pinned, public domain); only the
  _derived_ products (the baked `out/<target>/` blobs, the `final_400x400/`
  crops, the venv) are gitignored. One stdlib script bakes and fetches the rest
  (bootstraps its own `.venv`; `--refs` adds the image-gate answer-key crops,
  `--fonts` the four foundry faces the docs glyph figure needs):
  `python3 assets/images/moon/get_assets.py --refs --fonts`. After a pipeline
  change re-bake with
  `assets/images/moon/.venv/bin/python -m moon.assets --target s3 p4 knob`,
  then prove it against NASA's renders with
  `host_sim/.venv/bin/python host_sim/check_gate.py --target s3` (and `p4`,
  `knob`).
- **Moon rendering, host:** `host_sim/moon_sim` builds the exact device core on
  the host and writes PNGs at the real panel size; `host_sim/check_gate.py`
  image-diffs it against NASA reference frames.

## Development

```bash
idf.py save-defconfig
idf.py clang-check --exit-code --run-clang-tidy-options="-header-filter=''" main/main.cpp
```

Headless verification switches (all default off) — `TEST_SETTINGS_DEMO_SEC`,
`TEST_AUTO_WAKE_SEC`, `TEST_BAKE_NO_RELIEF`, and more — are documented in the
Kconfig table in the
[UI Kconfig reference](https://ukos-git.github.io/moon-clock-esp/reference/kconfig-ui.html).

## Where to go next

The hardware and build reference lives in the
[docs](https://ukos-git.github.io/moon-clock-esp/), not here:

- [The S3 board](https://ukos-git.github.io/moon-clock-esp/devices/s3.html) —
  wiring, full pin map, build/flash, pre-configured defaults
- [The P4 4C board](https://ukos-git.github.io/moon-clock-esp/devices/p4.html)
  — the sibling port, C6 over SDIO
- [The knob board](https://ukos-git.github.io/moon-clock-esp/devices/knob.html)
  — the dial sibling project (`knob/`), deep sleep on an RTC timer
- [Getting started](https://ukos-git.github.io/moon-clock-esp/getting-started.html)
  — the minimal verified flash runbook, expanded
- [The user interface](https://ukos-git.github.io/moon-clock-esp/ui/index.html)
  — every screen, element by element
- [Performance](https://ukos-git.github.io/moon-clock-esp/verification/performance.html)
  — how fast everything is, with the exact commands to reproduce the numbers

The source is on [GitHub](https://github.com/ukos-git/moon-clock-esp).

## License

The firmware and all other software in this repository are licensed under the
**GNU General Public License, version 2 or later** (GPL-2.0-or-later,
[LINK](LICENSE)). The documentation
([docs/](https://ukos-git.github.io/moon-clock-esp/)) is licensed under
**Creative Commons Attribution-ShareAlike 4.0 International**
([docs/LICENSE](docs/LICENSE)).

Third-party attributions live in [NOTICE](NOTICE): the Waveshare S3 BSP
(Apache-2.0, modified — LVGL strip height 50→25), the ERFA subset (BSD-style,
NumFOCUS/SOFA), stb_image_write (public domain), and the fonts compiled into
the firmware — Montserrat (SIL OFL 1.1) and Font Awesome 7 Free (glyphs CC BY
4.0). The lunar imagery comes from NASA (SVS 4720 / 5415, dialamoon) — U.S.
government works, public domain, credited at their source URLs.
