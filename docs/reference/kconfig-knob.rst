Knob component Kconfig
======================

.. brief:
   Reader: a developer building the knob board.
   They already know: ESP-IDF builds, what a Kconfig option is, and that the knob is a sibling project.
   After this page they can: look up every knob option, see which shared clock/UI keys the knob pins, and change a value.
   Page type: reference.

These options control the knob's own moon view, its time handling (the boot-arm bridge), its power ladder and deep sleep, its UI umbrella (``KNOB_UI_ENABLE``) and its battery fuel gauge.
The fuel gauge has eight of the thirty-three keys.

The options live in the ``knob/`` sibling project's component ``Kconfig`` files: ``knob_moon``, ``knob_time``, ``knob_power``, ``knob_ui``, ``knob_battery`` and the ``board`` shim.
The physics has one source, the generated ``components/moon/generated/knob/moon_consts.h`` (:doc:`the moon constants <moon-consts>`).
The board facts are on :doc:`the Knob board <../devices/knob>`.

To change a value, run ``idf.py -C knob -B ../build/knob menuconfig`` (it writes the live ``knob/sdkconfig``) or set it in ``knob/sdkconfig.defaults`` (:doc:`the build system <build-system>`).
A ``CONFIG_…`` line in the live ``knob/sdkconfig`` beats every Kconfig ``default``.
After you change a default, delete the stale line from ``knob/sdkconfig``.

The shared keys the knob pins or reads
--------------------------------------

The knob links part of the clock's Kconfig tree.
It compiles ``components/clock`` (its keys are single-sourced on :doc:`kconfig-clock`) and ``components/ui`` (single-sourced on :doc:`kconfig-ui`).
This list names only the keys the knob pins or reads:

* ``BOARD`` choice: spelled by the knob's ``board`` shim.
  The shared board component is not added to the build, and ESP-IDF 5.5 does not process a root ``Kconfig.projbuild``.
  Default ``BOARD_KNOB``.
  ``BOARD_S3`` and ``BOARD_P4`` stay selectable but unused, so the shared components' ``if BOARD_P4`` Kconfig clauses parse.
* ``CLOCK_USE_PCF85063A``: pinned ``n`` in ``knob/sdkconfig.defaults``.
  The knob has no RTC chip, so it takes the clock's software time path (as the P4 does).
* ``CLOCK_NTP_SERVER``: default ``pool.ntp.org``.
  The boot arm's time fix goes to this server.
* ``CLOCK_DEFAULT_TIMEZONE``: the ``clock_sntp`` TZ fallback; the settings drive the zone otherwise.
  The remaining clock keys compile at their defaults.
* ``UI_SCREEN_SWITCH_FADE``: the render seam paces its LEDC fade and dim ramps at this many ms.
  It is the equivalent of the clocks' 0x51 hardware ramp; 0 means instant, no ramp.
* ``UI_CLOCK_ALPHA_PCT``: the shared clock widget's opacity at the knob's dim rung (20 %, dim parity; ``knob_render_set_home_dim``).
  The remaining UI keys compile at their defaults; they drive the dial and alarm-editing screens.

The knob's own options
----------------------

.. include:: _fragments/kconfig-knob.inc

Where to go next
----------------

- :doc:`the knob display <../implementation/knob-display>`: the power ladder and the alarm arbitration these keys tune.
- :doc:`the partition tables <partitions>`: the knob's flash layout.
