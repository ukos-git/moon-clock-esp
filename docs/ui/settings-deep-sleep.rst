The sleep countdown (deep sleep)
================================

The twin of the :doc:`idle-timeout screen <settings-light-sleep>` (the same parameterized builder, ``screen = 1``): the **deep-sleep countdown** roller, in minutes, centred on its row, with the icon-only **power-off kill switch** (96 px solid ``power-off`` glyph, the same "roller + icon toggle to the right" layout) at its right edge. The countdown takes effect at the **next light-sleep entry** — the RTC one-shot is armed right there (the dusk spin that used to own the arming was retired :commit:`f7740500`; sleep depth follows the timer, :doc:`the sleep machine <../implementation/clock-sleep>`) and triggers the deep-sleep transition on expiry. The deep transition itself ends on the flat-black disk with the backlight off.

The kill switch (the store key ``deep_sleep_countdown_enable``, default on) is the countdown's **disable**. Tapping the glyph flips the flag, saves the store, and recolours it: **white = the countdown is armed**, **grey = it will not be armed and the roller is locked** (no scroll, no commit — the ``RELEASED`` handler refuses; on the knob the dial steps are refused at the dial layer too; the selected row dims to moon grey). With the countdown off the device **stays dimmed in light sleep indefinitely** — the entry logs "not armed, staying in light sleep" — and the existing touch/tilt wake paths still bring it home. The gate is **live at the arming site**: the flag is read when the next light-sleep entry would arm the one-shot, so a re-enable takes effect at that very entry, and a disable at the next. Re-entering the screen re-applies the lock (the per-entry refresh).

.. figure:: ../img/screens/annot/settings-deep-sleep_s3.png
   :alt: the sleep countdown (deep sleep) — the S3 panel (466, annotated)
   :align: center

   The sleep countdown (deep sleep) — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/settings-deep-sleep_p4.png
   :alt: the sleep countdown (deep sleep) — the P4 panel (720, annotated)
   :align: center

   The sleep countdown (deep sleep) — P4 720×720 (the same UI_PX layout, scaled)

.. figure:: ../img/screens/knob/settings_deep_sleep.png
   :alt: the sleep countdown (deep sleep) — the knob panel (360)
   :align: center

   The sleep countdown (deep sleep) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; on the knob this same ``deep_sleep_countdown_sec`` key arms its RTC TIMER deep sleep, and the power-off toggle skips that rung — the board dims and stays dim)

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
     - Minutes on the screen, seconds in the store. Takes effect at the next light-sleep entry (the entry itself arms the one-shot, :doc:`the sleep machine <../implementation/clock-sleep>`). **Locked** (no scroll/commit, selected row grey) while the kill switch is off.
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``deep_sleep_countdown_sec`` (the store), ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the seed); the lock guard refuses a commit while ``deep_sleep_countdown_enable`` is off

   * - 3
     - Power-off toggle (the countdown kill switch)
     - 96 px solid ``power-off`` glyph (``GLYPH_POWER_OFF``, U+F011). **White = the countdown is armed**, grey = it will not be armed (the device stays dimmed in light sleep; the roller is locked). Tap flips ``deep_sleep_countdown_enable``, saves the store, and recolours + (un)locks — the same idiom as the alarm master toggle.
     - ``event_handler_cb_settings_sleep_toggle_clicked``; the live gate wraps the sole arming site in ``action_switch_to_light_sleep_screen`` (``settings_deep_sleep_countdown_enable()``); on the knob the same key skips the ladder's deep-sleep rung

   * - 4
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
