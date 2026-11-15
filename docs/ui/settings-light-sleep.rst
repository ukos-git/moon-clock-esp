The idle timeout (light sleep)
==============================

One parameterized sleep-settings builder (the light and deep screens are near-identical): the **idle-timeout** roller — 1–30 **minutes**, stored internally as seconds (60–1800) — alone and **centred** on its row (the row clears the exit button's band). The value is applied **live** via ``clock_idle_set_timeout_ms()``; the release persists the store. The icon-only **dusk animation toggle** (a 96 px regular moon glyph) that used to share this row left with the dusk itself on 2026-11-13 — light sleep is a static dim frame now, so there is no sleep-transition animation to toggle. The store's ``dusk_anim_enable`` — like the retired boot sweep's ``boot_anim_enable`` — stays as a legacy no-op, unread and unexposed.

.. figure:: ../img/screens/annot/settings-light-sleep_s3.png
   :alt: the idle timeout (light sleep) — the S3 panel (466, annotated)
   :align: center

   The idle timeout (light sleep) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-light-sleep_p4.png
   :alt: the idle timeout (light sleep) — the P4 panel (720, annotated)
   :align: center

   The idle timeout (light sleep) — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_light_sleep.png
   :alt: the idle timeout (light sleep) — the knob panel (360)
   :align: center

   The idle timeout (light sleep) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; on the knob this same ``idle_timeout_sec`` key commits its own LEDC backlight dim)

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
     - The one roller, centred on its row (210 px wide, 96 px numbers font). Applied live to the idle timer; the store keeps the seconds (the screen shows minutes).
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``idle_timeout_sec`` (the store), ``CLOCK_IDLE_TIMEOUT_SEC`` (the seed)

   * - 3
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
