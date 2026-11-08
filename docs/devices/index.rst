The boards
==========

The project targets two Waveshare boards. This page covers what both
share — the ESP-IDF toolchain, how to tell which USB port is which
board, and the build/flash conventions — and the per-board pages
cover the hardware, the pre-configured defaults, and the full
compile-time settings. A third Waveshare board, the
ESP32-S3-Knob-Touch-LCD-1.8, carries its own standalone
moon-display firmware as a sibling project
(:doc:`the Knob board <knob>`).

.. toctree::
   :maxdepth: 1

   s3
   p4
   knob

The two targets
---------------

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

:doc:`The S3 board <s3>` is the product's face and the default build
target; :doc:`the P4 4C <p4>` is the second board, ported as a full
sibling. The hardware difference that matters most to the firmware:
the S3 carries a PCF85063A RTC behind a TCA9554 IO expander (so the
deep-sleep countdown and the alarm arm into the PCF's hardware
registers), while the 4C carries **no** RTC IC or IO expander (the
time base is the 32 kHz crystal RTC timer + SNTP, and the countdown /
alarm run on a software ``esp_timer`` one-shot).

Installing the toolchain
------------------------

Both boards build with **ESP-IDF 5.5.1**. Install it once (the project
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
the procedure instead; both boards present a serial adapter the host
claims as a ``/dev/ttyACM*`` node:

1. **Plug in *one* board at a time** and watch what node appears::

       lsusb                        # a QinHeng / Espressif serial adapter
       dmesg | tail                 # the ttyACM node just claimed

   The node that appears when you plug the board in (and disappears
   when you unplug it) is that board's port. Do this for each board
   separately so you learn both.
2. **Confirm which chip sits on a port** before flashing, so you
   never flash the wrong board::

       esptool.py --port <port> chip_id

   ``ESP32S3`` identifies the S3 board; ``ESP32-P4`` identifies the
   4C. (``esptool.py`` is on ``PATH`` after ``export.sh``.) Run this
   against the port you are about to flash — it is the single check
   that catches a stale port guess.
3. Keep a scratch note of the mapping *for this plug session* (it does
   not survive a re-plug) and re-run step 2 if in doubt.

Build and flash conventions
---------------------------

.. graphviz:: ../_graphs/build-flash.dot
   :alt: Build and flash sequence
   :align: center

The conventions both boards share:

- **Build sequentially.** Never run two ``idf.py`` builds in parallel
  — the two targets share the component manager's cache and parallel
  configures corrupt each other's lock resolution. One ``idf.py`` at a
  time.
- **Check the binary size after every build** against the ``factory``
  partition (2048 K on the S3, 9216 K on the P4 —
  :doc:`the partition tables <../reference/partitions>`). The S3's 2 MB
  ``factory`` is the binding constraint; the binary-size chronology
  (:doc:`the binary-size chronology <../verification/binary>`) tracks it.
- **``idf.py flash -p <port>`` flashes all three images** — the
  ``factory`` app, the ``storage`` SPIFFS (baked from ``spiffs/`` on
  every build), and the ``moon_assets`` blob
  (:doc:`the build system <../reference/build-system>`).
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
   :alt: Three-file sdkconfig merge order
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

All three are **hand-curated**: ``idf.py save-defconfig`` writes only
the shared file and would clobber the board lines, so the files are
kept by hand — a stray board line in the wrong file is a build-time
``FATAL_ERROR`` (the root ``CMakeLists`` target/board guard), not a
runtime surprise. Each per-board page lists its own file's contents
and the full compile-time settings; the Kconfig rules the whole
project lives by are in :doc:`the build system <../reference/build-system>`.
