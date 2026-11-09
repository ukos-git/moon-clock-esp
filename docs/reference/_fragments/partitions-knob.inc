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
       components, ~1.3 MB — ~67 % free; the ``knob/`` sibling
       project's own app)
   * - ``storage``
     - data/spiffs
     - 512 K
     - SPIFFS: ``wifi_credentials.txt`` (gitignored — the clock's
       filename, read by the shared ``components/wifi``)
       — :doc:`the SPIFFS store <../reference/spiffs-store>`
   * - ``moon_assets``
     - data/undefined
     - **2560 K**
     - The generated equirect albedo + int8 relief DEM blob
       (864×432 albedo + 1024×512 DEM = 897,536 B). 2560 K is sized
       for the proven 2048×1024-DEM bake too, so the table never has
       to change — a partition change means a full flash on the bench
       unit — :doc:`the Python API <../reference/python-api>`

Total 7232 K; the 16 MB chip leaves ~8.9 MB unused. The table is
``knob/partitions.csv``, selected by ``knob/sdkconfig.defaults``
(``CONFIG_PARTITION_TABLE_CUSTOM=y`` + filename, the same mechanism
as the clock's, in the sibling project's own config).
