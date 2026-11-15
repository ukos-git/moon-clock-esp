Getting started
===============

This site documents the Moon Phase Clock end to end — the physics, the
implementation, the three boards, the user interface, and the
validation.
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
  way it is.** The :doc:`start page <index>` — the product definition,
  the three design properties, and the map of the rest of the site.
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

Flashing a newly acquired board on Linux
----------------------------------------

The minimal path, verified end to end on a fresh clone. The S3 1.75
AMOLED is the default target; the commands below are for it — the P4
4C differs only where marked. :doc:`The board pages <devices/index>`
carry the details (port identification, pre-configured defaults,
binary-size checks).

1. **Install ESP-IDF 5.5.1** (the version the project is built
   against; the repo's ``idf.py build`` picks it up from the sourced
   environment)::

      # Debian/Ubuntu first (the ESP-IDF installer's prerequisites):
      sudo apt-get install git wget flex bison gperf python3 \
          python3-pip python3-venv cmake ninja-build ccache libffi-dev \
          libssl-dev libusb-1.0-0 dfu-util

      git clone --recursive https://github.com/espressif/esp-idf.git
      cd esp-idf && git checkout v5.5.1
      ./install.sh esp32s3            # esp32p4 as well, for the 4C board
      source ~/esp/esp-idf/export.sh  # every idf.py session starts here

   (The ESP-IDF installer's own ``install.sh`` prints the exact package
   list for your distribution — the line above is the one this repo
   uses.)

2. **Clone the project and identify the port.** Plug the board in and
   find the node it claims — the one that appears on plug-in and
   disappears on unplug is the board's port. Confirm the chip on it
   before flashing:

   .. code-block:: bash

      ls /dev/ttyACM*                          # /dev/ttyUSB* on some hosts
      esptool.py --port <port> chip_id         # ESP32S3 → the 1.75 AMOLED
                                               # ESP32-P4 → the 4C board

3. **Build and flash.** Fresh clones build from the checked-in
   ``sdkconfig.defaults`` (no manual ``sdkconfig`` anywhere):

   .. code-block:: bash

      git clone git@git.project-moby.net:matthias/moon-phase.git
      cd moon-phase
      idf.py build                            # default target = esp32s3
      idf.py flash -p <port>                  # app + SPIFFS + moon assets
      idf.py monitor -p <port>                # serial console, 115200

   For the **P4 4C** instead, the same flow with the target and build
   directory pinned on every command —
   ``idf.py -D IDF_TARGET=esp32p4 -B build/p4 build`` and
   ``… flash -p <port>`` against the port that reported ``ESP32-P4``.
   :doc:`The P4 page <devices/p4>` has the exact sequence.

4. **Tell it the network.** The board scans on boot and lands on the
   WiFi screen; tap an access point and enter the password on the
   round keyboard (the :doc:`round keyboard's design record <keyboard>`
   is what that screen looks like and why). The credentials persist in
   the board's SPIFFS — :doc:`the SPIFFS store <reference/spiffs-store>`.

That is the whole bring-up: toolchain, one build, one flash, one
password. Everything else — the moon asset pipeline, the image gate,
the uisim screenshot pass — is developer tooling, not part of putting
a clock on a desk.

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
:cfile:`components/moon/Kconfig`). The **knob** (sibling project
``knob/``, :doc:`the device page <devices/knob>`) is a third target
with **no Kconfig mirror**: its sizes (albedo 864, relief DEM halved to
1024×512 for the PSRAM budget) live only in the pipeline's
``TARGETS["knob"]`` row, written into its generated header and guarded
by ``knob/CMakeLists.txt`` against its own partition table. Only
regenerate if you change the pipeline or ``MOON_ALBEDO_W`` — the root
``CMakeLists`` fails the configure (``FATAL_ERROR``) on a
width/manifest/partition mismatch:

.. code-block:: bash

   cd assets/images/moon && uv venv .venv && uv pip install -e .
   .venv/bin/python -m moon.assets --target s3 p4 knob   # all three boards

The pipeline loads the 4096×2048 16-bit LROC albedo (16-bit via
tifffile; the texture carries the 180° longitude offset), bakes the
per-target blob + manifest + the tracked generated header
(``components/moon/generated/<target>/``), and validates the
partition fit. The moon image gate then proves the pipeline's output
against NASA's reference renders for **all three** board textures
(one simulator build per target: ``build/``, ``build-p4/``,
``build-knob/``, each configured with ``-DMOON_ASSETS_TARGET=<t>``):

.. code-block:: bash

   host_sim/.venv/bin/python host_sim/check_gate.py --target s3
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4
   host_sim/.venv/bin/python host_sim/check_gate.py --target knob
