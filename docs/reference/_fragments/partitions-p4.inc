.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no top-level headings, ../<section>/<page> :doc: targets).

The P4 (32 MB)
---------------
.. graphviz:: ../_graphs/partition-p4.dot
   :alt: P4 partition layout
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
     - Kept even though the PHY runs on the C6 slave (the reference
       merged binary and the esp_hosted host-side examples both carry
       it)
   * - ``factory``
     - app/factory
     - **9216 K**
     - The application binary — huge headroom vs the S3's 2048 K
   * - ``storage``
     - data/spiffs
     - 64 K
     - SPIFFS, as on the S3
   * - ``moon_assets``
     - data/undefined
     - **5120 K**
     - The per-board albedo + DEM blob; the P4's 2048×1024 albedo +
       2048×1024 DEM = 4,194,304 B < 5,242,880 B. (A 2560-wide albedo
       would need ≥ 5248 K — the root ``CMakeLists`` guard refuses a
       blob that overflows this row, naming the KiB it needs.)

Total 14464 K (the same 64 K fixed prefix — bootloader at 0x2000,
partition table at 0x8000 like the S3, nvs + phy up to the factory's
0x10000 — plus the rows); the 32 MB chip leaves ~17.9 MB unused. Two
header notes: the P4 binary header still says **16 MB** (both boards
flash a 16 MB header, ``CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y``), so
esptool's size-mismatch warning on the P4 is *expected*.
