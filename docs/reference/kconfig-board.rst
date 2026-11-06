Board selection Kconfig
=======================

The project's dual-board switch lives in
``components/board/Kconfig.projbuild``. A project-*root*
``Kconfig.projbuild`` is not processed by the ESP-IDF 5.5 build system
(only per-component ``Kconfig`` / ``Kconfig.projbuild`` files are
collected), so the choice lives in the always-built board-shim
component. The Kconfig value selects the board's *app-side* behaviour
(the P4-only code paths gate on ``CONFIG_BOARD_P4``); the build target
(``IDF_TARGET``) selects the partition table and the BSP component.
The two must agree — the root ``CMakeLists`` raises a ``FATAL_ERROR``
on a mismatch (:doc:`the build system <build-system>`).

The board choice
----------------

.. include:: _fragments/kconfig-board-choice.inc

The esp_hosted guard keys
-------------------------

.. include:: _fragments/kconfig-guard-keys.inc
