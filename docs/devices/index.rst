The boards
==========

.. brief:
   Reader: someone about to buy or set up a board for the clock, comfortable in a terminal.
   They already know: the getting-started tutorial, or at least what building and flashing mean.
   After this page they can: pick a board, install the toolchain, tell which USB port is which board, and build and flash it.
   Page type: how-to (with a short comparison up front).

The clock runs on three Waveshare boards.
This page helps you choose one, then covers what all three share: the ESP-IDF toolchain, how to tell which USB port is which board, and the build and flash conventions.
Each board has its own reference page with the pin map, the pre-configured defaults and the full compile-time settings.

.. toctree::
   :maxdepth: 1

   s3
   p4
   knob

Choose a board
--------------

If you have no board yet, buy the S3 AMOLED board.
It is the default build target and the only board with a real-time clock (RTC) chip.

.. list-table::
   :header-rows: 1
   :widths: 22 26 26 26

   * -
     - :doc:`S3 AMOLED <s3>` (default)
     - :doc:`P4 4C <p4>`
     - :doc:`Knob <knob>`
   * - Waveshare name
     - ESP32-S3-Touch-AMOLED-1.75
     - ESP32-P4-WIFI6-Touch-LCD-XC
     - ESP32-S3-Knob-Touch-LCD-1.8
   * - Build target
     - ``esp32s3`` (default)
     - ``esp32p4``
     - ``esp32s3``, the same tree with ``-D KNOB_BOARD=ON``
   * - Screen
     - 1.75-inch, 466×466 CO5300 QSPI AMOLED
     - 4-inch, 720×720 JD9365 MIPI-DSI IPS
     - 1.8-inch, 360×360 SH8601 QSPI backlit LCD
   * - Input
     - touch (CST9217), tilt (QMI8658C motion sensor)
     - touch (GT911)
     - touch (CST816S) and a rotary dial
   * - Time base
     - PCF85063A RTC (hardware alarm)
     - 32 kHz crystal + SNTP (software alarm)
     - SNTP + NVS time snapshot; the internal RTC timer arms the deep-sleep and alarm wake (never an early ring)
   * - Wi-Fi
     - on the S3
     - a remote ESP32-C6 over SDIO
     - on the S3
   * - TF card
     - yes (SPI, 1-bit)
     - yes (SDMMC, 4-bit)
     - no socket
   * - Power
     - USB in this build
     - USB (a VBAT header backs the 32 kHz crystal)
     - built-in battery, real deep sleep
   * - Missing
     - a true deep-sleep wake (the RTC alarm line reaches no wake input)
     - RTC chip, IO expander, motion sensor, deep-sleep wake
     - RTC chip, motion sensor, sound (the alarm is silent)

The difference that matters most to the firmware is the time base.
The S3 carries a PCF85063A RTC behind a TCA9554 IO expander, so the deep-sleep countdown and the alarm arm into the PCF's hardware registers.
The 4C has no RTC chip and no IO expander.
It keeps time with the 32 kHz crystal RTC timer and SNTP, and the countdown and alarm run on a software ``esp_timer`` one-shot.
The knob also has no RTC chip.
It sleeps for real (``esp_deep_sleep_start``) and arms the wake on the SoC's RTC timer.

The S3 is the product's face.
The 4C is a full sibling port with the largest screen and the fastest chip.
The knob is the family's third board.
It builds as its own project that links the clock's shared ``clock``, ``wifi``, ``moon`` and ``ui`` components behind its own board shim, and it adds the dial, the battery gauge and real deep sleep (:doc:`the Knob board <knob>`).
The :doc:`housing </housing>` page describes a 3D-printed case that seats all three.

Install the toolchain
---------------------

All three builds use ESP-IDF 5.5.1, with the framework checkout at ``~/esp/esp-idf``.
You install it once:

1. Follow the ESP-IDF install guide for your platform to get the ``esp-idf`` repository and the matching toolchains.
   The ``idf_tools`` installer fetches the Xtensa and RISC-V compilers, OpenOCD and ``esptool`` for the pinned IDF version.
2. In every shell you build in, ``source`` the export script.
   It puts ``idf.py``, ``esptool.py`` and the toolchain on ``PATH``::

       source ~/esp/esp-idf/export.sh

