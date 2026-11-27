Partition tables
================

.. brief:
   Reader: a developer flashing or resizing one of the boards.
   They already know: what a flash partition table is.
   After this page they can: read each board's partition rows and know which partitions the build flashes and what a full flash overwrites.
   Page type: reference.

Each board has its own flash partition table:

- the S3: ``partitions.csv`` (16 MB chip);
- the P4: ``partitions_p4.csv`` (32 MB chip);
- the knob: ``partitions_knob.csv`` (16 MB chip).

Consumers find every partition by label, not by offset, so the three tables are free to differ in size.

How the build picks a table
---------------------------

The per-target defaults files select the table, not CMake.
The shared ``sdkconfig.defaults`` enables ``CONFIG_PARTITION_TABLE_CUSTOM=y``.
The P4 file sets ``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``.
The knob's ``sdkconfig.defaults.knob`` sets it to ``partitions_knob.csv`` (the knob is an esp32s3 target, so its defaults file is passed explicitly, with ``-D SDKCONFIG_DEFAULTS``).
The S3 keeps the default ``partitions.csv``.
The ``partition_table`` component's project hook re-derives ``PARTITION_CSV_PATH`` from the Kconfig values and overwrites any plain ``set()`` in CMake (:doc:`the build system <build-system>`).

Each table below appears twice: as the row listing, which :doc:`the drift gate <build-system>` checks against its CSV, and as a layout strip in flash order.

S3 — ``partitions.csv``
-----------------------

.. include:: _fragments/partitions-s3.inc

.. graphviz:: ../_graphs/partition-s3.dot
   :alt: The S3 partitions in flash order: bootloader and table, factory 2048 K, storage 64 K, moon_assets 2560 K, moon_movie 11648 K.
   :caption: The S3 table in flash order. The ``factory`` 2048 K row is the tightest limit of the three boards. The ``moon_assets`` 2560 K row holds the 864×432 albedo and the 2048×1024 DEM (2,470,400 B combined). The last row, ``moon_movie`` 11648 K, takes the chip's leftover for one synodic cycle of q90 gray JPEGs, so the table fills the whole 16384 K chip including the 64 K prefix.
   :align: center

P4 — ``partitions_p4.csv``
--------------------------

.. include:: _fragments/partitions-p4.inc

.. graphviz:: ../_graphs/partition-p4.dot
   :alt: The P4 partitions in flash order: bootloader and table, factory 9216 K, storage 64 K, moon_assets 5120 K, moon_movie 18304 K.
   :caption: The P4 table in flash order. ``factory`` is 9216 K. ``moon_assets`` 5120 K holds the 2048×1024 albedo and the 2048×1024 DEM (4096 K combined). The last row, ``moon_movie`` 18304 K, takes the chip's leftover (one synodic cycle of q90 gray JPEGs, 178 frames at 4 h each), so the table fills the whole 32768 K chip. ``moon_movie`` starts at 14464 K, beyond the 16 MB mark; the binary header declares 32 MB, so esptool prints no size-mismatch warning on this board.
   :align: center

knob — ``partitions_knob.csv``
------------------------------

.. include:: _fragments/partitions-knob.inc

.. graphviz:: ../_graphs/partition-knob.dot
   :alt: The knob partitions in flash order: bootloader and table, factory 4096 K, storage 512 K, moon_assets 2560 K, moon_movie 9088 K, coredump 64 K.
   :caption: The knob table in flash order. ``factory`` is 4096 K and ``storage`` is a larger 512 K. ``moon_assets`` 2560 K holds the 864×432 albedo and the 1024×512 DEM (876 K combined). ``moon_movie`` 9088 K takes the chip's leftover (one synodic cycle of q90 gray JPEGs, 355 frames at 2 h each); the knob has no SD socket, so flash is its only movie source. The last row is the 64 K ``coredump`` partition. The table fills the whole 16384 K chip.
   :align: center

Read the knob's ``coredump`` partition with ``esptool read_flash`` at the partition's data offset, then decode it with ``espcoredump.py``.

The data partitions the build flashes
-------------------------------------

The root ``CMakeLists`` flashes the data partitions alongside the ``factory`` app (:doc:`the build system <build-system>`).

``storage`` (SPIFFS)
   The build creates it from the source tree's ``spiffs/`` directory on every build (``FLASH_IN_PROJECT``).
   The app owns it at runtime: it writes ``alarm.txt`` and ``wifi_credentials.txt`` back to it.
   A full flash therefore overwrites the runtime state; the settings screens re-create the files.

``moon_assets``
   A generated blob (gitignored, local only).
   The build warns, and does not fail, when it is missing.
   The app then boots without the moon; recognise that symptom.

``moon_movie``
   The second generated blob: the boot movie's synodic cycle of q90 gray JPEGs (``python -m moon.movie``), sized to fill the row's leftover so the movie never limits the code build.
   It is the last row on the S3 and the P4, and the last but one on the knob, ahead of its 64 K ``coredump`` ring.
   On the P4 the row starts beyond the 16 MB mark (:doc:`the binary budget <../verification/binary>`).
   The knob has no SD socket, so its movie lives only in flash.
   The build warns, and does not fail, when the blob is missing.
   The boot then falls back to the card replay, the saved frame or the disc (the card-less path), so a missing movie never blocks a flash.

The knob build runs through the **same root** ``CMakeLists``, so it is covered by the very guards described above — no knob-side copy:

- ``storage`` comes from the shared ``spiffs/`` and holds ``wifi_credentials.txt`` (the clock's filename, because the shared ``components/wifi`` reads it).
- A missing ``moon_assets`` blob warns.
  A malformed manifest, or a blob that does not fit its row, is a configure-time ``FATAL_ERROR`` from the root CMakeLists (the knob's table is ``partitions_knob.csv``, read through ``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME``): a knob that boots without the moon is a bench mistake, not a shipping failure mode.
  The albedo-width cross-check is skipped for the knob target (it has no ``MOON_ALBEDO_W`` Kconfig mirror; the manifest's ``alb_w`` and the partition fit are its guards).
- The ``moon_movie`` blob goes through the same movie-guard block and only warns when missing.
- The root CMake's ``MOON_CALC_VER`` derive stamps the knob's firmware, and the reader rejects a blob with a stale calculation version at boot.

Where to go next
----------------

- :doc:`the SPIFFS store <spiffs-store>`: the files on the ``storage`` partition.
- :doc:`the binary budget <../verification/binary>`: how close each ``factory`` app is to its row.
- :doc:`the boot movie <../implementation/moon-movie>`: what the ``moon_movie`` blob holds.
