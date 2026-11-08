# Moon Phase Clock — project instructions

Physically correct moon-phase clock, dual-board: the Waveshare ESP32-S3 Touch
AMOLED 1.75 (466x466 RGB565 CO5300 QSPI panel, CST9217 touch, QMI8658C IMU,
PCF85063A RTC behind a TCA9554, 16 MB flash + octal PSRAM) and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC "4C" (720x720 round IPS, JD9365 MIPI-DSI, GT911
touch, remote C6 Wi-Fi over SDIO, 32 MB flash + PSRAM — the full sibling port;
`docs/devices/p4`). ESP-IDF 5.5.1, LVGL 9.6. The portable moon core
(`components/moon/core`) is shared verbatim with the host simulator
(`host_sim/`) and with the third Waveshare board — the
ESP32-S3-Knob-Touch-LCD-1.8 "knob", a standalone moon display as the `knob/`
sibling project (its own ESP-IDF project, build dir, `dependencies.lock.knob`
and gitignored `knob/sdkconfig`, its third asset target `--target knob`;
`docs/devices/knob` and `docs/implementation/knob-display`; it shares the moon
core and asset pipeline with the clock and nothing else).

## Standing rule: keep the documentation current

The project documentation is the Sphinx site under `docs/` (furo; built with
`docs/.venv/bin/sphinx-build -b html docs docs/_build/html`). **After every
change to the project** (code, Kconfig, screens, timings, hardware facts, test
system, build steps) **update the site** — do not let it drift:

1. Fix the affected RST pages — every statement the change invalidated
   (timings, sizes, options, flow, limits). The reference tables (Kconfig,
   constants, partitions) are single-sourced; regenerate them from the current
   sources, and keep the per-board pages (`docs/devices/{s3,p4}.rst`) in sync
   with their `sdkconfig.defaults*` files.
