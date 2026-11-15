The settings hub
================

The settings root: a black screen, a central concentric exit (the 150 px circle-xmark ring) back to the calling level, and a ring of sub-screen icons at EQUAL arc distances — 360/n degrees apart (S3: 6 icons × 60°, P4: 7 icons × ~51.4°; the P4 ring opens with the clock icon for manual time entry). The deep-sleep icon is anchored at the bottom (180°) and found **by target**, not by index, so the anchor cannot drift when the ring's count differs between the boards. Moon weight distinguishes the two sleep screens (regular = light sleep, solid = deep sleep); the ring radius is ``UI_PX(140)`` (216 on the P4) — balanced against the 96 px exit disk with equal breathing room both sides.

.. figure:: ../img/screens/annot/settings-hub_s3.png
   :alt: the settings hub — the S3 panel (466, annotated)
   :align: center

   The settings hub — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-hub_p4.png
   :alt: the settings hub — the P4 panel (720, annotated)
   :align: center

   The settings hub — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_hub.png
   :alt: the settings hub — the knob panel (360)
   :align: center

   The settings hub — knob 360×360 at rest (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; six icons like the S3, centre exit white)

.. figure:: ../img/screens/knob/settings_hub_dial.png
   :alt: the settings hub with the dial cursor focused — the knob panel (360)
   :align: center

   The same hub after one clockwise dial detent — ``screens_dial_step(+1)``: the focused ring icon goes white (grey = unfocused), the central exit carries the focus ring when it is the cursor. Touch the knob's EC1 encoder, the cursor rides the shared handlers (:ref:`dial-semantics-knob <dial-semantics-knob>`)

The wheel keyboard (:doc:`settings-wifi-password <settings-wifi-password>`) has no hub icon of its own — it is reached by tapping an AP row on the WiFi screen, and its only way back is the centre long press.

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
     - Exit (centre circle-xmark)
     - Back to the CALLING level — the airplane screen for the normal path. The exit loads the home screen only; it kicks off no sync (the sync triggers are the hourly gate, the wake path on the clocks, and the keyboard commit — :doc:`the transient WiFi screen <wifi>`).
     - ``event_handler_cb_hub_exit``, :cfunc:`settings_hub_icon`'s sibling :cfunc:`settings_create_exit_button`

   * - 2
     - Alarm sound (speaker, solid)
     - The 4-loop sound picker (both boards — the ES8311 is on both).
     - :doc:`settings-alarm-sound <settings-alarm-sound>` — option ``alarm_sound_id``

   * - 3
     - Alarm (clock, regular)
     - The alarm rules screen (the master toggle + the two entries).
     - :doc:`settings-alarm <settings-alarm>`

   * - 4
     - Light sleep (regular moon)
     - The idle-timeout picker (1–30 min).
     - :doc:`settings-light-sleep <settings-light-sleep>` — option ``idle_timeout_sec``

   * - 5
     - Deep sleep (solid moon)
     - The sleep-countdown picker (1–30 min).
     - :doc:`settings-deep-sleep <settings-deep-sleep>` — option ``deep_sleep_countdown_sec``

   * - 6
     - WiFi
     - The AP scan list (a background scan runs while the screen is open).
     - :doc:`settings-wifi <settings-wifi>`

   * - 7
     - Timezone (globe, solid)
     - The UTC-offset / Europe-Berlin picker.
     - :doc:`settings-tz <settings-tz>` — option ``timezone_idx``
