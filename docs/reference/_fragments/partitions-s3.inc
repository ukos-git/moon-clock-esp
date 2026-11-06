.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no top-level headings, ../<section>/<page> :doc: targets).

The S3 (16 MB)
---------------
.. graphviz:: ../_graphs/partition-s3.dot
   :alt: S3 partition layout
   :align: center

.. list-table::
   :header-rows: 1
   :widths: 18 14 12 56

   * - Name
     - Type
     - Size
     - Contents
   * - ``nvs``
     - data/nvs
     - 24 K (0x6000)
     - NVS key-value store
   * - ``phy_init``
     - data/phy
     - 4 K (0x1000)
     - Wi-Fi PHY calibration
   * - ``factory``
     - app/factory
     - **2048 K**
     - The application binary. Widened from the old 1392 K /
       1,425,408 B ceiling when the app outgrew it; the circular
       keyboard's widened font + code was the push.
   * - ``storage``
     - data/spiffs
     - 64 K
     - SPIFFS: ``wifi_credentials.txt`` + ``alarm.txt`` (+ the
       runtime-owned ``settings.txt``) — :doc:`the SPIFFS store
       <../reference/spiffs-store>`
   * - ``moon_assets``
     - data/undefined
     - **2560 K**
     - The generated equirect albedo + int8 relief DEM blob
       (864×432 albedo + 2048×1024 DEM = 2,470,400 B < 2,621,440 B)
       — :doc:`the Python API <../reference/python-api>`

Total 4752 K; the 16 MB chip leaves ~10.7 MB unused. The 2 MB
``factory`` is the board's binding constraint — the binary-size
chronology (:doc:`the binary-size chronology <../verification/binary>`)
tracks it.
