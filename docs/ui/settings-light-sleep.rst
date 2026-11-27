The idle timeout (light sleep)
==============================

One parameterized sleep-settings builder (the light and deep screens are near-identical): the **idle-timeout** roller — 1–30 **minutes**, stored internally as seconds (60–1800) — **centred** on its row (the row clears the exit button's band), with the icon-only **power-off kill switch** (a 96 px solid ``power-off`` glyph, ``UI_PX`` 24 px to the right of the roller box — the same "roller + icon toggle to the right" layout the retired dusk animation toggle held here until :commit:`f7740500`) at its right edge. The value is applied **live** via ``clock_idle_set_timeout_ms()``; the release persists the store.

The kill switch (the store key ``idle_timeout_enable``, default on) is the timer's **disable** — the settings range only shortens, it never turns off, so this is the only way to keep the device awake. Tapping the glyph flips the flag, saves the store, and recolours the glyph: **white = the idle timer is armed** (the historical behaviour), **grey = the transition is off**. While off, the roller is **locked** — no scroll, no commit (the ``RELEASED`` handler refuses; the knob's dial steps are refused at the dial layer too, where ``lv_roller_set_selected`` would bypass the LVGL disabled state) — and the selected row dims to moon grey. The gate is **live**: ``clock_idle_check_cb`` reads the flag every 1 s check, so the idle rung stops firing on the very next tick (and a re-enable resumes it with no re-arm — the tap that flipped the toggle already reset the activity clock, so it cannot instant-sleep). Re-entering the screen re-applies the lock (the per-entry refresh). The legacy ``dusk_anim_enable`` — like the retired boot sweep's ``boot_anim_enable`` — stays as a no-op, unread and unexposed.

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

   The idle timeout (light sleep) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; on the knob this same ``idle_timeout_sec`` key commits its own LEDC backlight dim, and the power-off toggle skips that dim rung — dial steps on the locked roller are refused at the dial layer)

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
     - The one roller, centred on its row (210 px wide, 96 px numbers font). Applied live to the idle timer; the store keeps the seconds (the screen shows minutes). **Locked** (no scroll/commit, selected row grey) while the kill switch is off.
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``idle_timeout_sec`` (the store), ``CLOCK_IDLE_TIMEOUT_SEC`` (the seed); the lock guard refuses a commit while ``idle_timeout_enable`` is off

   * - 3
     - Power-off toggle (the idle-timer kill switch)
     - 96 px solid ``power-off`` glyph (``GLYPH_POWER_OFF``, U+F011). **White = the idle timer is armed**, grey = off (the roller is locked). Tap flips ``idle_timeout_enable``, saves the store, and recolours + (un)locks — the same idiom as the alarm master toggle.
     - ``event_handler_cb_settings_sleep_toggle_clicked``; the live gate is ``clock_idle_check_cb`` (``settings_idle_timeout_enable()`` per check); on the knob the same key skips the dim rung of the power ladder

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
