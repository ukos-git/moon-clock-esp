Moon constants
==============

The moon's compile-time constants are **not Kconfig** — they are
``#define``s in a generated header. The single source of truth is
``assets/images/moon/moon/constants.py`` (shared verbatim with the
host simulator); ``python -m moon.assets`` renders the per-board
header into ``components/moon/generated/{s3,p4,knob}/moon_consts.h``
(do-not-edit). ``components/moon/CMakeLists.txt`` adds
``generated/p4`` on ``CONFIG_BOARD_P4`` and ``generated/s3``
otherwise, so every clock translation unit sees the board's own
constants; the ``knob/`` sibling project adds ``generated/knob`` in
its own build. The bake's geometry, the lighting model, and the asset
blobs all agree because they all read the same table.

The per-board differences are the albedo size (``MOON_ALB_W/H``, the
P4's 2048 against the S3/knob's 864) and the relief DEM
(``MOON_DEM_W/H`` and its quantization range, 1024×512 on the knob) —
everything else is board-independent. The asset pipeline
(:doc:`the Python API <python-api>`) regenerates the headers, the
blobs, and the ``moon_assets.bin`` partition image together, and the
root ``CMakeLists`` ``FATAL_ERROR``s on a width/manifest/partition
mismatch (``knob/CMakeLists.txt`` replicates the guards without the
Kconfig check) — so after touching the pipeline or ``MOON_ALBEDO_W``
all three targets' headers and blobs must be regenerated
(``--target s3 p4 knob``).

The constants
-------------

.. include:: _fragments/moon-consts.inc