2. If the change adds/reworks a screen or UI geometry, regenerate the
   screenshots BOTH panels — `docs/.venv/bin/sphinx-build -b media` (the
   `moonmedia` builder: `host_sim/uisim/build.sh run` + `--panel 720` + the
   `--wheel` capture, `docs/tools/enclose.py` (numbered callouts inside the
   round product housing, alpha outside) over every `docs/ui/labels/*.json`
   spec, the device composites) or by hand (`build.sh run` →
   `docs/img/screens/`, `build.sh run --panel 720` → `docs/img/screens/p4/`,
   then re-run `enclose.py` for the affected label specs). If the change only
   touches the moon bake path, the host image gate must pass before the doc's
   numbers are trustworthy — ALL THREE board textures (the knob sibling
   included): `host_sim/.venv/bin/python host_sim/check_gate.py --target s3`
   and `--target p4` and `--target knob` (the gate exits 2 on a stale sim
   binary built against the other target's texture).
3. If the change alters an animation, regenerate the videos:
   `python3 docs/gen_media.py` (needs `host_sim/.venv`, PIL, ffmpeg).
4. Verify clean: `docs/.venv/bin/sphinx-build -b html docs docs/_build/html`
   must finish with **0 warnings** (the `moonmedia` staleness check warns at
   build time if a committed screenshot is missing or older than its render
   inputs). Commit docs with the code change.

## Hard invariants (violations ship visible bugs)

- Every moon-pipeline frame hits the panel as ONE atomic whole-panel
  `lv_refr_now` under `s_rgb_mtx` (`moon_ui_screen_switched`). Two sanctioned
  exceptions: the fade-to-black screen switch (`UI_SCREEN_SWITCH_FADE`, a
  hardware panel-brightness ramp that writes no pixels — the new screen's one
  atomic flush runs while the panel is still black) and the burn-in spin's
  direct panel write (wrapped `esp_lcd_panel_draw_bitmap`, the render task owns
  the panel for that frame under `s_panel_mtx`).
- `spiffs/wifi_credentials.txt` and `sdkconfig` are gitignored — never commit
  either; `spiffs/` edits are separate `spiffs:` commits.
- The 16 MB board's PSRAM is NOT SPI-DMA-capable: display flush strips are
  staged through internal RAM; keep the BSP `buffer_height` at 25 and the flush
  queue depth at 1 (32 KiB S3 SPI DMA cap).
- Flash budget: S3 `factory` partition 2,097,152 B (2048 K — widened from the
  old 1392 K / 1,425,408 B ceiling when the app outgrew it); P4 `factory` 9216
  K (`partitions_p4.csv`, 32 MB chip — the binary header stays 16 MB, so
  esptool's size-mismatch warning is expected); check the binary size after
  every build.
- Kconfig `n` leaves the macro UNDEFINED — all runtime values go through
  `#ifndef`-seeded getters, never `#define` redefinitions of the Kconfig
  macros.
- `managed_components/` is gitignored — depend on its public API only, never on
  modified vendor files.

## Commands

```bash
source ~/esp/esp-idf/export.sh
# S3 (466) — the default target:
idf.py build
# P4 4C (720) — the Kconfig BOARD choice comes from
# sdkconfig.defaults.esp32p4 (CONFIG_BOARD_P4=y); own build dir + own
# dependencies.lock.p4 (gitignored):
idf.py -D IDF_TARGET=esp32p4 -B build/p4 build
# flash the board you identified (see "Identifying the boards"):
idf.py flash -p <port>
idf.py -D IDF_TARGET=esp32p4 -B build/p4 flash -p <port>
# moon assets: per-board pre-baked albedo + DEM blobs (gitignored,
# assets/images/moon/out/{s3,p4,knob}/); the root CMakeLists FATAL_ERRORs on a
# width/manifest/partition mismatch (knob/CMakeLists.txt replicates the
# guards) — regenerate after touching the pipeline or MOON_ALBEDO_W:
cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob
# knob moon display (sibling project, own build dir + own gitignored
# dependencies.lock.knob + knob/sdkconfig; flash the board chip_id-verified
# as the bench's ESP32-S3; app-flash preserves its spiffs credentials):
idf.py -C knob -B ../build/knob build
idf.py -C knob -B ../build/knob app-flash -p <port>
host_sim/.venv/bin/python host_sim/check_gate.py --target s3   # moon image gate (NASA refs)
host_sim/.venv/bin/python host_sim/check_gate.py --target p4   # all three board
host_sim/.venv/bin/python host_sim/check_gate.py --target knob # textures must pass
host_sim/uisim/build.sh run                    # real screens -> PNG snapshots (466)
host_sim/uisim/build.sh run --panel 720        # the P4 set (720, the 2048 texture)
python3 docs/gen_media.py                      # docs videos (moon_sim frames)
docs/.venv/bin/sphinx-build -b html docs docs/_build/html       # the docs site (0 warnings is the gate)
docs/.venv/bin/sphinx-build -b media docs docs/_build/media     # regenerate the docs screenshots + composites
```

### Identifying the boards

Never hard-code which port is which board — the mapping changes with USB
topology (which board is plugged in, hub order, a re-plug). Both boards present
a serial adapter the host claims as a `/dev/ttyACM*` node:

1. Plug in _one_ board at a time and watch what node appears (`lsusb`,
   `dmesg | tail`) — the node that appears on plug-in (and disappears on
   unplug) is that board's port.
2. Confirm which chip sits on the port before flashing:
   `esptool.py --port <port> chip_id` — `ESP32S3` identifies the S3 board,
   `ESP32-P4` the 4C. That single check catches a stale port guess.
3. Keep a scratch note of the mapping _for this plug session_ (it does not
   survive a re-plug); re-run step 2 if in doubt.

Defaults are the 3-file split (`sdkconfig.defaults` shared +
`sdkconfig.defaults.esp32s3` / `sdkconfig.defaults.esp32p4` per-target — the
per-target file is auto-appended when it exists, later file wins).
`idf.py save-defconfig` writes the _shared_ file only — with this split it
would clobber the board-specific lines, so keep the three files hand-curated (a
stray board line in the wrong file is caught by the root CMakeLists'
target/board `FATAL_ERROR`). A hand-edited live `sdkconfig` pin ALSO beats a
Kconfig default (Kconfig defaults are first-match-wins and a `CONFIG_…=…` line
in the live file wins over every `default` line) — after changing a default,
delete the stale pin so the new default takes effect:
`sed -i '/^CONFIG_UI_MOON_ANIM_BAKE_PX=/d; /^CONFIG_MOON_ALBEDO_W=/d' sdkconfig sdkconfig.p4`
(the per-target `sdkconfig.p4` backups follow the same rule).

zsh: quote globs (`--include='*.c'`). Pre-commit runs clang-format on C/H only
— **never run clang-format on Kconfig/other extensionless files** (it mangles
them; the hook's own filter keeps it safe).
