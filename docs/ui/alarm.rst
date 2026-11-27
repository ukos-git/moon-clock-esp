The alarm (ringing) screen
==========================

.. brief:
   Reader: a firmware developer working on alarms or audio.
   They already know: C, LVGL basics, FreeRTOS tasks.
   After this page they can: trace a ring from the trigger to the speaker and back to silence.
   Page type: reference.

The alarm screen shows while an alarm rings.
It shows the clock and the alarm indicator, and the speaker plays the chosen alarm sound.

How it behaves
--------------

Any touch dismisses the alarm.
A dedicated input-device callback handles the touch, not the idle-wake path.
The idle-wake path fires only while the clock is idle; a tap while awake would only reset the idle timer.

The sound is one of the four loops in :cfile:`components/ui/alarm_audio.h`, played through the ES8311 codec and chosen on :doc:`settings-alarm-sound <settings-alarm-sound>`.

An alarm never rings forever.
The ``UI_ALARM_RING_MAX_MIN`` hard cap (default 30 min, range 1–240) stops it, and the cap works in two layers:

- The ``alarm_audio`` play task stops on its own elapsed deadline (``RING_CAP_MS``, defined at :clabel:`components/ui/alarm_audio.c:65` and checked at :clabel:`components/ui/alarm_audio.c:264`).
  This check is unconditional, so the audio stops even if the UI is parked in a sleep state.
- The UI tick auto-dismisses at the same cap (:clabel:`components/ui/actions.c:789`, with the timestamp set at ring start, :clabel:`components/ui/actions.c:1190`).
  It silences the ring and clears the alarm-active state.
  Only when someone is at the panel does the alarm screen hand over to the home screen.
  While the clock is idle the cap only silences the ring: no panel lights up for an empty room, and the sleep ladder keeps control.

The stop is idempotent; usually the audio deadline gets there first.
It uses the same stop path as the touch dismiss (the dedicated input-device callback ``alarm_indev_read_cb``, :clabel:`components/ui/actions.c:1247`; the asleep side goes through :cfunc:`action_wake_screen`, :clabel:`components/ui/actions.c:1377`).

Screenshots
-----------

.. figure:: ../img/screens/annot/alarm_s3.png
   :alt: The alarm screen on the S3 panel, with numbered callouts on each element.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/alarm_p4.png
   :alt: The alarm screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/alarm.png
   :alt: The alarm screen on the knob's 360 px panel, without callouts.
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
     - Clock HH:MM
     - The shared widget at its normal position. The minute tick keeps it current while the alarm rings.
     - :cfunc:`create_screen_alarm`

   * - 2
     - Alarm indicator
     - The glyph below the clock at (185, 320): the ``screen-indicator-alarm`` object, in the regular-weight font.
     - :cfunc:`create_screen_alarm`

   * - 3
     - Any touch dismisses
     - A dedicated input-device callback, not the idle-wake path. It disarms the alarm, fades out the audio and switches back to the current moon screen.
     - ``action_register_alarm_touch`` (:cfile:`components/ui/actions.c`); option: the ``alarm.txt`` rules

Implementation notes
--------------------

The scheduler arms each alarm: the PCF85063A hardware alarm on the S3, a software ``esp_timer`` one-shot on the P4.
A boot with zero rules must disarm any stale armed ALM, or a stale time fires a phantom alarm (:doc:`the clock and sleep machine <../implementation/clock-sleep>`).

The ring chain (ES8311)
~~~~~~~~~~~~~~~~~~~~~~~

The audio side of the ringing state is :cfile:`components/ui/alarm_audio.c`.
It holds four baked loops in PSRAM (the external pseudo-static RAM), starts one short-lived player task per ring or preview, and streams over blocking I2S, which paces its own volume ramp.

The two AMOLED clocks have the codec and a speaker amplifier.
The knob has neither: it answers the same header with silent no-ops (``knob/components/knob_ui/knob_alarm_audio_stub.c``), so the shared :cfile:`actions.c` links on all three boards.
Both init steps (opening the codec, allocating the loops) fail softly, like the IMU: on failure the alarm stays display-only and the sound selection still remembers its choice.

.. graphviz:: ../_graphs/alarm-audio.dot
   :alt: The alarm ring chain: boot init, the four PSRAM loops, a ring starting the player task, the task's own fade, and the knob's silent stub.
   :caption: The whole chain sealed. Boot ``alarm_audio_init`` joins the ES8311 to the I2C bus that the touch, IMU and RTC already use, and bakes the four 1 s loops (176 400 B of PSRAM). An alarm starts one player task at a time; the fade-out is the task's own job, and the knob's silent stub closes the chain.
   :align: center

.. graphviz:: ../_graphs/alarm-audio-loops.dot
   :alt: The four baked loops: Two-Tone A5/E5, Beep at 2 kHz, Bell with partials, and Arpeggio C5-E5-G5-C6.
   :caption: The four loops: 0 Two-Tone A5/E5 (880/659.26 Hz, 4×250 ms), 1 Beep (2 kHz square, 3×200 ms bursts), 2 Bell (880 Hz plus 1760/2640/3520 partials, exponential decay τ = 350 ms), 3 Arpeggio (C5-E5-G5-C6, 4×250 ms). All are mono s16 at 22 050 Hz with 10 ms attack and release against clicks. The knob's stub reports all four names as "Silent".
   :align: center

The player task is 6 KiB at priority 4, with volume 75 and a 2500 ms ramp.
The preview uses the same player for one pass with a 300 ms ramp; ``s_busy`` makes ring and preview mutually exclusive.
The blocking ``esp_codec_dev_write`` of 1024-sample chunks (about 47 ms each) is the task's clock, so the ramp, computed from wall time, can never run ahead of the sound.
The touch dismiss never touches the codec.
It only raises the stop flag; the task then fades itself by −5 every 10 ms (about 150 ms), closes the DAC and deletes itself.

.. graphviz:: ../_graphs/alarm-audio-play.dot
   :alt: The player task: ring or preview into codec open, blocking 1024-sample chunks pacing the ramp, and the dismiss raising only the stop flag.
   :caption: The player task opened. Read it from the ring or preview start at the top to the self-delete at the bottom; the dismiss enters only through the stop flag.
   :align: center

Where to go next
----------------

- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the scheduler that arms the fire (``alarm.txt``, the PCF85063A ALM pin on the S3, the ``esp_timer`` one-shot on the P4). The figures above start at ``action_alarm_triggered``.
- :doc:`settings-alarm-sound`: the selection screen and its saved ``alarm_sound_id=`` line.
- :doc:`Alarm rules <settings-alarm>`: where the user sets the alarms.
