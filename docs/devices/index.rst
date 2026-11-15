The boards
==========

The project targets three Waveshare boards. This page covers what all
three share — the ESP-IDF toolchain, how to tell which USB port is
which board, and the build/flash conventions — and the per-board
pages cover the hardware, the pre-configured defaults, and the full
compile-time settings. The third board, the
ESP32-S3-Knob-Touch-LCD-1.8 "knob", is a sibling project that links
the clock's shared ``clock``/``wifi``/``moon``/``ui`` components
behind its own board shim and runs the moon display — the dial,
battery gauge and real deep sleep are its additions
(:doc:`the Knob board <knob>`).

.. toctree::
   :maxdepth: 1

   s3
   p4
   knob

The three targets
-----------------

.. list-table::
   :header-rows: 1
   :widths: 18 20 31 31

   * - Board
     - Target
     - Panel
     - Time base
   * - ESP32-S3-Touch-AMOLED-1.75
     - ``esp32s3`` (default)
     - 466×466 CO5300 QSPI AMOLED
     - PCF85063A RTC (hardware alarm)
   * - ESP32-P4-WIFI6-Touch-LCD-XC (4C)
     - ``esp32p4``
     - 720×720 JD9365 MIPI-DSI
     - 32 kHz crystal + SNTP (software alarm)
   * - ESP32-S3-Knob-Touch-LCD-1.8 (knob)
     - ``esp32s3``, sibling project under ``knob/``
     - 360×360 SH8601 QSPI backlit LCD
     - SNTP + NVS time snapshot; RTC **TIMER** arms the deep-sleep /
       alarm wake (never an early ring)

:doc:`The S3 board <s3>` is the product's face and the default build
target; :doc:`the P4 4C <p4>` is the second board, ported as a full
sibling; :doc:`the knob <knob>` is the family's third board, built as
its own project that links the clock's shared components. The
hardware difference that matters most to the firmware:
the S3 carries a PCF85063A RTC behind a TCA9554 IO expander (so the
deep-sleep countdown and the alarm arm into the PCF's hardware
registers), while the 4C carries **no** RTC IC or IO expander (the
time base is the 32 kHz crystal RTC timer + SNTP, and the countdown /
alarm run on a software ``esp_timer`` one-shot). The knob likewise has
no RTC IC — it sleeps for real (``esp_deep_sleep_start``) and arms the
wake on the SoC's RTC TIMER.

Installing the toolchain
------------------------

All three builds use **ESP-IDF 5.5.1**. Install it once (the project
expects the framework checkout at ``~/esp/esp-idf``):

1. Follow the ESP-IDF install guide for your platform to get the
   ``esp-idf`` repo + the matching toolchains (the ``idf_tools``
   installer fetches the Xtensa/RISC-V compilers, OpenOCD, and
   ``esptool`` for the pinned IDF version).
2. In every shell you build in, ``source`` the export script (it puts
   ``idf.py``, ``esptool.py`` and the toolchain on ``PATH``)::

       source ~/esp/esp-idf/export.sh

3. Verify: ``idf.py --version`` reports 5.5.1 and ``idf.py set-target
   esp32s3`` (or ``esp32p4``) configures without error.

