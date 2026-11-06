Getting started
===============

This site documents the Moon Phase Clock end to end — the physics, the
implementation, the two boards, the user interface, and the validation.
Start where your job is:

- **I want to build and flash one of the boards.**
  :doc:`The boards <devices/index>` — the shared ESP-IDF 5.5.1 toolchain
  setup, the board-identification procedure (never hard-code which
  ``/dev/ttyACM*`` port is which board), the build/flash conventions,
  and the per-board hardware, pre-configured defaults, and
  compile-time settings: :doc:`the ESP32-S3 1.75 AMOLED <devices/s3>`
  (the product's face, the default target) and :doc:`the ESP32-P4 4C
  <devices/p4>` (the 720×720 round MIPI-DSI board, ported as a full
  sibling).
- **I want to understand what this product is and why it is built the
  way it is.** :doc:`The overview <overview>` — the one-paragraph
  product definition, the design constraints, and the map of the rest
  of the site.
- **I want the science** — how a moon frame is computed (the body-frame
  lighting model, the terrain, the art pass) and validated against
  NASA's rendered moon. :doc:`The science <science/index>`.
- **I want the implementation** — the C core, the display pipeline
  (one atomic whole-panel flush), the animation system, the sleep/wake
  machine. :doc:`The implementation <implementation/index>`.
- **I want to see the user interface** — one page per screen, both
  panels, every element named and linked to its code and its option:
  :doc:`The user interface <ui/index>`, and the round keyboard's own
  design record plus the 13-alternative study: :doc:`the round
  keyboard <keyboard>`, :doc:`the round keyboard lab <keyboard-lab>`.
- **I want the reference** — the build system, the Kconfig tables, the
  constants, the partition tables, the SPIFFS store, the Python/C APIs,
  the sources: :doc:`The reference <reference/index>`.

Every code link in the site resolves to the locally generated source
pages (:doc:`The source code <code/index>`), and the reverse "Referenced
from" index on those pages links back to the docs — the two directions
keep the documentation and the code from drifting apart.

Regenerating the moon assets (optional)
---------------------------------------

The ``moon_assets`` partition holds the pre-baked equirect albedo +
relief DEM — one blob **per board target** (the S3's 864×432 albedo +
DEM fits its 2560 K partition; the P4's 2048×1024 fits its 5120 K —
:doc:`the partition tables <reference/partitions>`). The blobs and the
rest of ``assets/`` are gitignored; the texture width is the Kconfig
``MOON_ALBEDO_W`` (height = width / 2, even only), defaulting per board
to the best resolution each flash + PSRAM budget allows (864 on the S3,
2048 on the P4 — the first-match-wins
``default 2048 if BOARD_P4`` line in
:cfile:`components/moon/Kconfig`). Only regenerate if you change the
pipeline or ``MOON_ALBEDO_W`` — the root ``CMakeLists`` fails the configure
(``FATAL_ERROR``) on a width/manifest/partition mismatch:

.. code-block:: bash

   cd assets/images/moon && uv venv .venv && uv pip install -e .
   .venv/bin/python -m moon.assets --target s3 p4    # both boards

The pipeline loads the 4096×2048 16-bit LROC albedo (16-bit via
tifffile; the texture carries the 180° longitude offset), bakes the
per-target blob + manifest + the tracked generated header
(``components/moon/generated/<target>/``), and validates the
partition fit. The moon image gate then proves the pipeline's output
against NASA's reference renders for **both** board textures:

.. code-block:: bash

   host_sim/.venv/bin/python host_sim/check_gate.py --target s3
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4
