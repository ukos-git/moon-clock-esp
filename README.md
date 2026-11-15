# Moon Phase Clock

A physically correct moon-phase clock. The current phase of the Moon — from the
last new moon to now — is rendered **live on a microcontroller** from LROC
Wide-Angle Camera albedo and LOLA relief, lit with full-disk displaced-normal
Lambert shading at the exact Sun/Moon/Earth geometry of the ephemeris (JPL
Moon98 + ERFA EPV). It is not a photo and not a clock face with a moon icon:
the crater shadows, the libration, the terminator, and the computed backlit
dark side match NASA's rendered moon to within a couple of gray levels per
pixel.

![The finished clock, simulated](docs/_static/photos/sim_home.png)

The same portable C core runs on two Waveshare boards from one tree:

- **ESP32-S3 Touch AMOLED 1.75** — 466×466 QSPI CO5300 AMOLED (the default
  target, the product's face)
- **ESP32-P4 WIFI6 Touch LCD XC "4C"** — 720×720 round MIPI-DSI JD9365 IPS,
  remote C6 Wi-Fi over SDIO (ported as a full sibling)

The complete documentation — the science, the implementation, the build system,
the per-board references (pin maps, build/flash, partitions), and the
per-screen UI reference with both-panel screenshots — is the Sphinx site under
`docs/` (build it with
`docs/.venv/bin/sphinx-build -b html docs docs/_build/html`; code links resolve
to the locally generated source pages). This README is the entry point, not the
reference.

## Build & flash

Verified end to end on a fresh clone, on Linux. The S3 1.75 is the default
target; the P4 4C differs only in the pinned target/build dir. Full details
(including port identification and per-board defaults) are in
[docs/getting-started](docs/getting-started.rst).

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
git clone <this repo> && cd <this repo>
idf.py build
idf.py flash -p <port>                 # app + SPIFFS + moon assets
idf.py monitor -p <port>               # serial console, 115200
```

For the **P4 4C**, pin the target and build dir on every command:
`idf.py -D IDF_TARGET=esp32p4 -B build/p4 build` … `flash -p <port>`.

On first boot the board scans for networks and lands on the WiFi screen — tap
an access point and enter the password on the round keyboard. The credentials
persist in the board's SPIFFS.

## Notes

- **WiFi credentials (optional, first flash):** two lines — SSID then password
  — in `spiffs/wifi_credentials.txt` (template
  `spiffs/wifi_credentials.example.txt`). The SPIFFS partition re-bakes from
  `spiffs/` on every build. Otherwise just enter them on the round keyboard.
- **Timezone:** `idf.py menuconfig` → _Component config → Clock Component
  Configuration → Default timezone_.
- **Moon assets:** the `moon_assets` partition holds the per-board equirect
  albedo + relief DEM (gitignored). Regenerate with
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
Kconfig table in the [documentation](docs/reference/kconfig-ui.rst).

## Where to go next

The hardware and build reference lives in the docs, not here:

- [The S3 board](docs/devices/s3.rst) — wiring, full pin map, build/flash,
  pre-configured defaults
- [The P4 4C board](docs/devices/p4.rst) — the sibling port, C6 over SDIO
- [Getting started](docs/getting-started.rst) — the minimal verified flash
  runbook, expanded
- [Performance](docs/verification/performance.rst) — how fast everything is,
  with the exact commands to reproduce the numbers
