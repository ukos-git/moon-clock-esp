Build system and Kconfig
========================

Two clock boards, one tree: the S3 build (the Waveshare
ESP32-S3-Touch-AMOLED-1.75) is the default target and the P4 build
(the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC) is a sibling with its own
partition table, its own dependency
lock, and a board-select Kconfig that gates the app's board-specific
code. The knob adds a **third build**: a sibling ESP-IDF project
under ``knob/`` with its own ``CMakeLists`` / partition table /
dependency lock / Kconfig, which links the clock's shared
``clock``/``wifi``/``moon``/``ui`` components through
``EXTRA_COMPONENT_DIRS`` (as individual dirs, never the parent
``components/``) and satisfies the component names it lacks with
same-named silent shims (``board``/``imu``/``esp_codec_dev``) —
:doc:`the Knob board <../devices/knob>`. This page is the build layer
— the three-file
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
     - the S3-only values (octal PSRAM at 80 MHz, 240 MHz core clock,
       CPU1 idle-WDT off, flush queue depth 1
       ``CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH=1`` — the display
       pipeline's NO_MEM fix). Carries **no** ``CONFIG_BOARD_S3=y``:
       the S3 is the board ``choice``'s default, while the P4 file
       does carry its board line (the asymmetry is documented on
       :doc:`the S3 board <../devices/s3>`; the buffer-height 25 lives
       in the vendored-BSP wrapper, not in this file)
   * - ``sdkconfig.defaults.esp32p4``
     - the P4's board line (``CONFIG_BOARD_P4=y``), the partition
       file (``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``),
       the rev v1.3 silicon (``CONFIG_ESP32P4_REV_MIN_100=y``),
       ``CONFIG_SPIRAM_SPEED_200M=y`` (the experimental-flag gotcha —
       without it the PSRAM *silently falls back to 20 MHz* and the
       DSI underruns, :doc:`the P4 port <../implementation/p4-port>`), the XIP-from-PSRAM + 256 KB L2
       cache (the 128 B-line M0 values), and the taller main stack

The knob does **not** participate in this split — it is its own
project with ``knob/sdkconfig.defaults`` and a gitignored live
``knob/sdkconfig`` (the sibling project's Kconfig carries its own
``choice BOARD``, default ``BOARD_KNOB``).

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
   :caption: The three builds (``<port>`` = the board you identified; see
             :doc:`the boards <../devices/index>`)

   source ~/esp/esp-idf/export.sh

   # S3 (466) — the default target
   idf.py build
   idf.py flash -p <port>

   # P4 4C (720) — own build dir + own dependencies.lock.p4 + own live
   # sdkconfig.p4 (-D SDKCONFIG is required while the root sdkconfig
   # pins IDF_TARGET=esp32s3 — see the P4 board page)
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 flash -p <port>

   # knob (360) — sibling project, own build dir + own
   # dependencies.lock.knob; app-flash preserves its SPIFFS credentials
   idf.py -C knob -B ../build/knob build
   idf.py -C knob -B ../build/knob app-flash -p <port>

Three constraints the commands encode:

- **The P4 build has its own build dir (``build/p4``), its own
  dependency lock (``dependencies.lock.p4``, gitignored) and its own
  live config (``sdkconfig.p4``, gitignored).** The
  component manager rewrites its lock on every configure and the two
  targets resolve *different* dependency sets (different BSPs +
  driver stacks) — a shared lock would oscillate. The root
  CMakeLists sets ``DEPENDENCIES_LOCK`` *before* ``project()`` (which
  runs the component manager) for the P4 build. The lock alone is not
  enough for the *config*: without ``-D SDKCONFIG=sdkconfig.p4`` the P4
  configure reads the root ``sdkconfig``, whose ``IDF_TARGET=esp32s3``
  pin makes ``targets.cmake`` abort with "Target … does not match
  currently selected IDF_TARGET". The flag creates ``sdkconfig.p4``
  from the defaults split on a fresh clone and stays correct after the
  first configure (it matches the cache entry), so every P4 command
  carries it.
- **The knob builds outside the clock's tree.** ``-B ../build/knob``
  resolves relative to the *invoking* directory — ``idf.py -C knob``
  first changes into ``knob/``, so the build dir lands next to the
  repo (``<parent of repo>/build/knob``), not inside it. Its live
  ``knob/sdkconfig`` and ``dependencies.lock.knob`` are gitignored
  like the clock's, and ``app-flash`` (not a full flash) is the
  convention that keeps the knob's SPIFFS Wi-Fi credentials.
- **Never run two ``idf.py`` builds in parallel** (a project rule):
  the builds share the component manager's cache, and parallel
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
is the third, :doc:`the binary-size chronology <../verification/binary>`):

