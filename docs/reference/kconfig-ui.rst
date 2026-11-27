UI component Kconfig
====================

.. brief:
   Reader: a developer building one of the boards.
   They already know: ESP-IDF builds and what a Kconfig option is.
   After this page they can: look up every UI option's default and meaning, and change it.
   Page type: reference.

These options control the moon pipeline, the sleep tunables, the coupling between the inertial measurement unit (IMU) and the moon's orbit, the demo mode and the headless test hooks.
It is the project's largest menu.
Every value below is the current source default.

The options live in ``components/ui/Kconfig`` (the menu "UI Component Configuration").

To change a value, run ``idf.py menuconfig`` (it writes the live ``sdkconfig``) or set it in the matching ``sdkconfig.defaults*`` file (:doc:`the build system <build-system>`).
A ``CONFIG_…`` line in the live ``sdkconfig`` beats every Kconfig ``default``.
After you change a default, delete the stale line from the live files: ``sed -i '/^CONFIG_<KEY>=/d' sdkconfig sdkconfig.p4``.

The options
-----------

.. include:: _fragments/kconfig-ui.inc

How the code reads these values
-------------------------------

The project's Kconfig rules apply (:doc:`the build system <build-system>`).
A ``n`` leaves the macro undefined, and the code gates on ``#if CONFIG_…``.
All runtime values go through ``#ifndef``-seeded getters.
The settings screens can change some values at runtime: the idle timeout, the sleep countdown, the animation flags, the timezone and the alarm sound.
These compile-time defaults seed them, and the settings store owns them at runtime (:doc:`the SPIFFS store <spiffs-store>`).

Where to go next
----------------

- :doc:`kconfig-knob`: the knob's own options and the UI keys it pins.
- :doc:`the display pipeline <../implementation/display>`: what the moon-pipeline options drive.
