# Moon Phase Clock — project instructions

Physically correct moon-phase clock, dual-board: the Waveshare ESP32-S3 Touch
AMOLED 1.75 (466x466 RGB565 CO5300 QSPI panel, CST9217 touch, QMI8658C IMU,
PCF85063A RTC behind a TCA9554, 16 MB flash + octal PSRAM) and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC "4C" (720x720 round IPS, JD9365 MIPI-DSI, GT911
touch, remote C6 Wi-Fi over SDIO, 32 MB flash + PSRAM — the full sibling port;
`docs/devices/p4`). ESP-IDF 5.5.1, LVGL 9.6. The portable moon core
(`components/moon/core`) is shared verbatim with the host simulator
(`host_sim/`) and with the third board of the moon-clock family — the
ESP32-S3-Knob-Touch-LCD-1.8 "knob": a **third build of the same tree** on the
esp32s3 target, selected by `-D KNOB_BOARD=ON` (own build dir,
`dependencies.lock.knob` and gitignored `sdkconfig.knob`, its third asset
target `--target knob`; `docs/devices/knob` and
`docs/implementation/knob-display`). Since the P1 home parity step it compiles
the clock's shared components (`clock`, `wifi`, `moon`, and `ui` — the shared
home screen over the knob's `knob_moon_render.c` seam + generated `fonts_knob/`
tier; legacy own-screen view behind `KNOB_UI_ENABLE=n`) as ordinary sibling
components of the SAME `components/` tree (entry point `main/main_knob.c`,
`knob_*` components alongside the shared ones; the shared `board`/`imu`
components answer the knob's board-coupled REQUIREs with their `KNOB_BOARD`
rows — `knob_board` + `knob_include` and `imu_stub.c` — and `esp_codec_dev` is
the real managed dependency `knob_board`'s manifest declares under a
`target == "esp32s3"` rule (the gate must be evaluable in the component
manager's `prepare_dependencies` step, which runs BEFORE kconfig: no
`sdkconfig.json` yet, so a `$CONFIG{…}` rule cannot resolve there); the clock
blocks' `EXCLUDE_COMPONENTS knob_board` is what keeps those deps out of the
S3/P4 locks (a target rule alone cannot, the knob IS an esp32s3); the shared
`choice BOARD` in `components/board/Kconfig.projbuild` carries the `BOARD_KNOB`
row, default `BOARD_S3`, pinned `y` in `sdkconfig.defaults.knob`) and adds the
dial, battery gauge and real `esp_deep_sleep_start` armed with RTC **TIMER**
wake for the fixed alarm rule (never an early ring; see the ladder +
arbitration in `docs/implementation/knob-display`). Component
`REQUIRES`/`PRIV_REQUIRES` must stay **literal**: the component manager's early
requirements pass (`build.cmake` →
`cmake -P component_get_requirements .cmake`) sees neither project variables
nor `CONFIG_` symbols — branch only SRCS/INCLUDES (target pass), and the knob's
branch key `KNOB_BOARD` reaches that early pass only through
`idf_build_set_property` (a plain `-D` cache variable does not).

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
2. If the change adds/reworks a screen, UI geometry, or figure, regenerate the
   media — `docs/.venv/bin/sphinx-build -b media` (the `moonmedia` builder: the
   scripted figs — every `docs/tools/gen_*.py` + the node isomorphic pipeline
   `gen_figures.js` → `assemble_figures.py` and `gen_bake_intermediates.js` →
   `gen_bake_strip.py` — then `host_sim/uisim/build.sh run` + `--panel 720` +
   `--panel 360` + the `--wheel`/`--dial` captures, `docs/tools/enclose.py`
   (numbered callouts inside the round product housing, alpha outside) over
   every `docs/ui/labels/*.json` spec, the device composites; `MEDIA_VIDEOS=1`
   adds the film, `MEDIA_NASA=1` the NASA answer-key art (gate run +
   `nasa_figs.py` + `gen_nasa_quarters.py`, needs the gitignored
   `assets/images/moon/final_400x400`). ALL docs media
   (`docs/img/screens{,/p4,/knob,/annot}`, `docs/_static/photos`,
   `docs/_static/figs`, `docs/img/nasa`, `docs/img/fade.mp4`,
   `docs/img/promo.mp4`) is gitignored build output — regenerated, NEVER
   committed (2026-11-16); the pages embed the files on disk, so `-b html`
   0-warnings presumes a media run. ONE exception:
   `docs/_static/photos/sim_home.webp` (the README hero, LFS) — the README
   renders on GitHub without a media run, so its signature image is the one
   committed photo; `docs/tools/gen_hero.py` regenerates it from the
   `sim_home.png` composite after any media run that changes it. The vendor
   legs of `perf_bus`/`perf_silicon` parse `managed_components/` — it holds
   only the LAST-resolved target's set, so reconfigure the P4 build for the
   gt911 header. The glyph figure `gen_perf_glyphs.py` needs the four foundry
   faces under `assets/fonts/` — fetched once, sha256-pinned, by
   `python3 assets/images/moon/get_assets.py --fonts`. The by-hand path
   (`build.sh run` → `docs/img/screens/`, `build.sh run --panel 720` →
   `docs/img/screens/p4/`, `--panel 360` → `docs/img/screens/knob/`, then
   re-run `enclose.py` for the affected label specs) stays fine for
   single-screen iterations. If the change only touches the moon bake path, the
   host image gate must pass before the doc's numbers are trustworthy — ALL
   THREE board textures (the knob build included):
   `host_sim/.venv/bin/python host_sim/check_gate.py --target s3` and
   `--target p4` and `--target knob` (the gate exits 2 on a stale sim binary
   built against the other target's texture).
3. If the change alters an animation, regenerate the videos:
   `python3 docs/gen_media.py` (needs `host_sim/.venv`, PIL, ffmpeg; the fade
   film is pinned to the S3 466 uisim set — run `host_sim/uisim/build.sh run`
   first, or run the `media` builder with `MEDIA_VIDEOS=1`, which does exactly
   that in the right pipeline slot).
4. Verify clean: `docs/.venv/bin/sphinx-build -b html docs docs/_build/html`
   must finish with **0 warnings** (the `moonmedia` staleness check warns at
   build time if a generated screenshot or film is missing or older than its
   render inputs — media are build outputs, so "missing" = `-b media` was never
   run on the tree), and `docs/.venv/bin/python docs/tools/check_docs_drift.py`
   must exit **0** — it is the drift gate for the hand-kept-in-sync
   single-sourced fragments (Kconfig/consts/partitions), the UI reachability
   matrix and its screenshots, and the binary-size rows. Commit docs with the
   code change (the generated media stays out — it is gitignored).

## Hard invariants (violations ship visible bugs)

- Every moon-pipeline frame hits the panel as ONE atomic whole-panel
  `lv_refr_now` under `s_rgb_mtx` (`moon_ui_screen_switched`). ONE sanctioned
  exception: the hardware panel-brightness ramp (a 0x51/backlight ramp that
  writes no pixels) — the fade-to-black screen switch (`UI_SCREEN_SWITCH_FADE`;
  the light-sleep dim rides the same family) is the only flow that dims, loads,
  flushes while still black, then ramps back. Computing frames NEVER ride the
  ramp: the compute-progress posture (user decision 2026-11-24,
  `docs/implementation/compute-progress`, which retired the strip-by-strip
  **bake reveal** and the hourly fade-to-black) keeps the old frame up the
  whole bake (boot / no frame on glass: the quick flat phase disc), the core-1
  producer publishes a rows-completed watermark per strip that the home/wifi
  seconds arc shows as a progress bar, and the finished frame lands as ONE
  atomic whole-panel flush in the bright panel. `s_frame` is never the partial
  side (only the PSRAM bounce is written strip by strip), so every resting
  panel state is a complete frame and there is no heal path on an abort. (The
  burn-in spin's direct panel write, the TF sweep cache's direct-write replay,
  the hourly ramp and the bake reveal were further exceptions until 2026-11-13
  / 2026-11-24, when the spin subsystem was removed entirely and the reveal
  retired — the home moon is plain LVGL, shown static; the TF module now serves
  the hourly refresh as its hour cache.)
- AMOLED burn-in is an accepted trade-off, not an open bug: with the spin gone
  the S3 shows the moon statically and a faint burned-in moon is expected — the
  user's standing call (2026-11-14) is that it is a feature rather than a bug
  (it will likely not disturb reading the display). Do not reintroduce
  anti-burn-in machinery (spin, pixel-shift, …) without a fresh user decision.
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

## Hardware (the Moon v1 board + the universal housing)

`hardware/` holds the two things that are **not** the shipping firmware: the
proposed **Moon v1** board (a standalone PCB that fixes the three boards'
alarm-wake / deep-sleep pitfalls — the PCF85063A ALM0 wired direct to an
EXT1/deep-sleep-capable S3 GPIO, real `esp_deep_sleep`, charge-IRQ + ADC1
battery gauge; `hardware/moonv1/`, KiCad 10) and the **universal 3D-printed
housing** that seats all three boards (a common chassis + four swappable seat
cartridges; `hardware/housing/`). `docs/housing` is the single-sourced page.

- **Source vs build output (the gitignore split):** committed = the KiCad
  project (`moonv1.kicad_pro` / `_sch` / `_pcb` / footprints / parts), the
  housing `params.json` + `build_housing.py` + `render.py` + `audit_stls.py`
  - the **STLs** (the printable deliverables). Gitignored build output =
    `hardware/moonv1/parts/vendor/` (Waveshare archives, re-fetched by
    `get_vendor.py`), `moonv1/{gerbers,step,svg,drc.html,erc.html}` (KiCad
    exports, regenerated by the gates below), and `hardware/housing/renders/`
    (regenerated previews — the docs page embeds them from disk, so a clean
    `-b html` presumes a `render.py` run, like the other media).
- **Blender runs via the flatpak, headless, and CANNOT see `/tmp`** — every
  Blender output (STLs, renders, temp `.blend`) must go under the project tree
  or `$HOME`, never `/tmp`. The pipeline is MCP-agnostic:
  ```bash
  flatpak run org.blender.Blender -b -P hardware/housing/build_housing.py -- --out hardware/housing/stl
  python3 -I hardware/housing/audit_stls.py        # re-measures the exported STLs (0.05 mm)
  flatpak run org.blender.Blender -b -P hardware/housing/render.py   # -> renders/
  ```
  The **two audits are the gates**: `build_housing.py` returns a hard-fail list
  (pocket radius vs board outline, anti-flex screw keep-outs, pocket depth vs
  board stack, chassis fit, height cap) and `audit_stls.py` re-measures the
  **exported** STLs in the faceplane frame. STLs are world-space millimetres
  (the exporter writes raw mm — verified against the evaluated scene), so the
  audit runs on the raw values; a x1000 / ÷1000 unit bug fails the 10–300 mm
  per-axis sanity band.
- **Anti-flex mounting rule (highest-priority mechanical rule, from the knob
  brown-out field history):** exactly ONE M2.5 screw per seat (the knob seat is
  press-fit, zero screws), printed boss with 0.10 mm clearance thread, **max
  0.5 N·m**, landing on a vendor mounting hole on a non-display, non-battery
  corner. Never screw through or clamp on the display. All seat geometry is
  built in the **faceplate's own coordinate frame** (the tilted plane the board
  sits in), never raw XY — a raw-XY offset lands a cut/boss ~1.8 mm off at the
  P4's 45° holes.
- **KiCad 10 gates (headless `kicad-cli`, from the AppImage):** no `new`
  subcommand — the `.kicad_pro` is hand-seeded as text. The board gates are
  `kicad-cli sch erc` → 0 errors (+ a netlist script asserting ALM0 reaches an
  EXT1 GPIO, VBUS-sense + charge-IRQ present, senses on ADC1, FPC pinout
  pin-for-pin vs the vendor PDF) and `kicad-cli pcb drc` → 0 violations (+
  courtyard checks, clean `pcb export gerbers`, `pcb export step`). Phase B2
  closes the loop: `pcb export step` → import into Blender → the Moon v1
  housing seat is regenerated from the real board outline, re-running both
  audits.

## Commands

```bash
source ~/esp/esp-idf/export.sh
# S3 (466) — the default target:
idf.py build
# P4 4C (720) — the Kconfig BOARD choice comes from
# sdkconfig.defaults.esp32p4 (CONFIG_BOARD_P4=y); own build dir + own
# dependencies.lock.p4 + own live sdkconfig.p4 (all gitignored). The
# -D SDKCONFIG is REQUIRED while the root sdkconfig still pins
# IDF_TARGET=esp32s3 (targets.cmake aborts the P4 configure otherwise);
# it becomes a cache entry after the first configure — keep passing it:
idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build
# flash the board you identified (see "Identifying the boards"):
idf.py flash -p <port>
idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 flash -p <port>
# moon assets — the NASA ground truth SHIPS WITH THE REPO (static,
# proven — one constant archive digest 2020-2026; sha256+size-pinned,
# public domain): the SVS 4720 kit (assets/images/moon/moon-kit/) + the
# 36 SVS 5415 gate/quarter frames (moon_phases_2025/) via LFS and
# mooninfo_2025.json as tracked JSON. The ONE-SHOT step on a fresh
# clone (stdlib python3) re-verifies the committed sources against
# their pins (re-fetched from the Internet Archive on a failed pin —
# SVS retired its /vis/ tree 2026-11-26), bootstraps its own .venv,
# regenerates tone_lut.bin, bakes out/{s3,p4,knob}; --refs adds the
# final_400x400/ answer-key crops, --fonts the four foundry faces under
# assets/fonts/ (the docs glyph figure, gen_perf_glyphs.py) — the
# derived products stay gitignored; provenance + pins:
# assets/images/moon/README.md):
python3 assets/images/moon/get_assets.py --refs --fonts
# moon assets: per-board pre-baked albedo + DEM blobs (gitignored,
# assets/images/moon/out/{s3,p4,knob}/); the root CMakeLists FATAL_ERRORs on a
# width/manifest/partition mismatch (one guard block run by all three boards —
# the knob build skips only the albedo-width check, no Kconfig mirror) —
# regenerate after touching the pipeline or MOON_ALBEDO_W (sources
# stay cached under moon-kit/, so the bake leg needs no network):
cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob
# knob moon display (third build of the SAME tree: esp32s3 target,
# -D KNOB_BOARD=ON; own build dir + gitignored dependencies.lock.knob +
# sdkconfig.knob — the knob is an esp32s3 target, so its live config
# cannot ride the IDF_TARGET hook and is passed explicitly; the defaults
# files are likewise passed explicitly because sdkconfig.defaults.knob is
# not auto-appended for a non-target build — only <file>.<idf_target> is;
# flash the board chip_id-verified as the bench's ESP32-S3; app-flash
# preserves the shared spiffs credentials):
idf.py -D KNOB_BOARD=ON -D SDKCONFIG=sdkconfig.knob \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.knob" \
  -B build/knob build
idf.py -D KNOB_BOARD=ON -D SDKCONFIG=sdkconfig.knob \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.knob" \
  -B build/knob app-flash -p <port>
host_sim/.venv/bin/python host_sim/check_gate.py --target s3   # moon image gate (NASA refs)
host_sim/.venv/bin/python host_sim/check_gate.py --target p4   # all three board
host_sim/.venv/bin/python host_sim/check_gate.py --target knob # textures must pass
host_sim/uisim/build.sh run                    # real screens -> PNG snapshots (466)
host_sim/uisim/build.sh run --tour              # the promo film's frames (out/tour_*.png, S3 466 only; moon
                                                       # screens first, the settings ring walk last)
host_sim/uisim/build.sh run --panel 720        # the P4 set (720, the 2048 texture)
host_sim/uisim/build.sh run --panel 360        # the knob set (360, CONFIG_BOARD_KNOB, fonts_knob tier)
python3 docs/gen_media.py                      # docs films: fade.mp4 + promo.mp4 (from the S3 466 out/ set; the
                                                   # promo frames go in the round product case)
docs/.venv/bin/sphinx-build -b html docs docs/_build/html       # the docs site (0 warnings is the gate)
docs/.venv/bin/python docs/tools/check_docs_drift.py           # drift gate: the single-sourced
                                                               # fragments (Kconfig/consts/
                                                               # partitions), the UI reachability
                                                               # matrix + its screenshots, and the
                                                               # binary-size rows vs the live
                                                               # sources (exit 1 = DRIFT: lines)
docs/.venv/bin/sphinx-build -b media docs docs/_build/media     # THE media regen: scripted figs + all three
                                                               # boards' screens/annot + composites
                                                               # (MEDIA_VIDEOS=1 adds the film, MEDIA_NASA=1
                                                               # the NASA art); ALL docs media is
                                                               # gitignored build output — never committed
```

### Identifying the boards

Never hard-code which port is which board — the mapping changes with USB
topology (which board is plugged in, hub order, a re-plug). Every board
presents a serial adapter the host claims as a `/dev/ttyACM*` node:

1. Plug in _one_ board at a time and watch what node appears (`lsusb`,
   `dmesg | tail`) — the node that appears on plug-in (and disappears on
   unplug) is that board's port.
2. Confirm which chip sits on the port before flashing:
   `esptool.py --port <port> chip_id` — `ESP32S3` identifies the S3 board,
   `ESP32-P4` the 4C. That single check catches a stale port guess. The knob is
   ALSO an ESP32-S3 with the same Espressif USB-JTAG descriptor (303a:1001) —
   chip_id cannot tell it from the AMOLED; read the live partition table
   instead: `esptool.py --port <port> read_flash 0x8000 0xc00 pt.bin` — the
   knob's carries `factory` 4 M + a `coredump` row (storage at 0x410000), the
   AMOLED's 2 M factory, no coredump (storage at 0x210000).
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
them; the hook's own filter keeps it safe). Commit messages are policed at the
commit-msg stage by two local hooks in `.pre-commit-config.yaml`:
`.githooks/cz-commit-msg` runs the commitizen checker
(`cz check --commit-msg-file $1 -l 100`) so every commit is a conventional
commit with a ≤100-char subject, and `.githooks/commit-msg` refuses AI
co-author trailers; `git-cz` / `cz commit` give the matching interactive
prompt, and after editing `.pre-commit-config.yaml` re-run
`pre-commit install -t commit-msg`.
