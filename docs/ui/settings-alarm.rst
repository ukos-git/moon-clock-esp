Alarm rules
===========

.. brief:
   Reader: a firmware developer working on alarms.
   They already know: C, LVGL basics, the settings hub.
   After this page they can: say what each alarm-rules glyph does and how a deactivation reaches the home screen.
   Page type: reference.

The alarm rules screen turns alarms off and leads to the two kinds of alarm: a repeating pomodoro interval and a real alarm at a fixed time.
The hub's alarm icon opens it.

How it behaves
--------------

The large central alarm clock is the master toggle.
It is white while any rule is loaded (``clock_alarm_rule_types() != 0``) and moon grey when everything is off.
A tap on it deactivates all rules.

The deactivation reloads the scheduler, which disarms the RTC (real-time clock) alarm.
The reload drops the cached next-alarm time on every board and fires the same alarm-update callback that a rule write fires.
The home screen's alarm row therefore reads ``--:--`` at once, not at the next minute tick.

The left stopwatch and the right alarm clock are the two mutually exclusive entries.
Activating one replaces the other's rule, so only one kind of rule ever exists.

Screenshots
-----------

.. figure:: ../img/screens/annot/settings-alarm_s3.png
   :alt: The alarm rules screen on the S3 panel, with numbered callouts on the three glyphs and the exit.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/settings-alarm_p4.png
   :alt: The alarm rules screen on the P4 panel, with numbered callouts on the three glyphs and the exit.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/settings_alarm.png
   :alt: The alarm rules screen on the knob's 360 px panel, without callouts.
   :align: center

   Knob, 360×360: the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``, as a raw panel capture (:doc:`the UI index <index>`).

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
     - Master (tap deactivates all)
     - The central 96 px regular alarm glyph. White means at least one rule is armed, grey means everything is off. A tap on the grey glyph does nothing; re-arming goes through the two entries.
     - ``event_handler_cb_settings_alarm_master``, :cfunc:`settings_alarm_screen_refresh`

   * - 2
     - Pomodoro (stopwatch, solid)
     - The interval entry (``clock_alarm_rule_types()`` bit 0). Entering it replaces any real-alarm rule.
     - :doc:`settings-pomodoro <settings-pomodoro>` via ``event_handler_cb_settings_alarm_entry``

   * - 3
     - Real alarm (clock, regular)
     - The fixed-time and day-range entry (bit 2). Entering it replaces any pomodoro rule.
     - :doc:`settings-alarm-time <settings-alarm-time>` via ``event_handler_cb_settings_alarm_entry``

   * - 4
     - Exit → hub
     - The shared bottom-centre exit (the 96 px circle-xmark), back to the calling level: the hub here.
     - :cfunc:`settings_sub_screen_prepare`

Implementation notes
--------------------

The screen sets its glyph colours on entry and after each save.
The tick must not set styles every frame, because each style set invalidates the object.

The triangle layout is one of the computed menu figures.
The middle row of :ref:`menu-geometry` (``docs/tools/gen_menu_geom.py``) draws this screen per panel.
The master sits at (0, −``UI_PX(30)``), the entries at (±``UI_PX(130)``, +``UI_PX(130)``), and the disks take the size of the tier's FA icon face (96/148/74 px).

Where to go next
----------------

- :doc:`The pomodoro interval <settings-pomodoro>`: the stopwatch entry and the rule arithmetic behind both entries.
- :doc:`The real alarm <settings-alarm-time>`: the fixed-time entry.
- :doc:`The alarm screen <alarm>`: what happens when a rule fires.