.. code-block:: cmake
   :caption: The two data-partition flashes (abridged)

   # SPIFFS: the runtime-owned files (wifi_credentials.txt + alarm.txt)
   spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

   # The moon assets: the per-board pre-baked blob (gitignored), the
   # out/<target>/ dir picked by IDF_TARGET (the knob's own
   # CMakeLists replicates the guards for --target knob).
   set(MOON_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/moon/out/${MOON_ASSET_TARGET})
   set(MOON_ASSETS_BIN ${MOON_ASSET_DIR}/moon_assets.bin)
   # Guards before the flash, all FATAL_ERROR: MOON_ALBEDO_W must be an
   # even equirect width, the manifest's alb_w must equal it, and the
   # combined blob must fit the moon_assets row of the partition CSV the
   # build actually uses.
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
- **``moon_assets``** is a *generated* blob — local-only and gitignored,
  re-acquired end to end on a fresh clone by
  ``python3 assets/images/moon/get_assets.py`` (checksum-pinned NASA
  sources into ``moon-kit/``, the venv, then the bake —
  :doc:`the Python API <python-api>`:
  ``cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob``
  — one blob per board under ``out/<target>/``). The build *warns*
  (does not fail) when it is missing —
  the app boots without the moon, which is the failure mode a reader
  should recognize. When the blob *is* there, three consistencies are
  enforced as build-time ``FATAL_ERROR``s: an even ``MOON_ALBEDO_W``,
  the manifest's baked width equal to it, and the combined blob within
  the ``moon_assets`` row of the partition CSV the build actually uses
  (the knob's own CMakeLists replicates the guards for its target).
  The blob's layout
  (albedo at 0, then the int8 DEM, ``esp_partition_read`` at boot) is
  the ``moon_assets.h`` contract.

The whole chain — sources to flashed partition — as a sealed overview
plus the two sides opened (beginners meet this branch as the optional
amber box of the bring-up on
:doc:`the getting-started page <../getting-started>`):

.. graphviz:: ../_graphs/assets-pipeline.dot
   :alt: The moon asset pipeline sealed — LROC albedo plus LOLA DEM through the per-target CLI, out/<target>/ with blob, manifest and gen.c fallback, the three configure-time FATAL_ERROR guards, the flashed moon_assets partition
   :caption: The sealed chain — one dataset through the per-target CLI into ``out/<target>/``, past the referee guards, onto the ``moon_assets`` partition; both sides are opened below.
   :align: center

.. graphviz:: ../_graphs/assets-pipeline-bake.dot
   :alt: The bake side opened — LROC albedo as 16-bit TIFF and the LOLA DEM feeding the moon.assets CLI, constants.py as single source also generating the tracked moon_consts.h, and the three gitignored outputs blob, manifest, gen.c
   :caption: The bake side — LROC albedo loaded 16-bit via ``tifffile`` plus the LOLA DEM, one dataset for all three boards (only the per-board BAKE resolution differs); ``moon/constants.py`` is the single source of truth whose widths and DEM quantisation range also GENERATE the tracked ``components/moon/generated/<t>/moon_consts.h`` the firmware compiles (:doc:`the constants reference <moon-consts>`); ``out/<target>/`` is gitignored and holds the trio — the blob (albedo u8 at offset 0, then the int8 DEM), the manifest (``alb_w``, ``combined_bytes``, DEM geometry — the guard's input AND its witness) and the compiled-in fallback ``moon_assets_gen.c`` (also a moonmedia staleness input).
   :align: center

.. graphviz:: ../_graphs/assets-pipeline-guards.dot
   :alt: The referee side opened — the Kconfig albedo width against the manifest and the partition row as three FATAL_ERROR guards, idf.py flash landing the row with the missing blob as warning only, and the host image gate grading the same bytes
   :caption: The referees — the Kconfig widths (864 on S3/knob, 2048 on P4 — the ``default … if BOARD_P4`` pattern) against the manifest, against the ``moon_assets`` row of the partition CSV the build ACTUALLY uses (``PARTITION_TABLE_CUSTOM_FILENAME`` honoured, else ``partitions.csv``). "All three mismatches have historically surfaced as obscure build/boot failures … so they are FATAL_ERRORs here", while a missing blob stays a WARNING — the app boots without the moon. The knob's ``CMakeLists`` replicates the trio for ``--target knob``; the host image gate grades the SAME bytes the device boots (``check_gate --target s3|p4|knob``, exit 2 on a sim built against the other board's texture).
   :align: center

The same rows counted in bytes — every data row's occupancy, the
shipped blobs against their ceilings, and the what-ifs the CSV
comments keep (re-derived at figure time) — are :ref:`the storage
ladder <storage-ladder-perf>`.

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
   settings store, :doc:`the clock and sleep machine <../implementation/clock-sleep>`, is the pattern's main user: every
   runtime-tunable value — the idle timeout, the sleep countdowns, the
   animation flags — is a ``#ifndef``-seeded getter, never a
   ``#define``.)

2. **Never clang-format a Kconfig file.** The pre-commit hook runs
   clang-format on C/H only — the hook's own filter keeps it safe —
   but a manual ``clang-format Kconfig`` mangles the file (the
   ``menuconfig`` syntax is not C). The rule is *never run it on
   extensionless files*.

   The framework is installed at the **commit-msg** stage
   (``pre-commit install -t commit-msg``): its file-format hooks see
   no files there and skip, while two local commit-msg hooks police
   the message itself. The first (``.githooks/cz-commit-msg``,
   ``stages: [commit-msg]``) runs the commitizen checker (``cz check
   --commit-msg-file $1 -l 100``), so every message must match the
   ``cz_conventional_commits`` schema — a ``type`` (with an optional
   ``(scope)``) and a colon, a ≤100-char subject, then a blank line
   before any body — which is the style the whole history was
   rewritten to in 2026-11-24; ``git-cz`` / ``cz commit`` give the
   matching interactive prompt. The limit is passed with ``-l``
   because this commitizen version shadows the ``message_length_limit``
   config key when the flag is omitted. The second (``.githooks/commit-msg``)
   rejects any commit message carrying a co-authorship attribution
   line (case-insensitive, any position — the history's trailers were
   scrubbed on 2026-11-22; new commits must stay clean the same
   way). A *pre-commit*-stage hook cannot see the message;
   installing the pre-commit stage (clang-format etc. on staged
   files) is a separate deliberate decision.

3. **Board-conditional code gates on ``CONFIG_BOARD_P4`` /
   ``CONFIG_BOARD_S3``** (the board-select Kconfig in
   ``components/board/Kconfig.projbuild``) — *not* on
   ``CONFIG_IDF_TARGET``. The board is the app's concept; the target
   is the build's. The mismatch guard (above) makes the two agree at
   build time, so the app can gate on the board and never see a
   target/board split. The P4's moon-geometry choices (``MOON_PX``
   400/650, ``MOON_R`` 200/325, the 25/130-row display strip pitch) are the
   main consumers (:doc:`the P4 port <../implementation/p4-port>`).

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
gate can run it (:doc:`the architecture <../implementation/architecture>` and :doc:`the image gate <../verification/image-gate>`).

