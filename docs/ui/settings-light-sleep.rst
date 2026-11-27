The idle timeout (light sleep)
==============================

.. brief:
   Reader: a firmware developer working on the sleep settings.
   They already know: C, LVGL rollers, the light-sleep state.
   After this page they can: say how the idle timeout is set, applied and disabled.
   Page type: reference.

The idle-timeout screen sets how long the clock stays awake without input before it dims into light sleep.
It can also switch the idle timer off.
The hub's regular moon icon opens it.

How it behaves
--------------

One roller sets the timeout in minutes, 1–30.
The store keeps it in seconds (60–1800).
Scrolling applies the value live through ``clock_idle_set_timeout_ms()``, and the release saves the store.

The power-off glyph to the right of the roller is the idle timer's kill switch (store key ``idle_timeout_enable``, default on).
The roller range only shortens the timeout and never turns it off, so this switch is the only way to keep the clock awake.
A tap flips the flag, saves the store and recolours the glyph:

- white: the idle timer is armed (the default);
- grey: the transition to light sleep is off.

While the switch is off, the roller is locked: it neither scrolls nor commits, and its selected row dims to moon grey.
The ``RELEASED`` handler refuses the commit.
On the knob the dial layer refuses the steps as well, because ``lv_roller_set_selected`` would bypass the LVGL disabled state.

The gate is live.
``clock_idle_check_cb`` reads the flag on every 1 s check, so the idle rung stops firing on the next tick.
A re-enable resumes it without a re-arm.
The tap that flipped the switch already reset the activity clock, so a re-enable cannot send the clock straight to sleep.
Re-entering the screen re-applies the lock (the per-entry refresh).

Screenshots
-----------

.. figure:: ../img/screens/annot/settings-light-sleep_s3.png
   :alt: The idle-timeout screen on the S3 panel, with numbered callouts on the caption, the roller, the power-off toggle and the exit.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/settings-light-sleep_p4.png
   :alt: The idle-timeout screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

.. figure:: ../img/screens/knob/settings_light_sleep.png
   :alt: The idle-timeout screen on the knob's 360 px panel, without callouts.
   :align: center

   Knob, 360×360: the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``.
   On the knob the same ``idle_timeout_sec`` key sets its own LEDC backlight dim, and the power-off toggle skips that dim rung.
   The dial layer refuses steps on the locked roller.

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
     - The 32 px SSID font. The deep-sleep screen's twin reads SLEEP COUNTDOWN.
     - :cfunc:`settings_create_sleep_screen` (``screen = 0``)

   * - 2
     - Idle timeout (1–30 min)
     - The one roller, centred on its row (210 px wide, 96 px numbers font). Applied live to the idle timer. The store keeps seconds; the screen shows minutes. Locked (no scroll or commit, selected row grey) while the kill switch is off.
     - ``event_handler_cb_settings_sleep_roller_released``; options ``idle_timeout_sec`` (the store), ``CLOCK_IDLE_TIMEOUT_SEC`` (the seed); the lock guard refuses a commit while ``idle_timeout_enable`` is off

   * - 3
     - Power-off toggle (the idle-timer kill switch)
     - The 96 px solid ``power-off`` glyph (``GLYPH_POWER_OFF``, U+F011). White means the idle timer is armed; grey means it is off and the roller is locked. A tap flips ``idle_timeout_enable``, saves the store, recolours the glyph and locks or unlocks the roller: the same idiom as the alarm master toggle.
     - ``event_handler_cb_settings_sleep_toggle_clicked``; the live gate is ``clock_idle_check_cb`` (``settings_idle_timeout_enable()`` per check); on the knob the same key skips the dim rung of the power ladder

   * - 4
     - Exit → hub
     - The shared bottom-centre exit, back to the hub.
     - :cfunc:`settings_sub_screen_prepare`

Implementation notes
--------------------

One parameterized builder, :cfunc:`settings_create_sleep_screen`, makes both sleep-settings screens; this one is ``screen = 0`` and :doc:`the sleep countdown <settings-deep-sleep>` is ``screen = 1``.
The roller is centred on its row, and the row clears the exit button's band.
The power-off glyph is 96 px solid, placed ``UI_PX`` 24 px to the right of the roller box.

The store still accepts the legacy keys ``dusk_anim_enable`` and ``boot_anim_enable``, but nothing reads or exposes them; they are no-ops.

Where to go next
----------------

- :doc:`The sleep countdown <settings-deep-sleep>`: the twin screen for the deep-sleep step.
- :doc:`Light sleep <light-sleep>`: the state this timeout leads to.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the idle check and the sleep ladder.
