Chapter 24 — Build system and Kconfig
=====================================

Two boards, one tree: the S3 build (the Waveshare
ESP32-S3-Touch-AMOLED-1.75) is the default target and the P4 build
(the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) is a sibling with its own
partition table, its own dependency
lock, and a board-select Kconfig that gates the app's board-specific
code. This chapter is the build layer — the three-file
``sdkconfig.defaults`` split, the P4 target block in the root
CMakeLists, the two flash-data partitions (SPIFFS + the moon assets
blob), and the Kconfig conventions the project lives by.

The three-file sdkconfig split
------------------------------

ESP-IDF loads ``sdkconfig.defaults``, then appends
``sdkconfig.defaults.<target>`` **when it exists** (later file wins).
The project uses that hook to keep one shared file and two per-target
files — all three **hand-curated**:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - File
     - Contents
   * - ``sdkconfig.defaults``
     - the shared values (the LVGL 9.6 setup, the component-manager
       manifest, ``CONFIG_PARTITION_TABLE_CUSTOM=y``, the clock/Kconfig
       defaults — everything that is the *same* on both boards)
   * - ``sdkconfig.defaults.esp32s3``
     - the S3's board line (``CONFIG_BOARD_S3=y``) + the S3-only
       values (the BSP's flush geometry: ``buffer_height 25``, queue
       depth 1 — Chapter 13's NO_MEM fix)
   * - ``sdkconfig.defaults.esp32p4``
     - the P4's board line (``CONFIG_BOARD_P4=y``), the partition
       file (``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``),
       the rev v1.3 silicon (``CONFIG_ESP32P4_REV_MIN_100=y``),
       ``CONFIG_SPIRAM_SPEED_200M=y`` (the experimental-flag gotcha —
       without it the PSRAM *silently falls back to 20 MHz* and the
       DSI underruns, Chapter 16), the XIP-from-PSRAM + 256 KB L2
       cache (the 128 B-line M0 values), and the taller main stack

The ``save-defconfig`` warning (the project's Kconfig rule #1):
``idf.py save-defconfig`` writes the *shared* file only. With the
split, a ``save-defconfig`` run would **clobber the board-specific
lines** (a P4 run would delete the S3's ``buffer_height``; an S3 run
would delete the P4's ``SPIRAM_SPEED_200M``). The three files are
therefore kept by hand — a stray board line in the wrong file is a
*build-time* error, not a runtime one: the root CMakeLists' guard
(above) raises ``FATAL_ERROR`` on a target/board mismatch.

The build commands
------------------

.. code-block:: bash
   :caption: The two targets (CLAUDE.md's Commands, verbatim)

   source ~/esp/esp-idf/export.sh

   # S3 (466) — the default target
   idf.py build && idf.py flash -p /dev/ttyACM1

   # P4 4C (720) — own build dir + own dependencies.lock.p4
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 build
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 flash -p /dev/ttyACM0

Two constraints the commands encode:

- **The P4 build has its own build dir (``build/p4``) and its own
  dependency lock (``dependencies.lock.p4``, gitignored).** The
  component manager rewrites its lock on every configure and the two
  targets resolve *different* dependency sets (different BSPs +
  driver stacks) — a shared lock would oscillate. The root
  CMakeLists sets ``DEPENDENCIES_LOCK`` *before* ``project()`` (which
  runs the component manager) for the P4 build.
- **Never run two ``idf.py`` builds in parallel** (a project rule):
  the two targets share the component manager's cache, and parallel
  configures corrupt each other's lock resolution. Build
  sequentially.

The P4 target block
-------------------

.. code-block:: cmake
   :caption: Root CMakeLists.txt — the esp32p4 block (abridged)

   if(IDF_TARGET STREQUAL "esp32p4")
       # The vendored S3 BSP is S3-only (its manifest declares
       # targets: [esp32s3]); EXCLUDE_COMPONENTS keeps it out of the
       # P4 build AND of the component manager's local-manifest list
       # (which would otherwise FATAL_ERROR on the target mismatch).
       set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75")
       idf_build_set_property(DEPENDENCIES_LOCK
           "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.p4")
   endif()

   project(moon-phase)

   # Target/board mismatch guard
   if((IDF_TARGET STREQUAL "esp32p4" AND NOT CONFIG_BOARD_P4) OR
           (IDF_TARGET STREQUAL "esp32s3" AND NOT CONFIG_BOARD_S3))
       message(FATAL_ERROR "Board mismatch: IDF_TARGET=...")
   endif()

The two gotchas in the block:

1. **The partition table is *not* set in CMake.** The
   ``partition_table`` component's project hook re-derives
   ``PARTITION_CSV_PATH`` from the Kconfig values, so a plain
   ``set()`` before ``project()`` is clobbered. The selection lives
   in the per-target defaults files (the shared defaults enable
   ``CONFIG_PARTITION_TABLE_CUSTOM=y``; the P4 file sets the
   ``partitions_p4.csv`` filename; the S3 keeps the default
   ``partitions.csv``).
2. **``EXCLUDE_COMPONENTS`` must name the S3 BSP.** The vendored
   manifest is target-specific (``targets: [esp32s3]``); left in the
   P4 build, the component manager's local-manifest check
   raises ``FATAL_ERROR`` on the target mismatch *before* the app builds.

The flash-data partitions
-------------------------

The root CMakeLists flash two *data* partitions (the ``factory`` app
is the third, Chapter 20):

.. code-block:: cmake
   :caption: The two data-partition flashes (abridged)

   # SPIFFS: the runtime-owned files (wifi_credentials.txt + alarm.txt)
   spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

   # The moon assets: the equirect albedo + int8 relief DEM blob
   set(MOON_ASSETS_BIN ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/moon/moon_assets.bin)
   if(EXISTS ${MOON_ASSETS_BIN})
       esptool_py_flash_to_partition(flash "moon_assets" ${MOON_ASSETS_BIN})
   else()
       message(WARNING "moon assets not found ...")
   endif()

The two are *different kinds* of flash data:

- **``storage`` (SPIFFS)** is baked from the *source tree's*
  ``spiffs/`` directory on **every** build (``FLASH_IN_PROJECT``) —
  and it is *runtime-owned*: the app writes ``alarm.txt`` and
  ``wifi_credentials.txt`` back to it, so a full flash **clobbers**
  the runtime state (accepted; ``spiffs/`` must not contain
  ``settings.txt``, which the app owns at runtime).
- **``moon_assets``** is a *generated* blob (the asset pipeline,
  Chapter 22: ``cd assets/images/moon && python -m moon.assets``),
  local-only and gitignored. The build *warns* (does not fail) when
  it is missing — the app boots without the moon, which is the
  failure mode a reader should recognize. The blob's layout
  (albedo at 0, then the int8 DEM, ``esp_partition_read`` at boot) is
  the ``moon_assets.h`` contract.

The Kconfig conventions
-----------------------

The project's Kconfig rules (CLAUDE.md's hard invariants) are
conventions with *specific bugs* behind each:

1. **A Kconfig ``n`` leaves the macro UNDEFINED.** ``CONFIG_FOO=n``
   does not define ``CONFIG_FOO`` — it defines *nothing*. The bug this
   prevents: a ``#define CONFIG_FOO 0`` fallback that *redefines* the
   macro when it is set. The rule: all runtime values go through
   **``#ifndef``-seeded getters** —

   .. code-block:: c

      static int foo_get(void)
      {
          #ifndef CONFIG_FOO
          static const int v = FOO_DEFAULT;
          #else
          static const int v = CONFIG_FOO;
          #endif
          return v;
      }

   never a ``#define`` redefinition of the Kconfig macro. (The
   settings store, Chapter 15, is the pattern's main user: every
   runtime-tunable value — the idle timeout, the sleep countdowns, the
   animation flags — is a ``#ifndef``-seeded getter, never a
   ``#define``.)

2. **Never clang-format a Kconfig file.** The pre-commit hook runs
   clang-format on C/H only — the hook's own filter keeps it safe —
   but a manual ``clang-format Kconfig`` mangles the file (the
   ``menuconfig`` syntax is not C). The rule is *never run it on
   extensionless files*.

3. **Board-conditional code gates on ``CONFIG_BOARD_P4`` /
   ``CONFIG_BOARD_S3``** (the board-select Kconfig in
   ``components/board/Kconfig.projbuild``) — *not* on
   ``CONFIG_IDF_TARGET``. The board is the app's concept; the target
   is the build's. The mismatch guard (above) makes the two agree at
   build time, so the app can gate on the board and never see a
   target/board split. The P4's moon-geometry choices (``MOON_PX``
   400/650, ``MOON_R`` 200/325, the 103/65-row spin strips) are the
   main consumers (Chapter 16).

4. **``managed_components/`` is gitignored.** The component manager
   owns that tree; the app depends on its *public API* only, never on
   modified vendor files. A fix that needs a vendor change goes to
   the *vendor* (or a local wrapper), not to a vendored edit — the
   ``dependencies.lock`` is the only tracked record of the vendor
   versions. (The two vendored-BSP fixes in the project's history —
   the ``buffer_height`` 50→25 and the ``max_transfer_sz`` 23,300 B —
   were made in *tracked* local BSP wrappers/shims, not in
   ``managed_components/``, for exactly this reason.)

The component layout
--------------------

.. list-table::
   :header-rows: 1
   :widths: 24 44 32

   * - Component
     - Responsibility
     - Key files
   * - ``main``
     - the boot sequence (assets, clock, WiFi, UI)
     - ``main.cpp``
   * - ``moon``
     - the portable core + the board display pipeline
     - ``core/`` (portable), ``moon_ui.c`` (the pipeline)
   * - ``clock``
     - the RTC (PCF85063A), the SNTP sync, the alarm scheduler,
       the settings store, the timezone table
     - ``clock.c``, ``clock_alarm_scheduler.c``,
       ``settings_store.c``, ``clock_tz.c``
   * - ``wifi``
     - the board-blind WiFi manager (STA + scan) + the credentials
       store
     - ``wifi.c`` (board-blind; the P4's remote-C6 path is the
       board's, not the manager's)
   * - ``ui``
     - the LVGL screens, the fonts, the settings hub, the wheel
     - ``screens.c``, ``moon_ui.c``, ``ui_px.h``, ``Kconfig``
   * - ``board``
     - the board shim (the display init, the touch, the IMU, the
       RTC-i2c mux) + the board-select Kconfig
     - ``board_display.c``, ``Kconfig.projbuild``

The dependency direction is strictly **downward** (main → ui/clock/
wifi → board → BSP), and the *portable* core sits at the bottom with
no upward edges — which is why the host simulator can link it and the
gate can run it (Chapters 10, 17).

The host-side tooling
---------------------

The non-IDF build lives in ``host_sim/`` (CMake, the same core):

.. code-block:: bash
   :caption: The host tools (all in the project venv where Python is involved)

   cd host_sim && cmake -B build -S . && cmake --build build -j
   # build/moon_sim     the C-sim (the gate's C frames; --neutral for the
   #                    gate's physics config)
   # build/moon_bench   the performance bench (Chapter 19)
   host_sim/.venv/bin/python host_sim/check_gate.py    # the image gate
   host_sim/uisim/build.sh run                          # the UI simulator
   # (renders the real screens to PNG; --wheel for the keyboard,
   #  --panel 466/720 for the panel size)
   python3 docs/gen_media.py                            # the docs videos
   python3 docs/gen_doc.py                              # the project doc
   # (embeds every tracked source file + the docs/ inventory into
   #  docs/DOCUMENTATION.md — the standing rule's regeneration)

The ``uisim`` is the one host tool that runs the *device's UI code*
(not just the core): it links the real ``screens.c`` / ``moon_ui.c``
against an LVGL build and re-drives the indev with simulated
timestamps — which is how the wheel's long-press state machine (Chapter
21) and every screen's layout are verified without the panel.

The standing rule (the doc)
---------------------------

The project's standing rule (CLAUDE.md) closes the loop on this
chapter: **after every change** (code, Kconfig, screens, timings,
hardware facts, test system, build steps) the documentation is updated
— the prose in ``docs/doc_template.md`` is fixed, the generated
``docs/DOCUMENTATION.md`` is regenerated (``python3 docs/gen_doc.py``
embeds every tracked source file verbatim), and the three are
committed together. A change that does not update the doc is a
*half-finished* change. This book (``docs/book/``) is inventoried by
the generator but not embedded — the book is the *article*; the
generated doc is the *source record*.

The final chapter is the reference material: the glossary the book's
terms resolve to, and the sources (Chapter 25).
