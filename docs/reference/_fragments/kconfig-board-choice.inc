.. NOTE: shared fragment — included by docs/reference/kconfig-board.rst and
   the docs/devices/ pages. No section headings here (the includer provides
   them), and :doc: targets use the ../<section>/<page> form, which resolves
   identically from any depth-1 docs/ directory.

The board selection itself is a Kconfig ``choice``:

.. list-table::
   :header-rows: 1
   :widths: 28 12 60

   * - Symbol
     - Default
     - Meaning
   * - ``BOARD`` (choice)
     - ``BOARD_S3``
     - the board selector prompt; one of the two below
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

An asymmetry worth knowing: the S3 selection is the *default* of the
choice, so ``sdkconfig.defaults.esp32s3`` does **not** carry
``CONFIG_BOARD_S3=y`` (the default already selects it), whereas
``sdkconfig.defaults.esp32p4`` **does** carry ``CONFIG_BOARD_P4=y``
(overriding the default back to the P4). The root ``CMakeLists``
guard (:doc:`the build system <../reference/build-system>`) is what
makes a stray board line in the wrong file a build-time error rather
than a silent runtime mix-up.

The knob project carries a **mirror of this choice in its ``board``
shim's Kconfig** (the shared board component is not in that build, and
ESP-IDF 5.5 does not process the root ``Kconfig.projbuild``): the same
three symbols with ``BOARD_KNOB`` added and made the default.

.. list-table::
   :header-rows: 1
   :widths: 28 12 60

   * - Symbol
     - Default
     - Meaning
   * - ``BOARD_KNOB``
     - ``y``
     - the Waveshare ESP32-S3-Knob-Touch-LCD-1.8: 360×360 SH8601 QSPI
       round LCD, CST816S touch, EC1 dial (no push-button), LEDC
       backlight, battery ADC on GPIO 1, **no** IMU / RTC IC / audio
       codec — real ``esp_deep_sleep_start``, the time base is the
       internal RTC timer + SNTP (software path, as on the P4)
       (:doc:`the Knob board <../devices/knob>`)

``BOARD_S3``/``BOARD_P4`` stay selectable-but-unused there — the
shared components' Kconfig lines reference the symbols (``default
2048 if BOARD_P4`` in moon), and Kconfig errors on undefined symbols
in ``if`` clauses, so all three must exist in every project.
