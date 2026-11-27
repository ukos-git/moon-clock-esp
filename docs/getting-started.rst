Getting started
===============

This tutorial takes you from a new board to a working clock on your
desk. You install the tools, build the firmware, copy it onto the
board, and connect the clock to Wi-Fi. The commands are for Linux and
have been checked on a fresh copy of the project.

The steps use the **ESP32-S3-Touch-AMOLED-1.75**, the default board.
The 4C board needs slightly different build commands, shown in step 5.
For the knob board, follow :doc:`its own page <devices/knob>` from
step 5 on.

What you need
-------------

- one of the three boards (:doc:`the boards <devices/index>`);
- a USB-C cable that carries data, not just power;
- a Linux computer with about 5 GB of free disk space;
- a 2.4 GHz Wi-Fi network and its password;
- about an hour, most of it waiting for downloads.

You do not need to know C. You will type commands into a terminal; if
one fails, the error message usually names the missing piece.

1. Install the ESP-IDF toolchain
--------------------------------

ESP-IDF is Espressif's set of tools for building software for its
chips. The project is built with version 5.5.1.

.. code-block:: bash

   # Debian/Ubuntu: the packages ESP-IDF needs
   sudo apt-get install git git-lfs wget flex bison gperf python3 \
       python3-pip python3-venv cmake ninja-build ccache libffi-dev \
       libssl-dev libusb-1.0-0 dfu-util

   mkdir -p ~/esp && cd ~/esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf && git checkout v5.5.1
   ./install.sh esp32s3            # add esp32p4 if you have the 4C board

Every new terminal you build in needs this line first. It puts the
tools on your path:

.. code-block:: bash

   source ~/esp/esp-idf/export.sh

Check that it worked: ``idf.py --version`` should print ``5.5.1``.

On other distributions, ESP-IDF's ``install.sh`` prints the package
list it needs.

2. Get the project
------------------

The project stores some large NASA image files with Git LFS, so set
that up before you clone:

.. code-block:: bash

   git lfs install
   git clone https://github.com/ukos-git/moon-clock-esp.git moon-phase
   cd moon-phase

3. Prepare the Moon maps
------------------------

The NASA maps in the repository are too large for the board. One
script checks them, shrinks them to the size each board can hold and
packs them into the form the firmware reads. It sets up its own Python
environment the first time, so it only needs ``python3``:

.. code-block:: bash

   python3 assets/images/moon/get_assets.py

You can skip this step. The clock then still builds and runs, but it
has no Moon picture. The build prints a warning to remind you.

4. Find the board's port
------------------------

Plug in the board. Linux gives it a device name such as
``/dev/ttyACM0``. To find out which one:

.. code-block:: bash

   ls /dev/ttyACM*          # on some computers /dev/ttyUSB*

If you are unsure, unplug the board, run the command again, and see
which name disappeared. Then check which chip is on that port:

.. code-block:: bash

   esptool.py --port /dev/ttyACM0 chip_id

``ESP32-S3`` means the AMOLED board (or the knob, which uses the same
chip). ``ESP32-P4`` means the 4C board. Do this check every time you
plug in more than one board: the names can change.

5. Build and flash
------------------

"Flashing" means copying the firmware into the board's memory. From
the project folder:

.. code-block:: bash

   idf.py build                       # the first build takes several minutes
   idf.py flash -p /dev/ttyACM0       # use the port from step 4
   idf.py monitor -p /dev/ttyACM0     # show the board's log; Ctrl+] quits

For the **4C board**, every command needs three extra settings: the
chip, a separate build folder, and a separate configuration file:

.. code-block:: bash

   idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 build
   idf.py -D IDF_TARGET=esp32p4 -B build/p4 -D SDKCONFIG=sdkconfig.p4 flash -p /dev/ttyACM0

:doc:`The 4C page <devices/p4>` explains why.

6. First start
--------------

After flashing, the board restarts on its own. It tries to set its
clock over Wi-Fi, but it does not know your network yet, so this
fails. You see a Wi-Fi screen for about 20 seconds, then the home
screen with the Moon. The time is wrong until the next step.

7. Connect to Wi-Fi
-------------------

1. Tap the screen. The settings open.
2. Tap the Wi-Fi icon and wait for the list of networks.
3. Tap your network and type the password on the round keyboard.
4. Tap the check mark.

The clock connects, sets the time, and returns to the home screen.
:doc:`Using the clock <using-the-clock>` explains the keyboard and the
other settings.

That's it: you have a working clock. Everything else in these docs is
for people who want to change it.

Behind the scenes
-----------------

This section is for developers. It shows what the steps above do in
the firmware, and how the Moon maps are made.

.. graphviz:: _graphs/bringup-overview.dot
   :alt: The bring-up as a black-box overview — the host, the optional asset branch, the chip_id gate, the per-target build and flash, the first boot resolving before the panel, and provisioning, closed by one loop into the credential file
   :align: center
   :caption: The bring-up as five sealed stages. The build and flash commands are drawn in detail on :doc:`the boards page <devices/index>` (``build-flash.dot``). The asset branch (pipeline, the configure-time ``FATAL_ERROR`` checks, the warning for a missing blob, the image gate) is the pipeline figure in :doc:`the build system <reference/build-system>`, which also describes the target/board guard. The two step-7 boxes open in the next two figures. The blue loop: provisioning ends at the credential file that the next boot's sync reads.

