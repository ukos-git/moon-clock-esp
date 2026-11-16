The alarm (ringing) screen
==========================

The alarm screen is the ringing state: the clock, the alarm indicator, and the alarm audio (the ES8311 codec, :cfile:`components/ui/alarm_audio.h`'s four loops, selectable in :doc:`settings-alarm-sound <settings-alarm-sound>`). Any touch dismisses it — a dedicated input-device callback rather than the idle-wake path, which only fires while the device is idle (a tap while awake would just reset the idle timer). The alarm itself is armed by the scheduler — the PCF85063A hardware alarm on the S3, the software ``esp_timer`` one-shot on the P4 — and a zero-rule boot must disarm any stale armed ALM or a stale time fires a phantom alarm (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).

One alarm never rings forever: the ``UI_ALARM_RING_MAX_MIN`` hard cap (default 30 min, 1–240) enforces itself in TWO layers. The ``alarm_audio`` play_task breaks on its OWN elapsed deadline (``RING_CAP_MS``, :cfile:`components/ui/alarm_audio.c:62-65`, checked at :264) — unconditional, the audio stops even if the UI is parked in a sleep state. And the UI tick auto-dismisses at the same cap (:cfile:`components/ui/actions.c:778-799`, timestamp armed at ring start :1186): the ring is silenced, the alarm-active state is cleared, and — only when someone is actually at the panel — the alarm screen hands over to the home screen; while idle the cap merely silences, no panel lights up for an empty room and the sleep ladder keeps ownership. The stop is idempotent — usually the audio deadline got there first — and it rides the same stop path as the touch dismiss (the dedicated indev callback ``actions.c:1246``; the asleep side through ``action_wake_screen`` :1376).

.. figure:: ../img/screens/annot/alarm_s3.png
   :alt: the alarm (ringing) screen — the S3 panel (466, annotated)
   :align: center

   The alarm (ringing) screen — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/alarm_p4.png
   :alt: the alarm (ringing) screen — the P4 panel (720, annotated)
   :align: center

   The alarm (ringing) screen — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/alarm.png
   :alt: the alarm (ringing) screen — the knob panel (360)
   :align: center

   The alarm (ringing) screen — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; raw panel capture, :doc:`the UI index <index>`)

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

The ring chain (ES8311)
-----------------------

The audio side of the ringing state is :cfile:`components/ui/alarm_audio.c`:
four baked loops in PSRAM, one short-lived player task per ring or preview,
and a blocking-I2S stream that paces its own volume ramp. The two AMOLED
clocks carry the codec + speaker amp; the knob has neither and answers the
same header with silent no-ops (``knob/components/knob_ui/knob_alarm_audio_stub.c``)
so the shared :cfile:`actions.c` links honestly on all three boards. Both
init paths (codec NULL, loops malloc) soft-fail like the IMU — the alarm
stays display-only and the sound selection still remembers its choice.

.. graphviz:: ../_graphs/alarm-audio.dot
   :alt: The alarm ring chain — boot init, four PSRAM loops, ring into the player task, self-performed fade, knob stub
   :caption: The whole chain sealed: boot ``alarm_audio_init`` joins the ES8311 to the I2C bus the touch/IMU/RTC already run on and bakes the four 1 s loops (176 400 B of PSRAM); an alarm fires ONE player task at a time; the fade-out is the task's own job, and the knob's silent stub closes the chain.
   :align: center

.. graphviz:: ../_graphs/alarm-audio-loops.dot
   :alt: The four baked loops — Two-Tone A5/E5, Beep 2 kHz, Bell with partials, Arpeggio C5-E5-G5-C6
   :caption: The loops in full — 0 Two-Tone A5/E5 (880/659.26 Hz, 4×250 ms), 1 Beep (2 kHz square, 3×200 ms bursts), 2 Bell (880 Hz + 1760/2640/3520 partials, exponential decay τ = 350 ms), 3 Arpeggio (C5-E5-G5-C6, 4×250 ms); mono s16 @22 050 with 10 ms attack/release against clicks, and the knob's stub answers all four names honestly as "Silent".
   :align: center

.. graphviz:: ../_graphs/alarm-audio-play.dot
   :alt: The player task — ring or preview into codec open, blocking 1024-sample chunks pace the ramp, dismiss only raises the stop flag
   :caption: The player opened: ring_start spawns a 6 KiB prio-4 task (vol 75, ramp 2500 ms; the preview is the same player, one pass, 300 ms ramp — ``s_busy`` makes them mutually exclusive); the BLOCKING ``esp_codec_dev_write`` of 1024-sample (~47 ms) chunks IS the clock, so the wall-time-computed ramp can never outrun the sound; the touch dismiss never touches the codec — it only raises the stop flag — and the task then fades itself −5 every 10 ms (~150 ms), closes the DAC and deletes itself.
   :align: center

The scheduler that *arms* the fire (``alarm.txt``, the PCF85063A ALM pin on
the S3, the ``esp_timer`` one-shot on the P4) is sketched in
:doc:`the clock and sleep machine <../implementation/clock-sleep>`; this
figure picks the story up at ``action_alarm_triggered``. The selection screen
and its persisted ``alarm_sound_id=`` line are :doc:`settings-alarm-sound`.
