.. NOTE: shared fragment — included by docs/reference/kconfig-board.rst and
   the docs/devices/ pages. No section headings here (the includer provides
   them), and :doc: targets use the ../<section>/<page> form, which resolves
   identically from any depth-1 docs/ directory.

The board selection itself is a Kconfig ``choice`` in the shared
board component (``components/board/Kconfig.projbuild``) — one entry
per board of the family.
The knob is an esp32s3 *target* (like the S3), so its build selects
it additionally through the ``-D KNOB_BOARD=ON`` flag
(:doc:`the build system <../reference/build-system>`):

.. list-table::
   :header-rows: 1
   :widths: 28 12 60

   * - Symbol
     - Default
     - Meaning
   * - ``BOARD`` (choice)
     - ``BOARD_S3``
     - the board selector prompt; one of the three below
   * - ``CONFIG_BOARD_S3``
     - ``y``
     - the Waveshare ESP32-S3-Touch-AMOLED-1.75: 466×466 CO5300 QSPI
       AMOLED, CST9217 touch, PCF85063A RTC + TCA9554 expander,
       QMI8658C IMU, built-in Wi-Fi; 16 MB flash, 8 MB PSRAM (not
       SPI-DMA-capable)
   * - ``CONFIG_BOARD_P4``
     - ``n``
     - the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC (4C): 720×720 JD9365
       MIPI-DSI, GT911 touch, remote C6 Wi-Fi over SDIO (esp_hosted /
       esp_wifi_remote), ES8311 codec, **no** RTC IC / IO expander /
       IMU — the time base is the 32 kHz crystal RTC timer + SNTP,
       alarms are software ``esp_timer``; 32 MB flash, 32 MB PSRAM
   * - ``CONFIG_BOARD_KNOB``
     - ``n``
     - the Waveshare ESP32-S3-Knob-Touch-LCD-1.8: 360×360 SH8601 QSPI
       round LCD, CST816S touch, EC1 dial (no push-button), LEDC
       backlight, battery ADC on GPIO 1, **no** IMU / RTC IC / audio
       codec — real ``esp_deep_sleep_start``, the time base is the
       internal RTC timer + SNTP (software path, as on the P4)
       (:doc:`the Knob board <../devices/knob>`)

An asymmetry worth knowing: the S3 selection is the *default* of the
choice, so ``sdkconfig.defaults.esp32s3`` does **not** carry
``CONFIG_BOARD_S3=y`` (the default already selects it), whereas the
P4's per-target file **does** carry ``CONFIG_BOARD_P4=y``, and the
knob's ``sdkconfig.defaults.knob`` carries ``CONFIG_BOARD_KNOB=y``
(the knob's defaults file is passed explicitly — ``-D
SDKCONFIG_DEFAULTS`` — because the knob is an esp32s3 target, not its own
target). The root ``CMakeLists`` guard
(:doc:`the build system <../reference/build-system>`) is what makes a
stray board line in the wrong file a build-time error rather than a
silent runtime mix-up.

The two symbols of the other boards stay selectable-but-unused on a
given build — the shared components' Kconfig lines reference the
symbols (``default 2048 if BOARD_P4`` in moon), and Kconfig errors on
undefined symbols in ``if`` clauses, so all three must exist.
