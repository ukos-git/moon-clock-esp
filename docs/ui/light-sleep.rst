Light sleep
===========

Light sleep keeps the panel on but dims the panel's *own* hardware brightness to ``UI_LIGHT_SLEEP_DIM_PCT`` (default 25 %, over the ``UI_SCREEN_SWITCH_FADE`` ramp) and drops the shared clock to ``UI_CLOCK_ALPHA_PCT`` (default 20 %) LVGL opacity — a plain panel has no true alpha, so the clock's own opacity IS the alpha. The moon holds the dusk's settled pose (the dusk pole spin, ``UI_MOON_DUSK_ENABLE``, :doc:`the sleep machine <../implementation/clock-sleep>`), and the deep-sleep countdown (``deep_sleep_countdown_sec``) runs toward the :doc:`deep-sleep <deep-sleep>` screen. Any tilt departure beyond ``UI_IMU_WAKE_DEG``, a touch, or a pending alarm wakes the device; the tick rate here is ``UI_LIGHT_SLEEP_TICK_MS`` (vs 10 ms active).

.. figure:: ../img/screens/annot/light-sleep_s3.png
   :alt: light sleep — the S3 panel (466, annotated)
   :align: center

   Light sleep — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/light-sleep_p4.png
   :alt: light sleep — the P4 panel (720, annotated)
   :align: center

   Light sleep — P4 720×720 (the same UI_PX layout, scaled)

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
     - Moon at 25 % panel dim
     - The panel's hardware brightness (the CO5300's 0x51 register on the S3, the backlight PWM on the P4) ramped down while the pixel content is held — which is why a lost QSPI write shows a frozen frame, not a dimmed one (:doc:`the GPSPI-stall incident <../verification/device-verify>`).
     - :cfunc:`moon_ui_screen_switched` (the 0x51 ramp under ``s_panel_mtx``)

   * - 2
     - Clock at 20 % opacity
     - The shared widget's LVGL opacity in light sleep; light-sleep only — deep sleep hides the clock entirely.
     - :cfunc:`create_screen_light_sleep` — options ``UI_CLOCK_ALPHA_PCT``, ``UI_LIGHT_SLEEP_DIM_PCT``

   * - 3
     - The wake paths
     - Tilt departure (``UI_IMU_WAKE_DEG``), touch (the idle-wake path), or the alarm. The wake ramps the brightness back to 100 and the clock back to full opacity in the same fade.
     - :cfunc:`action_wake_screen` — options ``UI_IMU_WAKE_DEG``, ``UI_LIGHT_SLEEP_TICK_MS``
