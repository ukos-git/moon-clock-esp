The display trim
================

The display trim: a 0..359 house **roller** that sets the clock's **zero angle** — the rotation the whole screen carries when it is mounted on the wall at an angle that is not a multiple of 90°. Degrees clockwise from 12 o'clock, 0..359, persisted as the ``display_rotation_deg`` line of ``settings.txt`` (:cfunc:`settings_display_rotation_deg` / :cfunc:`settings_set_display_rotation_deg`, the store clamps to 0..359). The trim is the *user half* of the orientation base — :cfunc:`action_orientation_set_user_base_deg` adds it to the build-time ``UI_ORI_BASE_V10`` (now a single shared ``900`` on all three panels; times ``UI_CLOCK_ROLL_SIGN``) and re-pushes ``transform_rotation`` to every screen root, so **the screen under the finger tilts for real on release** — that live tilt is the calibration itself. Reached by the hub's rotate-arrow icon on all three family boards; the knob steps the same roller in 5° detents on the dial and commits with a tap.

The roller replaced the old rim-handle arc drag (2026-11-16): the thin ring handle at the panel rim was too fiddly to grab on a touch panel, so the value now lives in the house single-slot roller (the alarm/time/pomodoro picker, ``settings_create_roller``) — **the roller IS the value display**, no separate readout. Scrolling moves the working angle live (``LV_EVENT_VALUE_CHANGED`` tips the preview card); ``LV_EVENT_RELEASED`` persists and re-pushes.

.. figure:: ../img/screens/annot/settings-display_s3.png
   :alt: the display trim — the S3 panel (466, annotated)
   :align: center

   The display trim — S3 466×466 (the badges are the legend numbers; the roller centred on ``0`` = trim 0°)

.. figure:: ../img/screens/annot/settings-display_p4.png
   :alt: the display trim — the P4 panel (720, annotated)
   :align: center

   The display trim — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_display.png
   :alt: the display trim — the knob panel (360)
   :align: center

   The display trim — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; the dial steps the roller at focus 0, one detent = 5°, a tap commits)

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
     - Roller (0..359)
     - The house single-slot roller (``settings_create_roller``, ``LV_ROLLER_MODE_NORMAL``, one visible row, the 48 px numbers font) IS the value display — the centred white entry is the working angle, the greyed neighbour the next detent. Scroll moves the angle live; a release commits. On the knob the dial's ``dial_roller_pair`` maps this roller to focus 0 and steps it ±5° per detent.
     - :cfunc:`settings_create_roller` + ``event_handler_cb_settings_display_roller`` (VALUE_CHANGED)

   * - 3
     - Preview card + tip
     - A 118 px grey-outlined card, centred on the panel (NOT lower-centre — at +78 it overlapped the exit disk), whose WHITE TIP BAR (a child at top-centre, so the card's transform carries it) marks "up". The card tips with the trim by the very setter the pipeline uses — ``transform_rotation`` at 0.1° units, centre pivot — so the preview and the content agree by construction.
     - ``lv_obj_set_style_transform_rotation`` (the same call ``ori_apply_v10`` makes), driven by ``settings_display_apply``

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`

Commit semantics
----------------

Scrolling only moves working state; **release commits**: ``settings_set_display_rotation_deg`` + ``settings_store_save`` (the SPIFFS write, logged as ``display base trim N deg saved``) + :cfunc:`action_orientation_set_user_base_deg` (the immediate re-push to all roots) — all driven by ``event_handler_cb_settings_display_save`` on the roller's ``LV_EVENT_RELEASED``. The trim lands BEFORE the ``UI_CLOCK_ROLL_SIGN`` flip, so it reads as "degrees clockwise on the wall" on every board regardless of the sign. The 90°-step tilt roll (the IMU) stacks ON TOP: the trim is the base the step rotations rotate about. On the knob the dial steps the same roller (``dial_roller_pair`` focus 0) ±5° per detent and a click commits the same release path — ``screens_dial_commit`` sends ``LV_EVENT_RELEASED`` on ``s_display_roller``.

Durability
----------

The value **survives power loss** — plain unplug and soft reset alike: nothing formats the ``storage`` partition at boot, and ``settings_store_load`` re-reads the line before :cfunc:`action_orientation_init` seeds the pipeline from it. Bench-verified on the S3 2026-11-25: a ``read_flash`` of the storage partition showed the committed value (plus the SPIFFS garbage-collect history of every earlier save), and the next boot's log loaded it and applied it from frame one. **The one thing that resets it to 0 is a full ``idf.py flash``** — the partition is re-baked from the source tree's ``spiffs/`` (which carries no ``settings.txt``), the documented reset of the whole runtime-owned store: :doc:`the SPIFFS store <../reference/spiffs-store>`. If the trim "came back" to its default after what looked like a power cycle, a firmware flash had intervened. One caveat, knob: the knob's boot does not re-apply the stored trim (no ``action_orientation_init`` in its boot path, and its IMU is the fixed-upright stub) — the value is read back the same way and is applied live at the next release, but the display comes up untrimmed until then.