The knob's build reuses the ``moon``/``clock``/``wifi``/``ui`` rows
verbatim through ``EXTRA_COMPONENT_DIRS`` (individual dirs, never the
parent ``components/`` — the component manager excludes components by
name only, so the names must be owned one by one), and stands in
**same-named components** where the shared requirements name something
the board lacks: its own ``board`` shim (SH8601 display + touch
init), an ``imu`` **stub** (the board has no IMU), and an empty
``esp_codec_dev`` (no codec). Its own ``knob_*`` components add the
dial, battery gauge, power ladder, time handling and the render seam
(:doc:`the Knob board <../devices/knob>`).

The seam, in pictures
---------------------

The pages above state the machine in prose — three projects, one repo,
two CMake passes, literal requirements, silent shims. This is the
sealed overview plus four detail sketches; every quoted line was read
out of ``CMakeLists.txt``, ``knob/CMakeLists.txt`` and the shared
components' ``CMakeLists`` at sketch time:

.. graphviz:: ../_graphs/build-seam.dot
   :alt: The build seam — the knob re-using the clock's four shared components through individually-listed EXTRA_COMPONENT_DIRS, same-named silent shims, and KNOB_BOARD branches confined to SRCS because the early requirements pass sees neither project variables nor CONFIG_ symbols
   :caption: The seam, drawn — the blue edge from the knob project is the seam itself: ``EXTRA_COMPONENT_DIRS`` spelled dir-by-dir, **never** the parent ``components/`` (the rule the shims sketch opens); the two-pass band is the reason for every rule the details carry.
   :align: center

