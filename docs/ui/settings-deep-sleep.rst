The sleep countdown (deep sleep)
================================

The twin of the :doc:`idle-timeout screen <settings-light-sleep>` (the same parameterized builder, ``screen = 1``): the **deep-sleep countdown** roller, in minutes. The countdown takes effect at the **next dusk arming** — it is armed on the dusk's *last frame* (the sleep depth follows the animation, never a fixed timer at the sleep entry) and triggers the deep-sleep transition on expiry. The deep transition itself ends on the flat-black disk with the backlight off, so this screen shows the same one sleep-transition (dusk) toggle as the light-sleep screen — the solid moon glyph for the countdown entry, the regular one for the idle timeout (the hub's two sleep entries use the same weight distinction).

.. figure:: ../img/screens/annot/settings-deep-sleep_s3.png
   :alt: the sleep countdown (deep sleep) — the S3 panel (466, annotated)
   :align: center

   The sleep countdown (deep sleep) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-deep-sleep_p4.png
   :alt: the sleep countdown (deep sleep) — the P4 panel (720, annotated)
   :align: center

   The sleep countdown (deep sleep) — P4 720×720 (the same UI_PX layout, scaled)

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
     - Caption (SLEEP COUNTDOWN)
     - The deep-sleep entry's caption (the light-sleep screen reads IDLE TIMEOUT).
     - :cfunc:`settings_create_sleep_screen` (``screen = 1``)

   * - 2
     - Sleep countdown (1–30 min)
     - Minutes on the screen, seconds in the store. Takes effect at the next dusk arming (the dusk's last frame arms the countdown, :doc:`the sleep machine <../implementation/clock-sleep>`).
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``deep_sleep_countdown_sec`` (the store), ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the seed)

   * - 3
     - Dusk animation toggle
     - The 96 px **solid** moon glyph (the deep-sleep entry's weight); the one sleep-transition toggle, shared with the light-sleep screen.
     - :cfunc:`settings_anim_toggle` — option ``dusk_anim_enable``

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
