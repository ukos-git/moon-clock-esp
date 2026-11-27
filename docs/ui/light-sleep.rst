Light sleep
===========

.. brief:
   Reader: a firmware developer working on the sleep states.
   They already know: C, LVGL basics, the airplane home screen.
   After this page they can: say what the dimmed screen shows, how it is dimmed and what wakes it.
   Page type: reference.

Light sleep is the dimmed state the clock enters after the idle timeout.
The panel stays on, at lower brightness, with the moon and a faint clock.

How it behaves
--------------

Entering light sleep dims the panel's own hardware brightness to ``UI_LIGHT_SLEEP_DIM_PCT`` (default 25 %) over the ``UI_SCREEN_SWITCH_FADE`` ramp.
The shared clock drops to ``UI_CLOCK_ALPHA_PCT`` (default 20 %) LVGL opacity.
A plain panel has no true alpha, so the clock's own opacity is the alpha.

The moon holds its last baked frame static: ``moon_ui_pause(true)`` interrupts the orbit and pauses the tick.
The dim itself is the transition (:doc:`the sleep machine <../implementation/clock-sleep>`).

The entry arms the deep-sleep countdown (``deep_sleep_countdown_sec``), which runs toward the :doc:`deep-sleep <deep-sleep>` screen.
If the countdown is disabled with the power-off toggle on :doc:`the deep-sleep settings screen <settings-deep-sleep>`, the entry never arms the one-shot.
This dim frame then stays until something wakes the clock.

Three things wake the clock: a tilt beyond ``UI_IMU_WAKE_DEG``, a touch, or a pending alarm.
The tick period here is ``UI_LIGHT_SLEEP_TICK_MS``, against 10 ms while awake.

Screenshots
-----------

.. figure:: ../img/screens/annot/light-sleep_s3.png
   :alt: The light-sleep screen on the S3 panel, with numbered callouts on each element.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/light-sleep_p4.png
   :alt: The light-sleep screen on the P4 panel, with numbered callouts on each element.
   :align: center

   P4, 720×720. The same ``UI_PX`` layout, scaled.

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
     - The panel's hardware brightness (the CO5300's 0x51 register on the S3, the backlight PWM on the P4) ramps down while the pixel content is held. That is why a lost QSPI write shows a frozen frame, not a dimmed one (:doc:`the GPSPI-stall incident <../verification/device-verify>`).
     - :cfunc:`moon_ui_screen_switched` (the 0x51 ramp under ``s_panel_mtx``)

   * - 2
     - Clock at 20 % opacity
     - The shared widget's LVGL opacity in light sleep. Light sleep only: deep sleep hides the clock entirely.
     - :cfunc:`create_screen_light_sleep`; options ``UI_CLOCK_ALPHA_PCT``, ``UI_LIGHT_SLEEP_DIM_PCT``

   * - 3
     - The wake paths
     - Tilt (``UI_IMU_WAKE_DEG``), touch (the idle-wake path) or the alarm. The wake ramps the brightness back to 100 and the clock back to full opacity in the same fade.
     - :cfunc:`action_wake_screen`; options ``UI_IMU_WAKE_DEG``, ``UI_LIGHT_SLEEP_TICK_MS``

Implementation notes
--------------------

The knob has no light-sleep screen.
It dims its real LCD backlight instead (see :doc:`the UI index <index>`).

Where to go next
----------------

- :doc:`Deep sleep <deep-sleep>`: the state the countdown leads to.
- :doc:`The idle-timeout settings <settings-light-sleep>`: how the user sets when this state starts.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the full sleep ladder.
