Moon constants
==============

The moon's compile-time constants are **not Kconfig** — they are
``#define``s in a generated header. The single source of truth is
``assets/images/moon/moon/constants.py`` (shared verbatim with the
host simulator); ``python -m moon.assets`` renders the per-board
header into ``components/moon/generated/{s3,p4}/moon_consts.h``
(do-not-edit). ``components/moon/CMakeLists.txt`` adds
``generated/p4`` on ``CONFIG_BOARD_P4`` and ``generated/s3``
otherwise, so every translation unit sees the board's own constants.
The bake's geometry, the lighting model, and the asset blobs all agree
because they all read the same table.

The only per-board difference is the albedo size (``MOON_ALB_W/H``) —
everything else is board-independent. The asset pipeline
(:doc:`the Python API <python-api>`) regenerates the headers, the
blobs, and the ``moon_assets.bin`` partition image together, and the
root ``CMakeLists`` ``FATAL_ERROR``s on a width/manifest/partition
mismatch — so after touching the pipeline or ``MOON_ALBEDO_W`` both
boards' headers and blobs must be regenerated.

The constants
-------------

.. include:: _fragments/moon-consts.inc