.. graphviz:: ../_graphs/build-seam-projects.dot
   :alt: The three projects opened — the root clock project spawning the P4 sibling that excludes the vendored S3 BSP and pins its own lock before project(), and the knob sibling whose KNOB_BOARD is set before project() because the component manager runs there, pre-Kconfig
   :caption: The projects — the P4 is a sibling build of the root (``-D IDF_TARGET=esp32p4 -B build/p4``) whose target block ``EXCLUDE_COMPONENTS`` the vendored S3 BSP (manifest targets: ``[esp32s3]``) "out of the P4 build AND of the component manager's local-manifest list" (:16-21) and pins ``DEPENDENCIES_LOCK dependencies.lock.p4`` **before** ``project()`` (:27-28, "which runs the component manager", :24-26); the tracked root ``dependencies.lock`` stays the S3's. The knob likewise owns its build dir (``-C knob -B ../build/knob`` — OUTSIDE the repo), its gitignored ``dependencies.lock.knob`` (:47) and its sdkconfig; ``set(KNOB_BOARD ON)`` sits at :23, BEFORE ``project(knob)`` (:49) — "A PROJECT variable, not a ``CONFIG_`` symbol: the component manager resolves requirements BEFORE Kconfig runs, and ``CONFIG_`` variables are unset in that first pass" (:20-22).
   :align: center

.. graphviz:: ../_graphs/build-seam-passes.dot
   :alt: The two CMake passes opened — the early requirements pass that sees neither project variables nor CONFIG_ symbols and forces literal REQUIRES, and the target pass where KNOB_BOARD branches live, with the four dropped ui translation units and the fonts_knob flip
   :caption: The passes — the EARLY one (``build.cmake:664``, ``cmake -P component_get_requirements.cmake``) evaluates every component ``CMakeLists`` with NEITHER project variables NOR ``CONFIG_`` symbols, so ``REQUIRES``/``PRIV_REQUIRES`` are spelled out unconditionally on purpose and every name must resolve in all three projects (the ui comment :36-43); an ``if()`` hiding a missing component is invisible in that pass, which is what the silent ``esp_codec_dev`` shim pays for. The target pass is where ``KNOB_BOARD`` branches live, and only in ``SRCS``/``INCLUDES`` (``CONFIG_BOARD_P4`` "reads false in that pass (harmless: SRCS are only consumed by the second, target-owning pass)") — the ui component's four dropped translation units (``alarm_audio.c``, ``demo.c``, ``moon_ui.c``, ``moon_tfcache.c``) and the ``fonts_knob`` flip (fonts/ 96-48-32, fonts_p4/ 148-74-50, fonts_knob/ 74-37-25 — identical ``lv_font_t`` symbol names, "so fonts.h and every consumer are unchanged").
   :align: center

