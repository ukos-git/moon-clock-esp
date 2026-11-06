Manual time entry (P4)
======================

Manual time entry — **P4 only**: the S3's PCF85063A is the time base and needs no manual entry, while the P4 (the crystal RTC timer + SNTP) carries the clock icon first on its hub ring (7 icons) to reach this screen. Two rows: the **time** HH:MM (the 96 px clock font, at the home clock label's position — no seconds: the home label shows none either, and adjusting them here would have no visible effect) and the **date** DD.MM.YY (the 48 px font, below). Releasing any ONE roller commits the whole value (the exit button's click commits it too, ``event_handler_cb_settings_time_exit``).

.. figure:: ../img/screens/annot/settings-time_s3.png
   :alt: manual time entry (p4) — the S3 panel (466, annotated)
   :align: center

   Manual time entry (P4) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-time_p4.png
   :alt: manual time entry (p4) — the P4 panel (720, annotated)
   :align: center

   Manual time entry (P4) — P4 720×720 (the same UI_PX layout, scaled)

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
     - Caption (SET TIME / DATE)
     - The 32 px SSID font (this screen shows a value the clock label would not).
     - :cfunc:`create_screen_settings_time`

   * - 2
     - Time row HH:MM
     - The hour roller at −75, the minute at +75, the colon at centre (the 140 px boxes, 96 px numbers font, at the home clock's position).
     - ``event_handler_cb_settings_time_commit`` (RELEASED)

   * - 3
     - Date row DD.MM.YY
     - Day −75, month 0, year +75, the dots at ±37 (the 70 px 48 px-font rollers, two-digit year 00–99 = 2000+YY), below the time row and clear of the exit band.
     - ``event_handler_cb_settings_time_commit`` (RELEASED)

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub — its click also commits the entered value.
     - ``event_handler_cb_settings_time_exit``
