Knob component Kconfig
======================

The ``knob/`` sibling project's own compile-time surface — the
``knob_moon``, ``knob_time`` and ``knob_power`` component
``Kconfig`` files. The knob never sees the clock's Kconfig tree: the
generated ``components/moon/generated/knob/moon_consts.h`` is its
single physics truth (:doc:`the moon constants <moon-consts>`), and
the board facts are on :doc:`the Knob board <../devices/knob>`.

.. include:: _fragments/kconfig-knob.inc
