The display trim
================

The display trim: a full-circle arc slider that sets the clock's **zero angle** — the rotation the whole screen carries when it is mounted on the wall at an angle that is not a multiple of 90°. Degrees clockwise from 12 o'clock, 0..359, persisted as the ``display_rotation_deg`` line of ``settings.txt`` (:cfunc:`settings_display_rotation_deg` / :cfunc:`settings_set_display_rotation_deg`, the store clamps to 0..359). The trim is the *user half* of the orientation base — :cfunc:`action_orientation_set_user_base_deg` adds it to the build-time ``UI_ORI_BASE_V10`` (times ``UI_CLOCK_ROLL_SIGN``) and re-pushes ``transform_rotation`` to every screen root, so **the screen under the finger tilts for real on release** — that live tilt is the calibration itself. Reached by the hub's rotate-arrow icon on all three family boards; the knob dials it with 5° detents instead of a drag.

.. figure:: ../img/screens/annot/settings-display_s3.png
   :alt: the display trim — the S3 panel (466, annotated)
   :align: center

   The display trim — S3 466×466 (the badges are the legend numbers; handle at 0° = 12 o'clock)

.. figure:: ../img/screens/annot/settings-display_p4.png
   :alt: the display trim — the P4 panel (720, annotated)
   :align: center

   The display trim — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_display.png
   :alt: the display trim — the knob panel (360)
   :align: center

   The display trim — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; the dial edits the handle at focus 0, one detent = 5°)

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
     - Caption (ZERO ANGLE)
     - The 32 px SSID font, the theme's grey.
     - :cfunc:`create_screen_settings_display`

   * - 2
     - Value ring + handle
     - The full 360° track right at the rim (stroke inked INWARD from panel/2, the alarm-time band's zeroed-pad pattern); the indicator spans arc ``270..(270+deg)%360`` — the wrap case draws through 360/0 exactly like the alarm day-range band. The 34 px handle rides the ring at the angle and IS the editor: a press + drag moves the angle under the finger (``PRESS_LOCK`` keeps the grab while the finger wanders off the ring; the indev point is mapped through the INVERSE root transform, because after a commit the whole screen — ring included — is tilted).
     - ``event_handler_cb_settings_display_handle`` + ``display_point_to_deg``; arc space is atan2 y-down (0 = right, 90 = down) = user degrees shifted by −90

   * - 3
     - Preview card + tip
     - A 140 px grey-outlined card whose WHITE TIP BAR (a child at top-centre, so the card's transform carries it) marks "up". The card tips with the trim by the very setter the pipeline uses — ``transform_rotation`` at 0.1° units, centre pivot — so the preview and the content agree by construction.
     - ``lv_obj_set_style_transform_rotation`` (the same call ``ori_apply_v10`` makes)

   * - 4
     - Readout
     - The whole-degree value (``"N DEG"``, the 48 px SSID font, white) — a SIBLING of the card, so it stays upright while the card tilts.
     - ``settings_display_apply``

   * - 5
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`

Commit semantics
----------------

The drag only moves working state; **release commits**: ``settings_set_display_rotation_deg`` + ``settings_store_save`` (the SPIFFS write, logged as ``display base trim N deg saved``) + :cfunc:`action_orientation_set_user_base_deg` (the immediate re-push to all roots). The trim lands BEFORE the ``UI_CLOCK_ROLL_SIGN`` flip, so it reads as "degrees clockwise on the wall" on every board regardless of the sign. The 90°-step tilt roll (the IMU) stacks ON TOP: the trim is the base the step rotations rotate about. On the knob the dial's ring handle (focus 0) steps ±5° per detent and a click commits the same release path (:c:func:`event_handler_cb_dial_display_press` focuses, the commit case sends ``LV_EVENT_RELEASED`` on the handle).