.. graphviz:: ../_graphs/build-seam-shims.dot
   :alt: The silent shims opened — the knob's board shim with its SH8601 init and BOARD_KNOB-default choice, the imu stub answering a fixed upright, the esp_codec_dev component that is only a CMakeLists, and the rule that the parent components/ directory is never added because the component manager excludes by name
   :caption: The shims — who names what: ``clock`` REQUIRES board (:5, "esp_io_expander board wifi …"), ``ui`` REQUIRES imu, board and ``esp_codec_dev`` (:55-59), while ``moon`` (:46) and ``wifi`` (:4) name NEITHER — the portable core sits below the board. The knob answers with same-named silent components: ``board`` (SH8601 display + touch init; its Kconfig carries ``choice BOARD`` default ``BOARD_KNOB``, ``BOARD_S3``/``BOARD_P4`` selectable-but-unused "so the shared Kconfig's if BOARD_P4 clauses parse" — the clocks answer the same name with the real component, default ``BOARD_S3``), ``imu`` (``imu_stub.c``: a fixed upright, no chip) and ``esp_codec_dev`` (a ``CMakeLists`` and NOTHING ELSE — on the clocks the vendored real component owns that name). Why never add the parent ``components/``: IDF 5.5.1 excludes components by name only — adding it would register the *clock's* ``board``/``imu`` and collide, and EXCLUDE-by-name could not fix that.
   :align: center

.. graphviz:: ../_graphs/build-seam-guards.dot
   :alt: The configure guards opened — the root's even-width, manifest-width and partition-fit FATAL_ERROR trio, the missing blob as warning only, the knob's stricter manifest-must-exist replicant, and the per-target sdkconfig.defaults as the source of CONFIG_ symbols
   :caption: The guards — the root's even-width / manifest-width / partition-fit ``FATAL_ERROR`` trio ("All three mismatches have historically surfaced as obscure build/boot failures … so they are FATAL_ERRORs here"; the fit message names the KiB, "they need >= X K"), and the knob's shape-replicant that drops the Kconfig-width check ("the knob has no Kconfig mirror; its single truth is the pipeline-written manifest + header") and is *stricter* about existence: a missing manifest kills the knob build, because a knob without its blob has no moon at all (no SD socket). ``CONFIG_`` symbols come instead from the per-target ``sdkconfig.defaults`` files (``CONFIG_BOARD_P4=y``, ``PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``), with the root's target/board mismatch guard as referee (:77-88).
   :align: center

The seam's *runtime* side — ``knob_moon_render.c`` implementing
``moon_ui.h``, the reentrant mutex, the deep-sleep ladder — is
:doc:`the knob display implementation <../implementation/knob-display>`;
the blob the fit guard prices is the asset-pipeline figure in
*"The flash-data partitions"* above, counted in bytes at
:ref:`the storage ladder <storage-ladder-perf>` below.

The seam has a ledger: how many parts each image assembles from
(the shared-4 under every bar, the registry set each lock resolves),
what it drops on the knob side (the four ``UI_BOARD_SRCS``
translations, 5,178 lines) against what answers its three board-side
names (44 lines and one CMakeLists), and which four ``REQUIRES``
literals oblige the early pass at all — counted in **parts** at
:ref:`the build census <census-ladder-perf>`. The same seam counted
in **choices** — the 69/57/85 menuconfig entries the three menus
define, the 29 inherited options that stay silent in the knob build,
the dead letters, the configure-only guards and the nine keys of
``settings.txt`` — is
:ref:`the configuration ladder <config-ladder-perf>`. And the table
transposed — **how many of the twelve link images compile each
source**: the 17 portable-core TUs alone at 12/12, the 412-TU vendored
LVGL stopping at 6 (the three screenshot rigs cost 1,236 vendored
compiles the gate and bench never pay), the four dropped ``UI_BOARD_SRCS``
at 2, and the knob's fifteen own files at exactly 1 apiece — is
:ref:`the sharing ladder <sharing-ladder-perf>`, whose parser dies if
the three core source lists ever disagree name-for-name.

