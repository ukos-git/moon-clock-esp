# Moon Phase Clock — project instructions

Physically correct moon-phase clock, dual-board: the Waveshare ESP32-S3 Touch
AMOLED 1.75 (466x466 RGB565 CO5300 QSPI panel, CST9217 touch, QMI8658C IMU,
PCF85063A RTC behind a TCA9554, 16 MB flash + octal PSRAM) and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC "4C" (720x720 round IPS, JD9365 MIPI-DSI, GT911
touch, remote C6 Wi-Fi over SDIO, 32 MB flash + PSRAM; board plumbing in,
Wi-Fi/moon-scale/UI-upscale/clock path in progress). ESP-IDF 5.5.1, LVGL 9.6.
The portable moon core (`components/moon/core`) is shared verbatim with the
host simulator (`host_sim/`).

## Standing rule: keep the documentation current

**After every change to the project** (code, Kconfig, screens, timings,
hardware facts, test system, build steps) **update the project documentation**
— do not let `docs/DOCUMENTATION.md` drift:

1. The prose lives in `docs/doc_template.md`. Fix any statement the change
   invalidated (timings, sizes, options, flow, limits) there.
2. The complete source code is embedded in the generated
   `docs/DOCUMENTATION.md` by `docs/gen_doc.py` — **regenerate it**:
   `python3 docs/gen_doc.py` (embeds every tracked source file verbatim, plus
   the docs/ inventory and this CLAUDE.md). Commit template, generator output,
   and the code change together.
3. If the change adds/reworks a screen, regenerate the screenshots BOTH panels:
   `host_sim/uisim/build.sh run` → `docs/img/screens/` (466, the S3 set — must
   stay sha256-identical when the S3 assets are unchanged), and
   `build.sh run --panel 720` (plain + `--wheel`) → `docs/img/screens/p4/` (the
   wheel shots come from `out/wheel_{rest,touched,ABC,123,sym}.png`, `rest` =
   the un-touched `settings_wifi_password.png`). New animations:
   `python3 docs/gen_media.py` (needs `host_sim/.venv`, PIL, ffmpeg).
4. If the change touches the moon bake path, the host image gate must pass
   before the doc's numbers are trustworthy — BOTH board textures:
   `host_sim/.venv/bin/python host_sim/check_gate.py --target s3` and
   `--target p4` (the gate exits 2 on a stale sim binary built against the
   other target's texture).

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
# Port map (verified 2026-11-01, re-verify with esptool after any re-plug):
#   /dev/ttyACM0 = the P4 4C board (the flashable bench board)
#   /dev/ttyACM1 = the S3 board (a PEER's board — never flash it here)
# S3 (466) — the default target; its board is /dev/ttyACM1 (peer's —
# do NOT flash; the S3's device verification stands in via host parity)
idf.py build
# P4 4C (720) — the Kconfig BOARD choice comes from
# sdkconfig.defaults.esp32p4 (CONFIG_BOARD_P4=y); own build dir + own
# dependencies.lock.p4 (gitignored); the P4 board is /dev/ttyACM0
idf.py -D IDF_TARGET=esp32p4 -B build/p4 build
idf.py -D IDF_TARGET=esp32p4 -B build/p4 flash -p /dev/ttyACM0
# moon assets: per-board pre-baked albedo + DEM blobs (gitignored,
# assets/images/moon/out/{s3,p4}/); the root CMakeLists FATAL_ERRORs on a
# width/manifest/partition mismatch — regenerate after touching the
# pipeline or MOON_ALBEDO_W:
cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4
python3 docs/gen_doc.py                        # regenerate docs/DOCUMENTATION.md
host_sim/.venv/bin/python host_sim/check_gate.py --target s3   # moon image gate (NASA refs)
host_sim/.venv/bin/python host_sim/check_gate.py --target p4   # both board textures must pass
host_sim/uisim/build.sh run                    # real screens -> PNG snapshots (466)
host_sim/uisim/build.sh run --panel 720        # the P4 set (720, the 2048 texture)
python3 docs/gen_media.py                      # docs videos (moon_sim frames)
```

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