3. Check the install: ``idf.py --version`` reports 5.5.1, and ``idf.py set-target esp32s3`` (or ``esp32p4``) configures without error.

If you build for the 4C, the first ``set-target esp32p4`` also pulls the ESP-Hosted / ``esp_wifi_remote`` component pair (the remote C6 over SDIO) and the two vendored BSPs.
The component manager fetches them from the ``idf_component.yml`` manifests.
The three ``int`` ``ESP_HOSTED_HOST_USB_*_MANIFEST`` guard keys (:doc:`board selection Kconfig <../reference/kconfig-board>`) keep the component manager's version solver from aborting while it does.
The P4 build uses its own build directory (``build/p4``) and its own dependency lock (``dependencies.lock.p4``), so the two targets' different BSP and driver sets do not overwrite each other (:doc:`the build system <../reference/build-system>`).

Identify which port is which board
----------------------------------

Do not hard-code which port is which board.
The mapping changes with USB topology: which board is plugged in, the hub order, a re-plug.
The S3 and the P4 each present a serial adapter that the host claims as a ``/dev/ttyACM*`` node.
So does the knob's app MCU, if the Type-C plug sits in the S3 orientation.
If you flip the knob's plug, the receptacle switches to the board's second MCU, the U4WDH, and its CH340-family bridge shows up as ``/dev/ttyUSB*`` instead (:doc:`the Knob board <knob>`).

1. Plug in one board at a time and watch which node appears::

       lsusb                        # a QinHeng / Espressif serial adapter
       dmesg | tail                 # the ttyACM node just claimed

   The node that appears when you plug the board in, and disappears when you unplug it, is that board's port.
   Repeat for each board so you learn all of them.
2. Before you flash, confirm which chip sits on the port::

       esptool.py --port <port> chip_id

   ``ESP32-P4`` identifies the 4C.
   ``ESP32S3`` identifies the S3 board or the knob: both are ESP32-S3 with 16 MB flash, so the chip report alone cannot tell them apart.
   Tell them apart by step 1 (which physical board you plugged in) and by label.
   On the knob, the plug orientation also decides the answer: the S3 side reports ``ESP32S3``, the U4WDH side ``ESP32-U4WDH``.
   (``esptool.py`` is on ``PATH`` after ``export.sh``.)
   Run this check against the port you are about to flash; it is the one check that catches a stale port guess.
3. Keep a scratch note of the mapping for this plug session.
   It does not survive a re-plug, so re-run step 2 if in doubt.

Build and flash
---------------

Before you start, you have sourced ``export.sh`` and identified the board's port.

.. graphviz:: ../_graphs/build-flash.dot
   :alt: The build and flash sequence: export the toolchain, bake the assets, merge the configuration, build, check the size, flash and monitor.
   :caption: The build and flash chain, left to right. It starts with ``source ~/esp/esp-idf/export.sh`` and the asset bake: ``python3 assets/images/moon/get_assets.py`` re-verifies the committed NASA kit (LFS, sha256-pinned) and bakes, and the bake alone is ``cd assets/images/moon && .venv/bin/python -m moon.assets``; its outputs are local and gitignored. The size check compares against the factory partition (2048 K S3, the binding constraint; 9216 K P4; 4096 K knob). The chain ends with the port rule: flash the ``chip_id``-verified port, never a remembered one.
   :align: center

The chain above is one leg of the first bring-up.
:doc:`The getting-started page <../getting-started>` draws the whole journey: the host shell, the optional asset branch behind its configure-time guards, the ``chip_id`` gate, the two command shapes separated by the target/board check, the boot sync, and the provisioning chain that ends in the credential file.

Each board page has the exact commands.
These rules hold for all three:

1. Run one ``idf.py`` at a time.
   The builds share the component manager's cache, and parallel configures corrupt each other's lock resolution.
2. After every build, check the binary size against the ``factory`` partition: 2048 K on the S3, 9216 K on the P4, 4096 K on the knob (:doc:`the partition tables <../reference/partitions>`).
   The S3's 2 MB ``factory`` is the binding constraint; :doc:`the binary-size chronology <../verification/binary>` tracks it.
3. Flash the identified port, never a remembered one.
   Re-run the ``chip_id`` check after any re-plug.