The host-side tooling
---------------------

The non-IDF build lives in ``host_sim/`` (CMake, the same core). The two
Python venvs the tools run in are bootstrapped from the tracked pin
files: ``docs/.venv`` from ``docs/requirements.txt``, and
``host_sim/.venv`` (gate + NASA legs) from the moon pipeline's
``assets/images/moon/requirements.txt``::

   python3 -m venv docs/.venv && docs/.venv/bin/pip install -r docs/requirements.txt
   uv venv host_sim/.venv && uv pip install --python host_sim/.venv/bin/python \
       -r assets/images/moon/requirements.txt

.. code-block:: bash
   :caption: The host tools (all in the project venv where Python is involved)

   cd host_sim && cmake -B build -S . && cmake --build build -j
   # build/moon_sim     the C-sim (the gate's C frames; --neutral for the
   #                    gate's physics config)
   # build/moon_bench   the performance bench (:doc:`performance <../verification/performance>`)
   python3 assets/images/moon/get_assets.py --refs   # one-shot NASA fetch
   #  (sha256-pinned kit + mooninfo + the gate/quarter reference frames;
   #  runs the bake too — --target narrows it; sources cache under moon-kit/)
   python3 assets/images/moon/get_assets.py --fonts  # the four foundry faces
   #  (assets/fonts/: Montserrat Regular/Bold from the designer's repo at a
   #  pinned commit + Font Awesome 7 Free from the 7.3.1 release zip — all
   #  sha256-pinned; gen_perf_glyphs.py refuses to run without them)
   host_sim/.venv/bin/python host_sim/check_gate.py --target s3    # the image
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4    # gate — all
   host_sim/.venv/bin/python host_sim/check_gate.py --target knob  # three boards
   # (the gate exits 2 on a stale sim binary built against the other
   #  target's texture — the --target must match the build it checks)
   host_sim/uisim/build.sh run                          # the UI simulator
   # (renders the real screens to PNG; --wheel for the keyboard,
   #  --dial <screen> for the knob's dial-cursor shot,
   #  --panel 466/720/360 for the three family panels)
   docs/.venv/bin/sphinx-build -b media docs docs/_build/media
   # (THE media builder — regenerates the site's ENTIRE binary media set:
   #  the scripted figures (every docs/tools/gen_*.py + the node
   #  isomorphic pipeline gen_figures.js -> assemble_figures.py and
   #  gen_bake_intermediates.js -> gen_bake_strip.py), all three boards'
   #  uisim screenshots + the --wheel/--dial captures, the annotated
   #  callouts (enclose.py over docs/ui/labels/*.json), the device
   #  composites; MEDIA_VIDEOS=1 adds the film (docs/gen_media.py) and
   #  MEDIA_NASA=1 the NASA answer-key art (a gate run + nasa_figs.py +
   #  gen_nasa_quarters.py — needs the gitignored final_400x400 key).
   #  The media is gitignored BUILD OUTPUT — the generators are the
   #  sources of truth, never the pixels — so -b html's 0-warning gate
   #  presumes a media run on the tree. The vendor-leg figures
   #  (perf_bus, perf_silicon) parse managed_components/, which holds
   #  the LAST-resolved target's set — reconfigure the P4 build for the
   #  gt911 leg. The glyph census (gen_perf_glyphs.py) needs the four
   #  foundry faces under assets/fonts/ — one-time
   #  ``get_assets.py --fonts``.)
   python3 docs/gen_media.py                            # the docs film alone
   docs/.venv/bin/sphinx-build -b html docs docs/_build/html
   # (the site build — 0 warnings is the gate, the standing rule; the
   #  moonmedia staleness check warns when any generated media is
   #  missing or older than its render inputs)
   make -C docs serve                             # browse it over http
   # (one origin, so furo's theme toggle survives navigation — under
   #  file:// Firefox partitions storage per document, see the section
   #  below)

The ``uisim`` is the one host tool that runs the *device's UI code*
(not just the core): it links the real ``screens.c`` / ``moon_ui.c``
against an LVGL build and re-drives the indev with simulated
timestamps — which is how the wheel's long-press state machine
(:doc:`the round keyboard <../keyboard>`) and every screen's layout are
verified without the panel.

