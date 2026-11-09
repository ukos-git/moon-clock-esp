Knob component Kconfig
======================

The ``knob/`` sibling project's compile-time surface: its own
``knob_moon``, ``knob_time`` (the boot-arm bridge), ``knob_power``
and ``board`` (shim) component ``Kconfig`` files. The physics stays
single-truth through the generated
``components/moon/generated/knob/moon_consts.h``
(:doc:`the moon constants <moon-consts>`), and the board facts are
on :doc:`the Knob board <../devices/knob>`.

Since the P0 family seam the knob **does** link part of the clock's
Kconfig tree — ``components/clock`` is compiled in (its keys are
single-sourced on :doc:`kconfig-clock`; only the ones the knob pins
or reads are called out here):

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
  their defaults; the UI-facing ones start mattering when the
  shared screens land with the parity phases.

.. include:: _fragments/kconfig-knob.inc
