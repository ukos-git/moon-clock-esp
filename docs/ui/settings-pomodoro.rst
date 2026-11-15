The pomodoro interval
=====================

Two shared clock-style rollers side by side (hours 0–23, minutes 0–59) seed from the stored interval rule (H = N / 60, M = N % 60; a sub-hour rule shows hours 0). The rollers **are** the display (the old separate "H:MM" preview label is gone); only a RELEASED persists (``minutes_total = H*60+M``, 0 rejected — the current rule stays), keeping the SPIFFS write + RTC reschedule off the drag path. The display sits exactly where the home clock's "HH:MM" would be (the hour slot's glyph centre at screen x 153, the minute slot at 303, the colon at 223 — the home label's own glyph centres).

.. figure:: ../img/screens/annot/settings-pomodoro_s3.png
   :alt: the pomodoro interval — the S3 panel (466, annotated)
   :align: center

   The pomodoro interval — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-pomodoro_p4.png
   :alt: the pomodoro interval — the P4 panel (720, annotated)
   :align: center

   The pomodoro interval — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_pomodoro.png
   :alt: the pomodoro interval — the knob panel (360)
   :align: center

   The pomodoro interval — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; raw panel capture, :doc:`the UI index <index>`)

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
     - The 32 px SSID font; the clock label has none — this screen needs one to say what the value IS.
     - :cfunc:`create_screen_settings_pomodoro`

   * - 2
     - Hour roller (00–23)
     - The 96 px bold-numbers font at 110 px tall, a 140 px wide box (Montserrat's digits are PROPORTIONAL — the worst-case "44" pair advances 132 px, which the old 120 px box clipped).
     - :cfunc:`settings_create_roller`, ``event_handler_cb_settings_pomodoro_released``

   * - 3
     - Minute roller (00–59)
     - Same style, the minute slot; the release persists via ``clock_alarm_set_pomodoro_interval`` (the RTC reschedule stays off the drag path).
     - :cfunc:`settings_create_roller`

   * - 4
     - Exit → alarm settings
     - The shared bottom-centre exit back to the CALLING level — the alarm screen (not the hub).
     - :cfunc:`settings_sub_screen_prepare`
