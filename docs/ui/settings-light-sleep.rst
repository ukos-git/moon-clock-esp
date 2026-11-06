The idle timeout (light sleep)
==============================

One parameterized sleep-settings builder (the light and deep screens are near-identical): the **idle-timeout** roller — 1–30 **minutes**, stored internally as seconds (60–1800) — and the icon-only **dusk animation toggle** on the same horizontal line (roller left of centre at −90, toggle right at +90; the row clears the exit button's band). The value is applied **live** via ``clock_idle_set_timeout_ms()``; the release persists the store. The toggle (a 96 px regular moon glyph, white on / grey off — no switch widget, ``LV_USE_SWITCH`` is off) is the *sleep-transition* animation (the dusk pole spin); the boot sweep's ``boot_anim_enable`` flag is a BOOT animation and is not exposed here.

.. figure:: ../img/screens/annot/settings-light-sleep_s3.png
   :alt: the idle timeout (light sleep) — the S3 panel (466, annotated)
   :align: center

   The idle timeout (light sleep) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-light-sleep_p4.png
   :alt: the idle timeout (light sleep) — the P4 panel (720, annotated)
   :align: center

   The idle timeout (light sleep) — P4 720×720 (the same UI_PX layout, scaled)

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
     - Caption (IDLE TIMEOUT)
     - The 32 px SSID font; the deep-sleep screen's twin reads SLEEP COUNTDOWN.
     - :cfunc:`settings_create_sleep_screen` (``screen = 0``)

   * - 2
     - Idle timeout (1–30 min)
     - The one roller on the shared line (210 px wide, 96 px numbers font). Applied live to the idle timer; the store keeps the seconds (the screen shows minutes).
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``idle_timeout_sec`` (the store), ``CLOCK_IDLE_TIMEOUT_SEC`` (the seed)

   * - 3
     - Dusk animation toggle
     - The 96 px regular (unfilled) moon glyph in a 100 px clickable box; a tap flips the flag, saves, and recolors. Both sleep screens show the ONE sleep-transition toggle (the deep transition ends on the flat-black disk, so it carries no separate animation).
     - :cfunc:`settings_anim_toggle`, ``event_handler_cb_settings_anim_toggle`` — option ``dusk_anim_enable`` (the store), ``UI_MOON_DUSK_ENABLE`` (the seed)

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