.. graphviz:: _graphs/bringup-boot-sync.dot
   :alt: First boot — wifi_manager_init reads the credential file, NTP runs under its bound, the radio deinits before the panel, NTP-fixed wakes to the airplane screen, and the failed handshake shows the transient WiFi screen until the UI tick hands off
   :align: center
   :caption: First boot. The boot sync runs synchronously **before the panel exists** (main.cpp :494-515). On a new board the credential file is missing, so ``wifi.c`` :299 logs "WiFi credentials not configured. Please set valid credentials." and the NTP leg runs into its 10 s bound (actions.c :65-67). ``wifi_manager_deinit()`` follows straight away, so the radio is off before the panel wakes (main.cpp :532-541; this stops the DTIM beacon wake-ups). On success the WiFi screen never appears (main.cpp :644-662). On failure it stays up through the ~8 s connect plus 10 s NTP handshake (actions.c :796), and one UI tick drops it on either outcome, so a failure cannot strand the device there (:62-64, :733-734). The sync task itself is ``wifi-sync.dot`` on :doc:`the WiFi screen <ui/wifi>` page.

.. graphviz:: _graphs/bringup-provisioning.dot
   :alt: The provisioning chain — tap opens the hub, the WiFi ring entry arms the scan at entry, the roller tap opens the prefilled wheel keyboard, the circle-check writes both credential lines and re-runs the handshake
   :align: center
   :caption: Provisioning. The home and the transient WiFi screen share one root touch handler, but a wake tap never opens the hub; only a second tap does (screens.c :108-118). The hub ring's ``GLYPH_WIFI`` entry (screens.c :598) starts the scan when the screen opens, behind the panel hold (screens.c :1624-1631, wifi.c :545). Tapping a network row opens the wheel keyboard with the SSID filled in (:1574-1577). The check mark refuses an empty password (:2297-2300); ``wifi_credentials_write`` writes **both** lines to ``/spiffs/wifi_credentials.txt`` (wifi.c :22, :117), and the same handshake runs again. That file is gitignored and must never be committed. ``idf.py flash`` rewrites the partition from ``spiffs/``, so the image carries only the tracked example file.

Where the Moon maps come from
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``moon_assets`` partition holds two pre-baked maps per board: the
brightness map (albedo, equirectangular) and the relief height map
(DEM). The S3 gets an 864 × 432 albedo and a 2048 × 1024 DEM, which fit
its 2560 K partition; the P4 gets a 2048 × 1024 pair in its 5120 K
partition (:doc:`the partition tables <reference/partitions>`).

The NASA source data ships with the repository:

- the CGI Moon Kit from SVS 4720 (``moon-kit/``): the 4096 × 2048
  16-bit LROC albedo, read as 16-bit with tifffile and carrying a 180°
  longitude offset, and the LOLA relief DEM;
- the 36 SVS 5415 reference frames used by the image gate and the
  quarter figures (``moon_phases_2025/``);
- ``mooninfo_2025.json``, the 8760-hour table of Moon data, as plain
  JSON.

The image files are stored with Git LFS. All of it is public domain and
pinned by sha256 and size. The data is static (the archive digest has
not changed from 2020 to 2026), so a fresh clone needs no download from
NASA. Only the *derived* files are gitignored and rebuilt: the
``out/<target>/`` bakes, the ``final_400x400/`` answer-key crops,
``tone_lut.bin`` and the ``.venv``.

``get_assets.py`` (stdlib only) does the whole job and is safe to run
again: it checks what already exists and builds only what is missing.
In order, it:

1. creates the local ``.venv`` (with ``uv`` if present, otherwise
   ``venv`` and pip) from the tracked ``requirements.txt``;
2. checks the committed sources against their **sha256 and size pins**.
   A file that fails is downloaded again from the primary source, or
   from the Internet Archive copy since SVS retired its ``/vis/`` tree
   on 2026-11-26. A file that has moved is refused, not baked;
3. regenerates ``tone_lut.bin`` from the table in ``moon/constants.py``;
4. bakes each target's blob, its manifest and the tracked generated
   header (``components/moon/generated/<target>/``) into
   ``out/<target>/``;
5. with ``--refs``, crops the committed SVS 5415 frames into
   ``final_400x400/`` for the image gate and the NASA figures in these
   docs.

``mooninfo`` is already committed; the dialamoon download behind it is
only needed to acquire fresh data. Every URL and pin is listed in
``assets/images/moon/README.md``.

The texture width is the Kconfig option ``MOON_ALBEDO_W`` (the height is
half the width; the value must be even). Its default is the largest
size each board's flash and PSRAM allow: 864 on the S3 and 2048 on the
P4, through the first-match-wins line ``default 2048 if BOARD_P4`` in
:cfile:`components/moon/Kconfig`. The **knob** (:doc:`its page
<devices/knob>`) has no Kconfig mirror. Its sizes (albedo 864, the
relief DEM halved to 1024 × 512 to fit its PSRAM) live only in the
pipeline's ``TARGETS["knob"]`` row, which is written into its generated
header and checked by the knob's CMake guard against its own partition
table.

Rebake only after you change the pipeline or ``MOON_ALBEDO_W``. The
sources stay cached under ``moon-kit/``, so this needs no network. The
root ``CMakeLists`` stops the configure step (``FATAL_ERROR``) if the
width, the manifest and the partition do not match:

.. code-block:: bash

   cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob

Then run the image gate for **all three** board textures. It compares
the result with NASA's reference renders. Each target has its own
simulator build (``build/``, ``build-p4/``, ``build-knob/``, each
configured with ``-DMOON_ASSETS_TARGET=<t>``):

.. code-block:: bash

   python3 assets/images/moon/get_assets.py --refs    # the reference crops, once
   host_sim/.venv/bin/python host_sim/check_gate.py --target s3
   host_sim/.venv/bin/python host_sim/check_gate.py --target p4
   host_sim/.venv/bin/python host_sim/check_gate.py --target knob

The whole asset branch (sources, per-board blobs, the three
configure-time checks, the missing-blob warning and the gate) is drawn
in the pipeline figure in :doc:`the build system
<reference/build-system>`.
