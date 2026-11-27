Alarm rules
===========

The alarm rules screen: the large central alarm-clock is the **master toggle** — white while any rule is loaded (``clock_alarm_rule_types() != 0``), moon grey when everything is off; a tap deactivates ALL rules (and disarms the RTC alarm via the scheduler reload — which now drops the cached next-alarm time on **every** board and fires the same alarm-update callback a rule write does, so the home screen's alarm row goes to ``--:--`` immediately, not at the next minute tick; the 0-rule reload path was missing both — and on the PCF85063A boards the cache clear was compiled out entirely, so even the minute ticks kept re-rendering the deactivated time from the stale cache, caught on the bench and fixed in :commit:`94806f03`). The left stopwatch and the right alarm-clock are the two **mutually-exclusive entries**: activating one replaces the other's rule, so exactly one kind of rule ever exists. Colours are refreshed on entry and after each save — the tick must not re-set styles every frame (style sets invalidate).

.. figure:: ../img/screens/annot/settings-alarm_s3.png
   :alt: alarm rules — the S3 panel (466, annotated)
   :align: center

   Alarm rules — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-alarm_p4.png
   :alt: alarm rules — the P4 panel (720, annotated)
   :align: center

   Alarm rules — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_alarm.png
   :alt: alarm rules — the knob panel (360)
   :align: center

   Alarm rules — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; raw panel capture, :doc:`the UI index <index>`)

The triangle is one of the computed menu figures: the middle row of
:ref:`menu-geometry` (``docs/tools/gen_menu_geom.py``) draws exactly
this screen per panel — master at (0, −``UI_PX(30)``), the entries at
(±``UI_PX(130)``, +``UI_PX(130)``), the disks sized by the tier's FA
icon face (96/148/74 px).

Elements
--------

.. list-table::
   :header-rows: 1
   :widths: 4 24 44 28

   * - #
     - Element
     - Purpose
     - Code / option

   * - 1
     - Master — tap deactivates all
     - The central 96 px regular alarm glyph; white = at least one rule armed, grey = everything off (a tap then is a no-op — re-arming goes through the two entries).
     - ``event_handler_cb_settings_alarm_master``, :cfunc:`settings_alarm_screen_refresh`

   * - 2
     - Pomodoro (stopwatch, solid)
     - The interval entry (``clock_alarm_rule_types()`` bit 0). Entering it replaces any real-alarm rule.
     - :doc:`settings-pomodoro <settings-pomodoro>` via ``event_handler_cb_settings_alarm_entry``

   * - 3
     - Real alarm (clock, regular)
     - The fixed-time + day-range entry (bit 2). Entering it replaces any pomodoro rule.
     - :doc:`settings-alarm-time <settings-alarm-time>` via ``event_handler_cb_settings_alarm_entry``

   * - 4
     - Exit → hub
     - The shared bottom-centre exit (the 96 px circle-xmark) back to the CALLING level — the hub here.
     - :cfunc:`settings_sub_screen_prepare`
