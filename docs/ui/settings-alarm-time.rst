The real alarm (time + days)
============================

The real alarm: the time rollers (minutes in **5-minute steps** — 10:03 is not a wake-up time) and the **day-range arc**. The arc is the upper half-circle (Monday left, Sunday right) — the same thin outer ring as the home screen's seconds arc (outer radius 223, 10 px from the panel edge; it clears the exit button's band). Day d (0 = Mon) sits at 181 + 30d degrees — the +1° offset keeps every stored arc angle off the 360/0 wrap edge. The dark track (``LV_PART_MAIN``) spans the whole arc; the moon-colour INDICATOR spans the two **draggable day handles** (the vendored arc is single-knob, so the range endpoints are plain objects on the ring, the arc itself non-clickable): a press snaps the handle to the nearest day under the touch point. The "Mo-Fr" range summary above the time carries the day meaning (the day letters are gone), so the ring can own the outer band.

.. figure:: ../img/screens/annot/settings-alarm-time_s3.png
   :alt: the real alarm (time + days) — the S3 panel (466, annotated)
   :align: center

   The real alarm (time + days) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-alarm-time_p4.png
   :alt: the real alarm (time + days) — the P4 panel (720, annotated)
   :align: center

   The real alarm (time + days) — P4 720×720 (the same UI_PX layout, scaled)

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
     - Day arc — a press snaps to a day
     - The upper half-circle track (r 213..223); the indicator (the moon grey) spans the handles. The arc is non-clickable — the presses land on the handles; a press anywhere snaps to the nearest day under the finger.
     - ``alarm_time_point_to_day`` (the inverse-transform idiom), ``event_handler_cb_settings_alarm_time_handle``

   * - 2
     - Range start handle (Mon)
     - The range's start endpoint (the 34 px knob on the ring's midline, r 218). Press + drag moves the start; the press locks to the finger (``PRESS_LOCK``).
     - ``event_handler_cb_settings_alarm_time_handle`` (idx 0)

   * - 3
     - Range end handle (Fri)
     - The range's end endpoint; a single-day range draws a 6° sliver (an arc cannot draw a zero-length span).
     - ``event_handler_cb_settings_alarm_time_handle`` (idx 1)

   * - 4
     - Hour / minute rollers
     - The time at the home clock's position (the minute roller's options are 00..55 in x5 steps); the release persists via ``clock_alarm_set_clock_alarm`` (the RTC reschedule stays off the drag path).
     - :cfunc:`settings_create_roller`, ``event_handler_cb_settings_alarm_time_save``

   * - 5
     - Range summary (Mo-Fr)
     - "Mo-Fr" for a range, "We" for a single day, "Fr-Mo" for the wrap case — the small range font (lowercase + the hyphen the SSID font lacks).
     - ``settings_alarm_time_range_text``

   * - 6
     - Exit → alarm settings
     - The shared bottom-centre exit back to the alarm screen.
     - :cfunc:`settings_sub_screen_prepare`