``idf.py flash -p <port>`` writes all three images: the ``factory`` app, the ``storage`` SPIFFS (baked from ``spiffs/`` on every build) and the ``moon_assets`` blob (:doc:`the build system <../reference/build-system>`).
A full flash resets the runtime SPIFFS state.
The app writes ``alarm.txt``, ``wifi_credentials.txt`` and ``settings.txt`` back at run time, and a re-flash replaces them with the ``spiffs/`` defaults (:doc:`the SPIFFS store <../reference/spiffs-store>`).

If the board is the knob, build and flash it from the same tree — it is an esp32s3 target, so the knob's flag (not the target) selects it, and its defaults file is passed explicitly:

.. code-block:: bash

   idf.py -D KNOB_BOARD=ON -D SDKCONFIG=sdkconfig.knob \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.knob" \
     -B build/knob build
   idf.py -D KNOB_BOARD=ON -D SDKCONFIG=sdkconfig.knob \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.knob" \
     -B build/knob app-flash -p <port>

``app-flash`` keeps the knob's SPIFFS Wi-Fi credentials; a full flash resets them (:doc:`the Knob board <knob>`).

How the default configuration is merged
---------------------------------------

The build configuration is a ``sdkconfig.defaults`` split: one shared file, plus a per-target file that the build system appends automatically.
The later file wins.
The knob is an esp32s3 target, so the auto-append would give it the S3's file; its defaults are passed explicitly instead.

.. graphviz:: ../_graphs/sdkconfig-merge.dot
   :alt: The sdkconfig merge order: the shared file, then the per-target file, then the live configuration, checked by the board guard; the knob's explicitly passed file and live config as the third lane.
   :caption: The merge order, top to bottom. kconfgen appends the per-target file when it exists, and the later file wins. The live files are the gitignored ``sdkconfig``, ``sdkconfig.p4`` and ``sdkconfig.knob``. A hand-edited pin in a live file beats every ``default`` line, so after you change a default, delete the stale pin. The knob's lane is the explicitly passed one: ``-D SDKCONFIG_DEFAULTS`` selects ``sdkconfig.defaults.knob`` and ``-D SDKCONFIG`` the live ``sdkconfig.knob``.
   :align: center

``sdkconfig.defaults``
   The shared values: the LVGL 9.6 setup, the component-manager manifest, ``CONFIG_PARTITION_TABLE_CUSTOM=y``, the clock and UI defaults, and target ``esp32s3`` so that a plain build picks the S3.

``sdkconfig.defaults.esp32s3``
   The S3's board values: the 16 MB flash header, octal PSRAM at 80 MHz, the 240 MHz core, the CPU1 idle watchdog off, and flush queue depth 1.

``sdkconfig.defaults.esp32p4``
   The P4's values: ``CONFIG_BOARD_P4=y``, ``partitions_p4.csv``, the 32 MB flash header, rev v1.3, PSRAM at 200 MHz with the experimental flag, XIP with a 256 KB L2 cache, PCF85063A off, and the C6-over-SDIO settings.

``sdkconfig.defaults.knob``
   The knob's own defaults (``CONFIG_BOARD_KNOB=y``, ``partitions_knob.csv``, the 100 Hz systick, …).
   The knob is an esp32s3 target, so the auto-append gives it the S3's per-target file on top of the shared one, and this file is added last, explicitly (``-D SDKCONFIG_DEFAULTS``) — later file wins, so it sets the knob's values (:doc:`the Knob board <knob>`).
   Its live config ``sdkconfig.knob`` is gitignored like the clock's.

Keep the root files by hand.
``idf.py save-defconfig`` writes only the shared file and would clobber the board lines.
A stray board line in the wrong file stops the build with a ``FATAL_ERROR`` from the root ``CMakeLists`` target/board guard.
Each board page lists its own file and the full compile-time settings.
The Kconfig rules for the whole project are in :doc:`the build system <../reference/build-system>`.

Where to go next
----------------

- :doc:`The S3 board <s3>`: the default board's pin map, defaults and settings.
- :doc:`The P4 4C port <p4>`: the second board and what the port took.
- :doc:`The build system <../reference/build-system>`: the CMake guards, the Kconfig rules and the knob seam.
