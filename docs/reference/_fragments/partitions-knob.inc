.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no top-level headings, ../<section>/<page> :doc: targets).

The knob (16 MB, sibling project)
----------------------------------
.. graphviz:: ../_graphs/partition-knob.dot
   :alt: knob partition layout
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
     - **4096 K**
     - The application binary (moon display on the shared clock
       components; measured 1,644,832 B (0x191920) on 2026-11-25 —
       ~39 % used;
       the ``knob/`` sibling project's own app —
       :doc:`the binary-size chronology <../verification/binary>`)
   * - ``storage``
     - data/spiffs
     - 512 K
     - SPIFFS: ``wifi_credentials.txt`` (gitignored — the clock's
       filename, read by the shared ``components/wifi``) + the
       tracked ``alarm.txt`` rules file the shared alarm scheduler
       mounts + the **runtime-owned** ``moon_frame.bin`` (the
       persisted last moon frame, 259,224 B: raw RGB565 square + 24 B
       header, written by ``KNOB_UI_LAST_FRAME`` only when the
       (day, signature) pair changes, shown first on the next wake —
       :doc:`the knob display <../implementation/knob-display>`)
       — :doc:`the SPIFFS store <../reference/spiffs-store>`
   * - ``moon_assets``
     - data/undefined
     - **2560 K**
     - The generated equirect albedo + int8 relief DEM blob
       (864×432 albedo + 1024×512 DEM = 897,536 B). 2560 K is sized
       for the proven 2048×1024-DEM bake too, so the table never has
       to change — a partition change means a full flash on the bench
       unit — :doc:`the Python API <../reference/python-api>`
   * - ``moon_movie``
     - data/undefined
     - **9088 K (0x8E0000)**
     - LAST-but-one row: the boot movie — one synodic cycle of q90
       gray JPEGs, baked by ``python -m moon.movie --target knob``
       from the device-identical portable core (355 frames @ 2 h =
       6,075,946 B today). The knob has NO SD socket — flash is its
       ONLY movie source. The app is bounded by its own ``factory``
       row, so the movie can never crowd the code — grow ``factory``,
       shrink this row, re-run the generator (it lands on a coarser
       frame step): the "fill the space, never hinder the code
       build" policy.
   * - ``coredump``
     - data/coredump
     - 64 K
     - The field-crash witness ring: a panic writes an ELF core dump
       here, read back on the bench next to the RTC_NOINIT gasp
       marker — :doc:`the knob display
       <../implementation/knob-display>`. Moved AFTER the movie when
       the row landed: consumers find every partition by label, so
       the ring's offset may move (the row is the same).

Total 16384 K (the 64 K fixed prefix — bootloader at 0x0, partition
table at 0x8000, nvs + phy up to the factory's 0x10000 — plus the
rows): the 16 MB chip is fully allocated, the ``moon_movie`` row
owning the space the earlier tables left unused. The 4 MB
``factory`` is the board's binding constraint — the binary-size
chronology (:doc:`the binary-size chronology <../verification/binary>`)
tracks it. The table is ``knob/partitions.csv``, selected by
``knob/sdkconfig.defaults`` (``CONFIG_PARTITION_TABLE_CUSTOM=y`` +
filename, the same mechanism as the clock's, in the sibling
project's own config).
