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
   * - ``moon_movie``
     - data/undefined
     - **11648 K (0xb60000)**
     - LAST row: the boot movie — one synodic cycle of q90 gray
       JPEGs, baked by ``python -m moon.movie`` from the
       device-identical portable core (355 frames @ 2 h =
       10,264,284 B today). The app is bounded by its own
       ``factory`` row, so the movie can never crowd the code —
       grow ``factory``, shrink this row, re-run the generator (it
       lands on a coarser frame step): the "fill the space, never
       hinder the code build" policy.

Total 16384 K (the 64 K fixed prefix — bootloader at 0x0, partition
table at 0x8000, nvs + phy up to the factory's 0x10000 — plus the
rows): the 16 MB chip is fully allocated, the ``moon_movie`` row
owning the space the earlier tables left unused. The 2 MB
``factory`` is the board's binding constraint — the binary-size
chronology (:doc:`the binary-size chronology <../verification/binary>`)
tracks it.
