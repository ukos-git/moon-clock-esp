Knob component Kconfig
======================

.. brief:
   Reader: a developer building the knob board.
   They already know: ESP-IDF builds, what a Kconfig option is, and that the knob is an esp32s3 target of the same project.
   After this page they can: look up every knob option, see which shared clock/UI keys the knob pins, and change a value.
   Page type: reference.

These options control the knob's own moon view, its time handling (the boot-arm bridge), its power ladder and deep sleep, its UI umbrella (``KNOB_UI_ENABLE``) and its battery fuel gauge.
The fuel gauge has eight of the thirty-three keys.

The options live in the ``knob_*`` components' ``Kconfig`` files under ``components/``: ``knob_moon``, ``knob_time``, ``knob_power``, ``knob_ui`` and ``knob_battery`` — the knob build (``-D KNOB_BOARD=ON``, an esp32s3 target of this project) is the one that compiles them (:doc:`the build system <build-system>`).
The physics has one source, the generated ``components/moon/generated/knob/moon_consts.h`` (:doc:`the moon constants <moon-consts>`).
The board facts are on :doc:`the Knob board <../devices/knob>`.

To change a value, run the knob's ``menuconfig`` (the full flag set is on :doc:`the build system <build-system>`):

.. code-block:: bash

   idf.py -D KNOB_BOARD=ON -D SDKCONFIG=sdkconfig.knob \
     -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.knob" \
     -B build/knob menuconfig

It writes the live ``sdkconfig.knob``; alternatively set a value in ``sdkconfig.defaults.knob``.
A ``CONFIG_…`` line in the live ``sdkconfig.knob`` beats every Kconfig ``default``.
After you change a default, delete the stale line from ``sdkconfig.knob``.

The shared keys the knob pins or reads
--------------------------------------

The knob links part of the clock's Kconfig tree.
It compiles ``components/clock`` (its keys are single-sourced on :doc:`kconfig-clock`) and ``components/ui`` (single-sourced on :doc:`kconfig-ui`).
This list names only the keys the knob pins or reads:

* ``BOARD`` choice: the knob's row in the shared ``choice`` (``components/board/Kconfig.projbuild``).
  The knob build pins ``CONFIG_BOARD_KNOB=y`` in ``sdkconfig.defaults.knob`` (the choice default is ``BOARD_S3``).
  ``BOARD_S3`` and ``BOARD_P4`` stay selectable but unused on the knob, so the shared components' ``if BOARD_P4`` Kconfig clauses parse.
* ``CLOCK_USE_PCF85063A``: pinned ``n`` in ``sdkconfig.defaults.knob``.
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
