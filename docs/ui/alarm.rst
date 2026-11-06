The alarm (ringing) screen
==========================

The alarm screen is the ringing state: the clock, the alarm indicator, and the alarm audio (the ES8311 codec, :cfile:`components/ui/alarm_audio.h`'s four loops, selectable in :doc:`settings-alarm-sound <settings-alarm-sound>`). Any touch dismisses it — a dedicated input-device callback rather than the idle-wake path, which only fires while the device is idle (a tap while awake would just reset the idle timer). The alarm itself is armed by the scheduler — the PCF85063A hardware alarm on the S3, the software ``esp_timer`` one-shot on the P4 — and a zero-rule boot must disarm any stale armed ALM or a stale time fires a phantom alarm (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).

.. figure:: ../img/screens/annot/alarm_s3.png
   :alt: the alarm (ringing) screen — the S3 panel (466, annotated)
   :align: center

   The alarm (ringing) screen — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/alarm_p4.png
   :alt: the alarm (ringing) screen — the P4 panel (720, annotated)
   :align: center

   The alarm (ringing) screen — P4 720×720 (the same UI_PX layout, scaled)

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
     - Clock HH:MM
     - The shared widget at its normal position; the minute tick keeps it current while the alarm rings.
     - :cfunc:`create_screen_alarm`

   * - 2
     - Alarm indicator
     - The glyph below the clock at (185, 320) (the ``screen-indicator-alarm`` object, the regular-weight font).
     - :cfunc:`create_screen_alarm`

   * - 3
     - Any touch dismisses
     - A dedicated indev callback (not the idle-wake path) — the disarm + audio fade-out + the switch back to the current moon screen.
     - ``action_register_alarm_touch`` (:cfile:`components/ui/actions.c`) — option: the ``alarm.txt`` rules