The reader's theme under ``file://``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The site is furo: the reader's Light/Dark/Auto pick lives in
``localStorage`` and furo's top-of-body reader re-reads it on every
page. Over http that is one shared origin and just works; opened
straight from the filesystem it does not — Firefox's stock default
(``privacy.file_unique_origin``) gives every ``file://`` document its
own storage partition, so a page under
``docs/_build/html/…html`` starts each navigation back at ``auto``
and follows the OS ("some pages auto-switch to bright, the same the
other way around"; headless automation profiles flip that pref, which
is why the loss looks unreproducible under a test driver). The answer
is ``docs/_static/theme_sync.js``: it rides the choice in the URL as
``?m=``, stamps it onto every in-site link the reader follows, seeds
it back into ``localStorage`` before furo's reader runs (head-injected
through a ``docs/_templates/page.html`` ``extrahead`` override —
furo's page.html extends base.html directly, its layout.html is a
decoy, and every other furo template extends page.html; furo also
renders every ``html_js_files`` entry at the body's end, too late for
the seed), and
re-stamps the address bar when the toggle is clicked — state the
storage partition cannot lose. The same click also dispatches
``furo:theme-change``, which the vendored furo never dispatches
itself: that missing event is why the mermaid diagrams used to sit in
the old theme after a toggle. A reader who prefers to skip the
mechanism entirely browses through ``make -C docs serve`` (one http
origin at ``127.0.0.1:8123``, ``DOCS_PORT=`` to move it, storage shared
as designed) — over http the script degenerates to re-affirming what
the storage already knows.

The standing rule (the doc)
---------------------------

The project's standing rule (CLAUDE.md) closes the loop on this
page: **after every change** (code, Kconfig, screens, timings,
hardware facts, test system, build steps) the documentation is
updated — the affected RST pages are fixed, and if screens, UI
geometry or a figure changed, ``sphinx-build -b media`` regenerates
them (all three panels' screenshots, the annotated callouts, the
figures — every embedded bitmap is a gitignored build output, never
committed; the per-screen pages live in
:doc:`the user interface <../ui/index>`) — and the doc change is
committed with the code change (the media itself never is). A change
that does not update the doc is a *half-finished* change.

The docs drift check
~~~~~~~~~~~~~~~~~~~~

Half of the standing rule — *keeping* the reference tables honest —
is automated by ``docs/tools/check_docs_drift.py`` (run it with the
docs venv: ``docs/.venv/bin/python docs/tools/check_docs_drift.py``;
exit 0 = green, exit 1 prints one ``DRIFT:`` line per disagreement).
The reference fragments carry no generator — they are *hand-kept-in-sync*
files, which is exactly the situation that drifts silently — so the
checker parses both sides and compares:

1. the ``kconfig-*.inc`` fragments against the live Kconfig sources
   (every documented key exists, every documented default is one of
   the source's first-match-wins ``default`` lines, and the reverse
   coverage pass catches *undocumented* symbols),
2. ``moon-consts.inc`` against the generated ``moon_consts.h`` header
   plus the core's numeric ``#define``/``const`` homes,
3. the ``partitions-*.inc`` fragments against the three CSVs (the
   decimal and the hex spelling must both agree with the CSV),
4. the :doc:`UI reachability matrix <../ui/index>` against the
   ``SCREEN_ID_*`` enum, the page files, and the screenshots each page
   embeds (✓ rows need their annotated figures, — rows must not embed
   the panel they claim unreachable), and
5. the ``binary.rst`` current-size rows against the binaries on disk.

The figure on :doc:`the binary-size page <../verification/binary>`
completes the chain the other direction: it *parses* that same table
(``docs/tools/gen_binary_figs.py``), so checker, page and figure can
never disagree about the flash budget.

The reference section is where the doc's tables come from: the
glossary the text's terms resolve to, and the sources
(:doc:`the glossary <../glossary>`, :doc:`Sources and references <sources>`).