The P4 target additionally needs the ESP-Hosted / ``esp_wifi_remote``
component pair (the remote C6 over SDIO) and the two vendored BSPs —
the component manager pulls these from the ``idf_component.yml``
manifests on the first ``set-target esp32p4`` (the three ``int``
``ESP_HOSTED_HOST_USB_*_MANIFEST`` guard keys,
:doc:`board selection Kconfig <../reference/kconfig-board>`, keep the
component manager's version solver from aborting while it does). The
P4 build uses its own build directory (``build/p4``) and its own
dependency lock (``dependencies.lock.p4``) so the two targets'
different BSP/driver sets do not oscillate
(:doc:`the build system <../reference/build-system>`).

Identifying the boards
----------------------

Never hard-code which port is which board — the mapping changes with
USB topology (which board is plugged in, hub order, a re-plug). Teach
the procedure instead; the S3 and P4 present a serial adapter the
host claims as a ``/dev/ttyACM*`` node, and so does the knob's app
MCU *when the Type-C plug sits in the S3 orientation* (flipping the
plug switches the receptacle to the board's second MCU — the
U4WDH's CH340-family bridge, which shows up as ``/dev/ttyUSB*``
instead; :doc:`the Knob board <knob>`):

1. **Plug in *one* board at a time** and watch what node appears::

       lsusb                        # a QinHeng / Espressif serial adapter
       dmesg | tail                 # the ttyACM node just claimed

   The node that appears when you plug the board in (and disappears
   when you unplug it) is that board's port. Do this for each board
   separately so you learn both.
2. **Confirm which chip sits on a port** before flashing, so you
   never flash the wrong board::

       esptool.py --port <port> chip_id

   ``ESP32S3`` identifies the S3 board **or the knob** — both are
   ESP32-S3 with 16 MB flash, so the chip report alone cannot tell
   them apart; disambiguate by step 1 (which physical board you just
   plugged in, and on the knob which plug orientation you used — the
   S3 side answers, the U4WDH side reports ``ESP32-U4WDH``) and by
   label; ``ESP32-P4`` identifies the 4C. (``esptool.py`` is on
   ``PATH`` after ``export.sh``.) Run this against the port you are
   about to flash — it is the single check that catches a stale port
   guess.
3. Keep a scratch note of the mapping *for this plug session* (it does
   not survive a re-plug) and re-run step 2 if in doubt.

Build and flash conventions
---------------------------

.. graphviz:: ../_graphs/build-flash.dot
   :alt: Build and flash sequence — export, assets, config merge, build, size check, flash, monitor
   :caption: The unrolled commands: ``source ~/esp/esp-idf/export.sh``, the asset bake (``cd assets/images/moon && .venv/bin/python -m moon.assets`` — its outputs are local and gitignored), the size budget (2048 K S3 — the binding constraint — / 9216 K P4 / 4096 K knob), and the port rule that closes the chain: flash the ``chip_id``-verified port, never a remembered one.
   :align: center

The chain above is one leg of the journey. The whole first-bring-up —
the host shell, the optional asset branch behind its configure-time
guards, the ``chip_id`` gate no port may skip, the two command shapes
separated by the target/board referee, the boot sync that resolves
before the panel exists, and the provisioning chain ending in the
credential file — is drawn on
:doc:`the getting-started page <../getting-started>` as a black-box
overview whose first-boot and provisioning legs open into their own
detail sketches.

The conventions the three builds share:

- **Build sequentially.** Never run two ``idf.py`` builds in parallel
  — the builds share the component manager's cache and parallel
  configures corrupt each other's lock resolution. One ``idf.py`` at a
  time.
- **Check the binary size after every build** against the ``factory``
  partition (2048 K on the S3, 9216 K on the P4, 4096 K on the knob —
  :doc:`the partition tables <../reference/partitions>`). The S3's 2 MB
  ``factory`` is the binding constraint; the binary-size chronology
  (:doc:`the binary-size chronology <../verification/binary>`) tracks it.
- **``idf.py flash -p <port>`` flashes all three images** — the
  ``factory`` app, the ``storage`` SPIFFS (baked from ``spiffs/`` on
  every build), and the ``moon_assets`` blob
  (:doc:`the build system <../reference/build-system>`).
- **The knob builds and flashes as its own project** —
  ``idf.py -C knob -B ../build/knob build`` (its build dir lands
  *outside* the clock's tree) and
  ``idf.py -C knob -B ../build/knob app-flash -p <port>`` — the
  ``app-flash`` is what preserves the knob's SPIFFS Wi-Fi
  credentials; a full flash resets them
  (:doc:`the Knob board <knob>`).
- **A full flash clobbers the runtime SPIFFS state** (``alarm.txt`` /
  ``wifi_credentials.txt`` / ``settings.txt`` are written back by the
  app, so a re-flash resets them to the ``spiffs/`` defaults —
  :doc:`the SPIFFS store <../reference/spiffs-store>`).
- **Flash the identified port, never a remembered one.** Re-run the
  ``chip_id`` check (above) after any re-plug.

Pre-configured defaults
-----------------------

The build configuration is a three-file ``sdkconfig.defaults`` split —
one shared file plus a per-target file that the build system
auto-appends (later file wins):

.. graphviz:: ../_graphs/sdkconfig-merge.dot
   :alt: Three-file sdkconfig merge order — shared file, per-target file, live config, board guard
   :caption: kconfgen auto-appends the per-target file when it exists and the LATER file wins; the live files are the gitignored ``sdkconfig`` / ``sdkconfig.p4``, and a hand-edited live pin beats every ``default`` line — so a changed default needs the stale pin deleted. The file contents are itemized in the list below; the knob carries its own ``knob/sdkconfig.defaults`` that never sees this split.
   :align: center

- ``sdkconfig.defaults`` — the shared values (LVGL 9.6 setup, the
  component-manager manifest, ``CONFIG_PARTITION_TABLE_CUSTOM=y``, the
  clock/UI defaults, target = ``esp32s3`` so a plain build guesses the
  S3).
- ``sdkconfig.defaults.esp32s3`` — the S3's board-specific values
  (octal PSRAM 80 MHz, 240 MHz core, CPU1 idle-WDT off, flush queue
  depth 1).
- ``sdkconfig.defaults.esp32p4`` — the P4's (``CONFIG_BOARD_P4=y``,
  ``partitions_p4.csv``, rev v1.3, PSRAM 200 MHz + the experimental
  flag, XIP + 256 KB L2, PCF85063A off, the C6-over-SDIO pins).
- ``knob/sdkconfig.defaults`` — the knob's **own** defaults: the
  sibling project never sees the root split, and its live
  ``knob/sdkconfig`` is gitignored like the clock's
  (:doc:`the Knob board <knob>`).

The root files are **hand-curated**: ``idf.py save-defconfig`` writes
only the shared file and would clobber the board lines, so the files
are kept by hand — a stray board line in the wrong file is a
build-time ``FATAL_ERROR`` (the root ``CMakeLists`` target/board
guard), not a runtime surprise. Each per-board page lists its own
file's contents and the full compile-time settings; the Kconfig rules
the whole project lives by are in :doc:`the build system <../reference/build-system>`.
