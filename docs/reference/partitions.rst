Partition tables
================

Each board has its own flash partition table — the S3's
``partitions.csv`` (16 MB chip) and the P4's
``partitions_p4.csv`` (32 MB chip). The selection lives in the
per-target defaults files, not in CMake: the shared
``sdkconfig.defaults`` enables ``CONFIG_PARTITION_TABLE_CUSTOM=y``;
the P4 file sets
``CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv"``; the S3
keeps the default ``partitions.csv``. (The ``partition_table``
component's project hook re-derives ``PARTITION_CSV_PATH`` from the
Kconfig values, clobbering any plain ``set()`` in CMake —
:doc:`the build system <build-system>`.) Consumers find every
partition by *label*, not offset, so the two tables are otherwise
free to differ in size.

.. include:: _fragments/partitions-s3.inc

.. include:: _fragments/partitions-p4.inc

The two data partitions in the build
------------------------------------

The root ``CMakeLists`` flashes the two *data* partitions alongside
the ``factory`` app (:doc:`the build system <build-system>`):

- **``storage`` (SPIFFS)** is baked from the source tree's
  ``spiffs/`` directory on **every** build (``FLASH_IN_PROJECT``) —
  and it is *runtime-owned*: the app writes ``alarm.txt`` and
  ``wifi_credentials.txt`` back to it, so a full flash **clobbers**
  the runtime state (accepted; the settings screens re-create them).
- **``moon_assets``** is a *generated* blob (gitignored, local-only).
  The build *warns* (does not fail) when it is missing — the app
  boots without the moon, which is the failure mode a reader should
  recognize.
