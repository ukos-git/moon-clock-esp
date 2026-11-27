The pomodoro interval
=====================

.. brief:
   Reader: a firmware developer working on alarms.
   They already know: C, LVGL rollers, the alarm rules screen.
   After this page they can: trace a roller release to the next ring time and the home alarm row.
   Page type: reference.

The pomodoro screen sets a repeating alarm interval in hours and minutes.
The stopwatch entry on the :doc:`alarm rules screen <settings-alarm>` opens it.

How it behaves
--------------

Two clock-style rollers sit side by side: hours 0–23 and minutes 0–59.
On entry they seed from the stored interval rule (H = N / 60, M = N % 60), so a sub-hour rule shows hours 0.
The rollers are the display; there is no separate preview label.

Only a release saves.
It stores ``minutes_total = H*60+M``; a total of 0 is rejected and the current rule stays.
Saving on release keeps the SPIFFS write and the RTC (real-time clock) reschedule off the drag path.

On the knob, the dial commit sends ``LV_EVENT_RELEASED`` to the focused roller, so the drag path and the detent path are the same commit.

Screenshots
-----------

.. figure:: ../img/screens/annot/settings-pomodoro_s3.png
   :alt: The pomodoro screen on the S3 panel, with numbered callouts on the caption, both rollers and the exit.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/settings-pomodoro_p4.png
   :alt: The pomodoro screen on the P4 panel, with numbered callouts on the caption, both rollers and the exit.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/settings_pomodoro.png
   :alt: The pomodoro screen on the knob's 360 px panel, without callouts.
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
     - Caption (REPEAT INTERVAL)
     - The 32 px SSID font. The clock label has no caption; this screen needs one to say what the value is.
     - :cfunc:`create_screen_settings_pomodoro`

   * - 2
     - Hour roller (00–23)
     - The 96 px bold-numbers font, 110 px tall, in a 140 px wide box. Montserrat's digits are proportional: the widest pair, "44", advances 132 px, so the box needs the width.
     - :cfunc:`settings_create_roller`, ``event_handler_cb_settings_pomodoro_released``

   * - 3
     - Minute roller (00–59)
     - Same style, in the minute slot. The release saves through ``clock_alarm_set_pomodoro_interval``; the RTC reschedule stays off the drag path.
     - :cfunc:`settings_create_roller`

   * - 4
     - Exit → alarm settings
     - The shared bottom-centre exit, back to the calling level: the alarm screen, not the hub.
     - :cfunc:`settings_sub_screen_prepare`

Implementation notes
--------------------

The rollers sit exactly where the home clock's "HH:MM" would be.
The hour slot's glyph centre is at screen x 153, the minute slot's at 303 and the colon at 223: the home label's own glyph centres.

The cycle behind the rollers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The rollers are the top of a chain that ends in a hardware interrupt.
A release rewrites the one rule file whole (:cfile:`components/clock/clock_alarm_scheduler.c`).
The interval rule is ``*/N * * * *``: an alarm rule with a minute interval, not a separate subsystem.
That is why the two entries on the alarm screen stay mutually exclusive by construction.

A 0 (``00:00``) never reaches the file.
``clock_alarm_set_pomodoro_interval`` gates the value to ``1..1440``, and the screen's 0-reject is the user-facing half of that gate.

The parser's pomodoro case is the minute field's ``*/N``, which may span the whole day: N goes up to 1440, with the grid anchored at midnight.
The next ring is the first multiple of N, counted in minutes since midnight, that lies strictly after now.
Once today's grid is used up, the next ring is ``00:00`` of the next day.
A rule with N ≤ 59 instead rings at multiples of N within the hour.
The parser rejects minute and hour intervals together.

The arming function caches the computed next ring and arms the PCF85063A ALM on the S3, or the ``esp_timer`` one-shot on the P4 and knob.
The home alarm row is a pure view of that cache: ``H:MM`` with an unpadded hour, a two-letter weekday prefix only while the ring is more than 24 h away, and ``--:--`` when nothing is scheduled.
The minute tick re-reads the cache because the prefix flips exactly at the 24 h boundary.

The fire itself (the shared 1300 ms poll, the trigger, the ring, and the fire-then-reschedule) is :doc:`the alarm lifecycle <../implementation/clock-sleep>`.
The figures below cover the settings side and the grid arithmetic: the cycle as a chain first, then the two boxes opened.

.. graphviz:: ../_graphs/pomodoro-cycle.dot
   :alt: The pomodoro cycle as a chain: alarm rules screen, stopwatch entry, release commit, rule setter, day grid, arming function, home row, and a note on the fire.
   :caption: The settings side in one chain. A roller release rewrites the one rule file, the day-grid arithmetic caches the next ring, and the home row reads the cache. The fire note belongs to the alarm lifecycle; the interval rule is the other rule kind that lands there (same 1300 ms poll, same fire-then-reschedule).
   :align: center

.. graphviz:: ../_graphs/pomodoro-cycle-edit.dot
   :alt: The settings side opened: three glyphs, the master tap, the mutual-exclusion note, the twin rollers with their seeding, and the dial that sends RELEASED.
   :caption: The settings side opened. The glyph colours come from ``clock_alarm_rule_types()``, derived from the parsed rules, so the glyphs cannot disagree with the file. The entries are mutually exclusive (each setter rewrites ``alarm.txt`` with its single rule). The twin rollers are the display (96 px font at the home clock's x-centres 153/303, colon at 223). Only a release saves, and the knob dial's commit sends ``LV_EVENT_RELEASED``, so drag and detent share the path.
   :align: center

.. graphviz:: ../_graphs/pomodoro-cycle-grid.dot
   :alt: The rule arithmetic opened: the setter gate, the full replace, the */N day grid anchored at midnight, the arming function and the home row view.
   :caption: The rule arithmetic opened. The setter gates ``1..1440`` and writes ``*/N * * * *`` as a full replace and reload. The minute field's ``*/N`` spans the whole day (N > 59 anchors the grid at midnight, N ≤ 59 rings within the hour, both intervals together are rejected). The single arming function caches the nearest ring, then arms the PCF85063A ALM (S3) or the ``esp_timer`` one-shot (P4/knob); the home row stays a pure view of that cache.
   :align: center

Where to go next
----------------

- :doc:`The real alarm <settings-alarm-time>`: the other rule kind.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the poll and the fire.
- :doc:`The airplane screen <modem-sleep>`: the home alarm row that shows the cached next ring.
