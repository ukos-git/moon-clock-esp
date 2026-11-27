The real alarm (time + days)
============================

.. brief:
   Reader: a firmware developer working on alarms.
   They already know: C, LVGL arcs and rollers, the alarm rules screen.
   After this page they can: explain how the day-range arc maps touches to days and where the alarm saves.
   Page type: reference.

The real-alarm screen sets a fixed alarm time and the range of weekdays it rings on.
The alarm-clock entry on the :doc:`alarm rules screen <settings-alarm>` opens it.

How it behaves
--------------

Two rollers set the time.
The minute roller moves in 5-minute steps, because 10:03 is not a wake-up time.
A release on either roller saves the alarm.

The day-range arc is the upper half-circle at the panel rim, with Monday on the left and Sunday on the right.
Two draggable handles mark the start and end of the range.
A press on a handle snaps it to the nearest day under the touch point, and a drag moves it.
The "Mo-Fr" summary above the time states the range in words.

Screenshots
-----------

.. figure:: ../img/screens/annot/settings-alarm-time_s3.png
   :alt: The real-alarm screen on the S3 panel, with numbered callouts on the day arc, both handles, the rollers, the range summary and the exit.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/settings-alarm-time_p4.png
   :alt: The real-alarm screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/settings_alarm_time.png
   :alt: The real-alarm screen on the knob's 360 px panel, without callouts.
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
     - Day arc (a press snaps to a day)
     - The upper half-circle track (r 224..233, the stroke inked inward from the panel half, touching the rim). The indicator, in moon grey, spans the handles. The arc is not clickable: presses land on the handles, and a press anywhere snaps to the nearest day under the finger.
     - ``alarm_time_point_to_day`` (the inverse-transform idiom), ``event_handler_cb_settings_alarm_time_handle``

   * - 2
     - Range start handle (Mon)
     - The range's start: a 34 px knob centred at r 218, 10 px inside the band's midline, so the disc pokes through both ink edges and stays visible on track and range. Press and drag moves the start; the press locks to the finger (``PRESS_LOCK``).
     - ``event_handler_cb_settings_alarm_time_handle`` (idx 0)

   * - 3
     - Range end handle (Fri)
     - The range's end. A single-day range draws a 6° sliver, because an arc cannot draw a zero-length span.
     - ``event_handler_cb_settings_alarm_time_handle`` (idx 1)

   * - 4
     - Hour / minute rollers
     - The time, at the home clock's position. The minute roller's options are 00..55 in steps of 5. The release saves through ``clock_alarm_set_clock_alarm``; the RTC reschedule stays off the drag path.
     - :cfunc:`settings_create_roller`, ``event_handler_cb_settings_alarm_time_save``

   * - 5
     - Range summary (Mo-Fr)
     - "Mo-Fr" for a range, "We" for a single day, "Fr-Mo" for the wrap case. It uses the small range font (lowercase plus the hyphen the SSID font lacks).
     - ``settings_alarm_time_range_text``

   * - 6
     - Exit → alarm settings
     - The shared bottom-centre exit, back to the alarm screen.
     - :cfunc:`settings_sub_screen_prepare`

Implementation notes
--------------------

The arc is a thin ring right at the rim: a pad-0 466×466 object.
Its ``get_center`` radius is therefore the panel half, and the ``UI_PX(10)`` stroke is inked inward from it, to 224..233.
It touches the round edge, unlike the home seconds arc, whose ``UI_PX(10)`` pad insets the stroke's outer edge to 223.
It stays well clear of the exit button's band.

Day d (0 = Monday) sits at 181 + 30d degrees.
The +1° offset keeps every stored arc angle off the 360/0 wrap edge.

The dark track (``LV_PART_MAIN``) spans the whole arc, and the moon-colour indicator spans the two handles.
The vendored arc widget has a single knob, so the range endpoints are plain objects on the ring and the arc itself is not clickable.
The range summary carries the day meaning, so the ring can own the outer band without day letters.

The computed geometry of this screen is the bottom row of the first figure in :ref:`menu-geometry`.

Where to go next
----------------

- :doc:`The pomodoro interval <settings-pomodoro>`: the other rule kind and the scheduler arithmetic.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: how the saved rule fires.
- :ref:`menu-geometry`: the computed per-panel layout.
