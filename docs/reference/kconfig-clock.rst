Clock component Kconfig
=======================

.. brief:
   Reader: a developer building one of the boards.
   They already know: ESP-IDF builds and what a Kconfig option is.
   After this page they can: look up every clock option's default and meaning, and change it.
   Page type: reference.

These options control the clock's time base (external real-time clock (RTC) chip or software time), the time format, the network time server, the default timezone and the idle behaviour.

The options live in ``components/clock/Kconfig`` (the menu "Clock Component Configuration").
The knob build links the same component, so these keys apply to all three boards.

To change a value, run ``idf.py menuconfig`` (it writes the live ``sdkconfig``) or set it in the matching ``sdkconfig.defaults*`` file (:doc:`the build system <build-system>`).
A ``CONFIG_…`` line in the live ``sdkconfig`` beats every Kconfig ``default``.
After you change a default, delete the stale line from the live files: ``sed -i '/^CONFIG_<KEY>=/d' sdkconfig sdkconfig.p4``.

The options
-----------

.. include:: _fragments/kconfig-clock.inc

Where to go next
----------------

- :doc:`the SPIFFS store <spiffs-store>`: the runtime settings these defaults seed.
- :doc:`the clock and sleep machine <../implementation/clock-sleep>`: how the time base and the idle ladder work.
