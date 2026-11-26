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
      esptool.py --port <port> chip_id         # ESP32S3 → the 1.75 AMOLED —
                                               #   but ALSO the knob: both are
                                               #   ESP32-S3 with 16 MB flash, so
                                               #   disambiguate by which board
                                               #   you just plugged in (the
                                               #   devices pages carry the full
                                               #   three-way check)
                                               # ESP32-P4 → the 4C board

3. **Build and flash.** Fresh clones build from the checked-in
   ``sdkconfig.defaults`` (no manual ``sdkconfig`` anywhere):

   .. code-block:: bash

      git clone git@git.project-moby.net:matthias/moon-phase.git
      cd moon-phase
      idf.py build                            # default target = esp32s3
      idf.py flash -p <port>                  # app + SPIFFS + moon assets
      idf.py monitor -p <port>                # serial console, 115200

   For the **P4 4C** instead, the same flow with the target, build
   directory and live config pinned on every command —
   ``idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build``
   and ``… flash -p <port>`` against the port that reported ``ESP32-P4``
   (the flag gives the P4 its own gitignored ``sdkconfig.p4`` — the root
   ``sdkconfig`` pins the S3 target and the configure would abort
   without it).
   :doc:`The P4 page <devices/p4>` has the exact sequence.

   A fresh clone has no moon asset blobs yet: the configure *warns*
   (does not fail) and the clock boots without the textured moon. One
   command puts the full product in place before that first flash —
   ``python3 assets/images/moon/get_assets.py --refs``, the
   `Acquiring the moon assets`_ step below.

4. **Tell it the network.** A fresh board has no
   ``/spiffs/wifi_credentials.txt`` yet, so the blocking boot sync
   (which runs *before* the panel comes up — see the first-boot sketch
   below)
   simply fails and the transient WiFi screen rides the ~8 s connect +
   10 s NTP handshake, then drops to the airplane screen: it shows the
   handshake, not a list, and never strands the device. The list is
   one tap away: tap the screen (home and the WiFi screen share the
   one root handler) to open the settings hub, take the WiFi ring
   entry, and the settings-WiFi screen starts the scan on entry — its
   roller shows a centred SCANNING placeholder, then one row per
   access point. Tap an access point and enter the password on the
   round keyboard (the :doc:`round keyboard's design record <keyboard>`
   is what that screen looks like and why); the circle-check key
   commits. The credentials persist in the board's SPIFFS —
   :doc:`the SPIFFS store <reference/spiffs-store>` — and the same
   commit starts the sync: the next boot's blocking sync finds the
   file and the board lands on the home with its moon.

That is the whole bring-up: toolchain, one build, one flash, one
password. Everything else — the moon asset pipeline, the image gate,
the uisim screenshot pass — is developer tooling, not part of putting
a clock on a desk.

.. graphviz:: _graphs/bringup-overview.dot
   :alt: The bring-up as a black-box overview — the host, the optional asset branch, the chip_id gate, the per-target build and flash, the first boot resolving before the panel, and provisioning, closed by one loop into the credential file
   :align: center
   :caption: The bring-up, drawn as black boxes: the five stages of the journey above, each sealed — no internals on this figure. Every box opens into its own sketch: the *commands* of step 3 are drawn once, :doc:`on the devices page <devices/index>` (``build-flash.dot``); the *optional asset branch* — pipeline, the configure-time ``FATAL_ERROR`` trio, the warning-only missing blob, the image gate — rides the pipeline figure in :doc:`the build-system reference <reference/build-system>`; the *referee* between the two command shapes is the target/board guard described in that same reference; and the two step-4 boxes open below this figure into their own detail sketches. The blue loop is the whole point: provisioning ends where the boot sync began, on the credential file the next boot's sync finds.

.. graphviz:: _graphs/bringup-boot-sync.dot
   :alt: First boot — wifi_manager_init reads the credential file, NTP runs under its bound, the radio deinits before the panel, NTP-fixed wakes to the airplane screen, and the failed handshake shows the transient WiFi screen until the UI tick hands off
   :align: center
   :caption: The first-boot box, opened. The boot sync is synchronous **before the panel exists** (main.cpp :494-515): the fresh board's missing credential file prints "WiFi credentials not configured. Please set valid credentials." (wifi.c :299) and the NTP leg rides the 10 s bound (actions.c :65-67). The immediate ``wifi_manager_deinit()`` is deliberate — "Airplane before the panel ever wakes up" (main.cpp :532-541; the DTIM beacon theft, task #29). The success path never shows the WiFi screen (main.cpp :644-662); the failure path shows it through the "~8+10 s handshake" (actions.c :796; the 8 s + 10 s pair), and the one UI-tick hand-off drops it on *both* handshake outcomes — "a failure must not strand the device on the WiFi screen" (:62-64, :733-734). The sync task's own anatomy — what runs off the UI task, and why — is ``wifi-sync.dot`` on :doc:`the WiFi screen <ui/wifi>` page.

