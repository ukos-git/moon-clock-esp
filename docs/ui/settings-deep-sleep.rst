The sleep countdown (deep sleep)
================================

The twin of the :doc:`idle-timeout screen <settings-light-sleep>` (the same parameterized builder, ``screen = 1``): the **deep-sleep countdown** roller, in minutes, alone and centred on its row. The countdown takes effect at the **next light-sleep entry** — the RTC one-shot is armed right there (the dusk spin that used to own the arming was retired 2026-11-13; sleep depth follows the timer, :doc:`the sleep machine <../implementation/clock-sleep>`) and triggers the deep-sleep transition on expiry. The deep transition itself ends on the flat-black disk with the backlight off; the store's ``dusk_anim_enable`` flag (with its toggle) left with the dusk, leaving no animation to expose on either sleep screen.

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

   The sleep countdown (deep sleep) — knob 360×360 (the shared ``screens.c`` compiled under ``CONFIG_BOARD_KNOB``; on the knob this same ``deep_sleep_countdown_sec`` key arms its RTC TIMER deep sleep)

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
     - Minutes on the screen, seconds in the store. Takes effect at the next light-sleep entry (the entry itself arms the one-shot, :doc:`the sleep machine <../implementation/clock-sleep>`).
     - ``event_handler_cb_settings_sleep_roller_released`` — options ``deep_sleep_countdown_sec`` (the store), ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the seed)

   * - 3
     - Exit → hub
     - The shared bottom-centre exit back to the hub.
     - :cfunc:`settings_sub_screen_prepare`
