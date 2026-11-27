Moon constants
==============

.. brief:
   Reader: a developer working on the moon renderer or the asset pipeline.
   They already know: C headers, the Python asset pipeline exists.
   After this page they can: look up each moon constant, know where it comes from and how to regenerate it.
   Page type: reference.

The moon's compile-time constants are not Kconfig options.
They are ``#define`` lines in a generated header, one per board, and one Kconfig key (the albedo width) selects what the pipeline bakes.

Where the constants come from
-----------------------------

The single source of truth is ``assets/images/moon/moon/constants.py``, shared verbatim with the host simulator.
``python -m moon.assets`` writes the per-board header to ``components/moon/generated/{s3,p4,knob}/moon_consts.h`` (do not edit).

``components/moon/CMakeLists.txt`` selects the directory from the ``BOARD`` symbols with a first-match ``if/elseif/else``:
``generated/knob`` on ``CONFIG_BOARD_KNOB``, ``generated/p4`` on ``CONFIG_BOARD_P4``, ``generated/s3`` otherwise.
Every consumer's ``#include "moon_consts.h"`` therefore resolves to the board's own texture dimensions.
The ``knob/`` sibling project supplies the symbol through its own board shim.
The bake's geometry, the lighting model and the asset blobs agree because they all read the same table.

What differs per board
----------------------

Two things differ per board:

- the albedo size (``MOON_ALB_W/H``): 2048 on the P4, 864 on the S3 and the knob;
- the relief elevation model (DEM: ``MOON_DEM_W/H`` and its quantization range): 1024×512 on the knob.

Everything else is the same on all boards.

How to regenerate
-----------------

The asset pipeline (:doc:`the Python API <python-api>`) regenerates the headers, the blobs and the ``moon_assets.bin`` partition image together.
The root ``CMakeLists`` raises ``FATAL_ERROR`` on a width, manifest or partition mismatch.
``knob/CMakeLists.txt`` replicates the guards without the Kconfig check.
After you touch the pipeline or ``MOON_ALBEDO_W``, regenerate all three targets' headers and blobs:

.. code-block:: bash

   cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob

The constants
-------------

.. include:: _fragments/moon-consts.inc

The one Kconfig key
-------------------

Everything generated above follows one Kconfig selection, the asset width the pipeline bakes and the build flashes:

.. include:: _fragments/kconfig-moon.inc

Where to go next
----------------

- :doc:`the C API <c-api>`: the functions that consume these constants.
- :doc:`the geometry <../science/geometry>`: where the pole and prime-meridian values come from.
