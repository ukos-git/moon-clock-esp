Board selection Kconfig
=======================

.. brief:
   Reader: a developer building one of the clock boards.
   They already know: ESP-IDF builds and what a Kconfig option is.
   After this page they can: look up the board choice and the esp_hosted guard keys, and change them.
   Page type: reference.

These options pick the board the app is built for and carry the guard keys for the P4's remote Wi-Fi (esp_hosted).
The board value selects the app-side behaviour: the P4-only code paths gate on ``CONFIG_BOARD_P4``.
The build target (``IDF_TARGET``) selects the partition table and the board support package (BSP) component.

The options live in ``components/board/Kconfig.projbuild``.
ESP-IDF 5.5 does not process a ``Kconfig.projbuild`` at the project root (it collects only per-component ``Kconfig`` / ``Kconfig.projbuild`` files), so the choice lives in the board-shim component, which every board build includes — the knob build included, whose ``BOARD_KNOB`` row it carries (:doc:`the Knob board <../devices/knob>`).

To change a value, run ``idf.py menuconfig`` (it writes the live ``sdkconfig``) or set it in the matching ``sdkconfig.defaults*`` file (:doc:`the build system <build-system>`); the knob build carries the same choice in its own live file, ``sdkconfig.knob``.
A ``CONFIG_…`` line in the live file beats every Kconfig ``default``.
After you change a default, delete the stale line from the live files: ``sed -i '/^CONFIG_<KEY>=/d' sdkconfig sdkconfig.p4 sdkconfig.knob``.

The board and the target must agree.
The root ``CMakeLists`` raises a ``FATAL_ERROR`` on a mismatch (:doc:`the build system <build-system>`).

The board choice
----------------

.. include:: _fragments/kconfig-board-choice.inc

The esp_hosted guard keys
-------------------------

.. include:: _fragments/kconfig-guard-keys.inc

Where to go next
----------------

- :doc:`kconfig-clock`: the time-base and idle options.
- :doc:`the P4 port <../implementation/p4-port>`: why the P4 needs its own code paths.
