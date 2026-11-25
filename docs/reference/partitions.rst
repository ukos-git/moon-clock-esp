Partition tables
================

Each board has its own flash partition table — the S3's
``partitions.csv`` (16 MB chip), the P4's
``partitions_p4.csv`` (32 MB chip), and the ``knob/`` sibling
project's ``knob/partitions.csv`` (16 MB chip). The selection lives in the
per-target defaults files, not in CMake: the shared
``sdkconfig.defaults`` enables ``CONFIG_PARTITION_TABLE_CUSTOM=y``;
the P4 file sets
``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``; the S3
keeps the default ``partitions.csv``. (The ``partition_table``
component's project hook re-derives ``PARTITION_CSV_PATH`` from the
Kconfig values, clobbering any plain ``set()`` in CMake —
:doc:`the build system <build-system>`.) Consumers find every
partition by *label*, not offset, so the three tables are otherwise
free to differ in size.

Each table is shown twice: the row listing — kept honest against its
CSV by :doc:`the drift gate <build-system>` — and the same rows as a
layout strip in flash order.

S3 — ``partitions.csv``
-----------------------

.. include:: _fragments/partitions-s3.inc

.. graphviz:: ../_graphs/partition-s3.dot
   :alt: S3 partition chain — bootloader + table, factory 2048 K, storage 64 K, moon_assets 2560 K, moon_movie 11648 K
   :caption: Flash order as a chain: the ``factory`` 2048 K row is the binding constraint of the family, and the ``moon_assets`` 2560 K row prices the 864×432 albedo + 2048×1024 DEM blob (2,470,400 B combined). The ``moon_movie`` 11648 K LAST row owns the chip's leftover — one synodic cycle of q90 gray JPEGs — so the table fills the whole 16384 K chip including the 64 K prefix.
   :align: center

P4 — ``partitions_p4.csv``
--------------------------

.. include:: _fragments/partitions-p4.inc

.. graphviz:: ../_graphs/partition-p4.dot
   :alt: P4 partition chain — bootloader + table, factory 9216 K, storage 64 K, moon_assets 5120 K, moon_movie 18304 K
   :caption: The 32 MB chip's chain: ``factory`` 9216 K, ``moon_assets`` 5120 K pricing the 2048×1024 albedo + 2048×1024 DEM blob (4096 K combined), and the ``moon_movie`` 18304 K LAST row owning the chip's leftover (one synodic cycle of q90 gray JPEGs, 178 frames @ 4 h today) — the table fills the whole 32768 K chip. The ``moon_movie`` row starts at 14464 K, past the old 16 MB header mark; the binary header is now 32 MB (bumped with the movie wave), so esptool's old size-mismatch warning on this board is gone.
   :align: center

knob — ``knob/partitions.csv``
------------------------------

.. include:: _fragments/partitions-knob.inc

.. graphviz:: ../_graphs/partition-knob.dot
   :alt: knob partition chain — bootloader + table, factory 4096 K, storage 512 K, moon_assets 2560 K, coredump 64 K, unused
   :caption: The sibling's chain: ``factory`` 4096 K, a fatter 512 K ``storage``, the 2560 K ``moon_assets`` row (864×432 albedo + 1024×512 DEM = 876 K combined), and the 64 K ``coredump`` witness of task #27 — read it with ``esptool read_flash`` at the data offset plus ``espcoredump.py``. The table totals 7296 K; ~8.8 MB stays unused.
   :align: center

The data partitions in the build
--------------------------------

The root ``CMakeLists`` flashes the *data* partitions alongside the
``factory`` app (:doc:`the build system <build-system>`):

- **``storage`` (SPIFFS)** is baked from the source tree's
  ``spiffs/`` directory on **every** build (``FLASH_IN_PROJECT``) —
  and it is *runtime-owned*: the app writes ``alarm.txt`` and
  ``wifi_credentials.txt`` back to it, so a full flash **clobbers**
  the runtime state (accepted; the settings screens re-create them).
- **``moon_assets``** is a *generated* blob (gitignored, local-only).
  The build *warns* (does not fail) when it is missing — the app
  boots without the moon, which is the failure mode a reader should
  recognize.
- **``moon_movie``** (S3 and P4, the LAST row) is the second
  *generated* blob: the boot movie's synodic cycle of q90 gray JPEGs
  (``python -m moon.movie``), sized to fill the row's leftover —
  "fill the space, never hinder the code build". On the P4 the row
  starts past the old 16 MB mark (the 32 MB bump,
  :doc:`the binary budget <../verification/binary>`). The build
  *warns* (does not fail) when it is missing: the boot falls back to
  the card replay / disc path (the card-less posture), so a movie
  never blocks a flash.

The ``knob/`` sibling project mirrors both — ``storage`` baked from
``knob/spiffs/`` (``wifi_credentials.txt``, the clock's filename —
the shared ``components/wifi`` reads it) and the ``moon_assets``
blob flashed to its partition — but guards the blob the other way:
existence and fit are configure-time ``FATAL_ERROR``\\ s in
``knob/CMakeLists.txt``, because a knob booting without the moon is
a bench mistake, not a shipping failure mode.
