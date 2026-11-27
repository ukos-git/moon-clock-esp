Deep sleep
==========

.. brief:
   Reader: a firmware developer working on the sleep states.
   They already know: C, LVGL basics, light sleep.
   After this page they can: say what the dark screen holds and why the CPU keeps running.
   Page type: reference.

Deep sleep is the dark state at the end of the sleep countdown.
The clock is hidden and the panel's backlight is off, so the screen is a flat black disk.

How it behaves
--------------

The deep-sleep countdown, armed at light-sleep entry, starts this state when it expires.
The screen root has no tap handler.
Waking goes through the wake paths of :doc:`the sleep machine <../implementation/clock-sleep>`.

The product's "deep sleep" is app-level: the CPU keeps running.
Neither clock board can wake from a true ``esp_deep_sleep`` on a timer or alarm.
On the S3 the RTC (real-time clock) alarm line lands on pin 3 of the TCA9554 expander, which is polled every 1.3 s and is not a wake input.
The P4 has no RTC chip at all (:doc:`the S3 board <../devices/s3>`, :doc:`the P4 4C <../devices/p4>`).

Screenshots
-----------

.. figure:: ../img/screens/annot/deep-sleep_s3.png
   :alt: The deep-sleep screen on the S3 panel, with numbered callouts on each element.
   :align: center

   S3, 466×466. The numbered badges match the Elements table.

.. figure:: ../img/screens/annot/deep-sleep_p4.png
   :alt: The deep-sleep screen on the P4 panel, with numbered callouts on each element.
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
     - Moon (the parked static frame)
     - The deep transition parks on the same static frame that light sleep holds. With the backlight off nothing shows, but the app layer keeps the image live for the alarm poll's wake.
     - :cfunc:`create_screen_deep_sleep`; option ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the countdown that armed this transition)

   * - 2
     - Clock (hidden at the full deep dim)
     - The widget is attached but the panel is off. The app-level sleep keeps the CPU running, so the alarm's poll still fires from the dark screen.
     - :cfunc:`create_screen_deep_sleep`; option ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the countdown that armed this transition)

   * - 3
     - Sleep indicator
     - The small glyph below the clock at (185, 320), the alarm screen's companion (same position, ``GLYPH`` label).
     - :cfunc:`create_screen_deep_sleep`

Implementation notes
--------------------

Whenever the shown phase has a dark side, that dark side is a computed, backlit surface, not a texture (:doc:`the dark side <../science/darkside>`).

The knob has no deep-sleep screen.
It runs a true ``esp_deep_sleep_start`` with the panel off and an RTC TIMER wake (see :doc:`the UI index <index>`).

Where to go next
----------------

- :doc:`The sleep-countdown settings <settings-deep-sleep>`: how the user sets or disables the countdown.
- :doc:`The alarm screen <alarm>`: what the alarm poll shows when it fires from here.
- :doc:`The clock and sleep machine <../implementation/clock-sleep>`: the full sleep ladder.
