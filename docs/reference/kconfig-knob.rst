Knob component Kconfig
======================

The ``knob/`` sibling project's compile-time surface: its own
``knob_moon``, ``knob_time`` (the boot-arm bridge), ``knob_power``,
``knob_ui`` (the ``KNOB_UI_ENABLE`` umbrella), ``knob_battery`` (the
fuel gauge — eight of the twenty-seven keys) and ``board`` (shim)
component ``Kconfig`` files. The physics stays
single-truth through the generated
``components/moon/generated/knob/moon_consts.h``
(:doc:`the moon constants <moon-consts>`), and the board facts are
on :doc:`the Knob board <../devices/knob>`.

Since the P0 family seam the knob **does** link part of the clock's
Kconfig tree — ``components/clock`` is compiled in (its keys are
single-sourced on :doc:`kconfig-clock`), and since P1 so is
``components/ui`` (single-sourced on :doc:`kconfig-ui`; only the ones
the knob pins or reads are called out here):

* ``BOARD`` choice — spelled by the knob's ``board`` **shim** (the
  shared board component is not added to the build, and a root
  ``Kconfig.projbuild`` is not processed by ESP-IDF 5.5). Default
  ``BOARD_KNOB``; ``BOARD_S3``/``BOARD_P4`` stay
  selectable-but-unused so the shared components' ``if BOARD_P4``
  Kconfig clauses parse.
* ``CLOCK_USE_PCF85063A`` — pinned ``n`` in
  ``knob/sdkconfig.defaults``: no RTC IC, the clock's software time
  path (the P4's posture).
* ``CLOCK_NTP_SERVER`` — default ``pool.ntp.org``: the boot arm's
  fix goes to this server (it replaced the retired
  ``KNOB_NTP_SERVER``).
* ``CLOCK_DEFAULT_TIMEZONE`` — the ``clock_sntp`` TZ fallback
  (settings-driven otherwise). The remaining clock keys compile at
  their defaults.
* ``UI_SCREEN_SWITCH_FADE`` — since P1 the seam paces its **LEDC**
  fade/dim ramps at this many ms (the clocks' 0x51 hardware-ramp
  equivalent; 0 = instant, no ramp).
* ``UI_CLOCK_ALPHA_PCT`` — the shared clock widget's opacity at the
  knob's dim rung (20 %, dim parity; ``knob_render_set_home_dim``).
  The remaining UI keys compile at their defaults — the screens they
  drive shipped with P2 (the dial) and P3 (alarm editing).

.. include:: _fragments/kconfig-knob.inc