.. graphviz:: _graphs/bringup-provisioning.dot
   :alt: The provisioning chain — tap opens the hub, the WiFi ring entry arms the scan at entry, the roller tap opens the prefilled wheel keyboard, the circle-check writes both credential lines and re-runs the handshake
   :align: center
   :caption: The network box, opened. One shared root handler serves the home and the transient WiFi screen, but "the wake tap itself never opens the hub — a deliberate second tap does" (screens.c :108-118); the hub ring's ``GLYPH_WIFI`` entry (screens.c :598) is the same table the tap ladder BFS-walks; the scan is armed at screen *entry*, behind the panel hold (screens.c :1624-1631, wifi.c :545); tapping an access-point row opens the round wheel keyboard with the SSID prefilled (:1574-1577); the circle-check rejects an empty password (:2297-2300) and ``wifi_credentials_write`` lands **both** lines in ``/spiffs/wifi_credentials.txt`` (wifi.c :22, :117) before the same handshake re-runs — and the next boot's blocking sync finds the file. The file stays gitignored ("never commit" it, the hard invariant); ``idf.py flash`` rewrites the partition from ``spiffs/``, so a tracked example is all the image carries. The transient screen's anatomy is :doc:`the WiFi screen <ui/wifi>`.


Acquiring the moon assets
-------------------------

The ``moon_assets`` partition holds the pre-baked equirect albedo +
relief DEM — one blob **per board target** (the S3's 864×432 albedo
and 2048×1024 DEM fit its 2560 K partition; the P4's 2048×1024 pair
fits its 5120 K —
:doc:`the partition tables <reference/partitions>`). The **NASA ground
truth ships with the repo** — the CGI Moon Kit pair from SVS 4720
(``moon-kit/``: the 4096×2048 16-bit LROC albedo loaded 16-bit via
tifffile, carrying the 180° longitude offset, and the LOLA relief DEM)
and the 36 SVS 5415 gate/quarter reference frames
(``moon_phases_2025/``) are committed via LFS, and the 8760-hour
``mooninfo_2025.json`` is plain tracked JSON — proven static (one
constant archive digest 2020–2026), sha256-pinned, public domain, so a
fresh clone needs no network round-trip to NASA. Only the *derived*
products (the ``out/<target>/`` bakes, the ``final_400x400/`` answer-key
crops, ``tone_lut.bin``, the ``.venv``) are gitignored and re-built by
one stdlib script, idempotently (a re-run re-verifies what exists and
builds only what is missing):

.. code-block:: bash

   python3 assets/images/moon/get_assets.py --refs

That single command bootstraps the local ``.venv`` (uv when present,
else ``venv`` + pip) from the tracked ``requirements.txt``,
re-verifies the committed sources against their **sha256 + size pins**
(a failing copy is re-fetched from the primary — or the Internet
Archive fallback, since SVS retired its ``/vis/`` tree on 2026-11-26 —
and a moved file is refused, not baked), regenerates ``tone_lut.bin``
from the tracked ``moon/constants.py`` table, bakes the per-target blob
+ manifest + the tracked generated header
(``components/moon/generated/<target>/``) into ``out/<target>/``, and
— with ``--refs`` — crops the committed SVS 5415 frames the image gate
and the docs' NASA figures read into ``final_400x400/`` (the
``mooninfo`` is already committed; the dialamoon fetch behind it is the
fresh-acquisition path only). Provenance, every URL and pin:
``assets/images/moon/README.md``.

The build tolerates the blobs' absence — the configure only *warns*
and the app boots without the textured moon — so this step is optional
for a bare bring-up and required for the product. The texture width is
the Kconfig ``MOON_ALBEDO_W`` (height = width / 2, even only),
defaulting per board to the best resolution each flash + PSRAM budget
allows (864 on the S3, 2048 on the P4 — the first-match-wins
``default 2048 if BOARD_P4`` line in
:cfile:`components/moon/Kconfig`). The **knob** (sibling project
``knob/``, :doc:`the device page <devices/knob>`) is a third target
with **no Kconfig mirror**: its sizes (albedo 864, relief DEM halved to
1024×512 for the PSRAM budget) live only in the pipeline's
``TARGETS["knob"]`` row, written into its generated header and guarded
by ``knob/CMakeLists.txt`` against its own partition table.

Only regenerate when you change the pipeline or ``MOON_ALBEDO_W`` —
the sources stay cached under ``moon-kit/``, so the bake leg needs no
network, and the root ``CMakeLists`` fails the configure
(``FATAL_ERROR``) on a width/manifest/partition mismatch:

.. code-block:: bash

   cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob

The moon image gate then proves the pipeline's output
against NASA's reference renders for **all three** board textures
(one simulator build per target: ``build/``, ``build-p4/``,
``build-knob/``, each configured with ``-DMOON_ASSETS_TARGET=<t>``):

.. code-block:: bash

   host_sim/.venv/bin/python host_sim/check_gate.py --target s3
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4
   host_sim/.venv/bin/python host_sim/check_gate.py --target knob

The branch as one picture — sources, per-board blobs, the three
configure-time guards, the warning-only missing blob, and the gate —
is the pipeline figure in :doc:`the build-system reference
<reference/build-system>`; its sealed version is the amber box in the
bring-up figure above.
