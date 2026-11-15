The alarm sound
===============

The alarm sound: one roller over the four baked loops (two-tone / beep / bell / arpeggio — the ES8311 codec module, :cfile:`components/ui/alarm_audio.h`'s ``ALARM_SOUND_COUNT`` = 4; no board gate on the screen itself). Releasing the roller applies + persists the selection and **plays a preview**; the speaker button re-hears the standing choice without touching the roller. Reached by the hub's speaker icon on all three family boards — the two clocks have the ES8311; the knob's roller still applies and persists its choice, but its preview plays through the silent ``esp_codec_dev`` shim (its figure below). On the P4 this is the screen the ``TEST_ALARM_RING_SEC`` hook proves end to end (the codec's open/stream/ramp/fade/close/re-open cycle).

.. figure:: ../img/screens/annot/settings-alarm-sound_s3.png
   :alt: the alarm sound — the S3 panel (466, annotated)
   :align: center

   The alarm sound — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-alarm-sound_p4.png
   :alt: the alarm sound — the P4 panel (720, annotated)
   :align: center

   The alarm sound — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_alarm_sound.png
   :alt: the alarm sound — the knob panel (360)
   :align: center

   The alarm sound — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; the preview plays nothing here — the knob's ``esp_codec_dev`` shim is silent)

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
     - Caption (ALARM SOUND)
     - The 32 px SSID font.
     - :cfunc:`create_screen_settings_alarm_sound`

   * - 2
     - Sound roller (4 loops)
     - The four loop names (the 32 px SSID font), left of centre to make room for the preview button on the same line; the selected index IS the sound id (no string parsing).
     - ``event_handler_cb_settings_alarm_sound_commit`` → ``alarm_audio_select`` + ``alarm_audio_preview`` — option ``alarm_sound_id`` (the store), ``SETTINGS_ALARM_SOUND_DEFAULT`` (the seed)

   * - 3
     - Preview (speaker)
     - The solid speaker glyph (U+F028) in a transparent 100 px box (the anim-toggle pattern); plays the currently selected loop once.
     - ``event_handler_cb_settings_alarm_sound_preview``

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
