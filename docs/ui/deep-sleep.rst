Deep sleep
==========

Deep sleep hides the clock and turns the panel's backlight off — the screen is the flat black disk. The product's "deep sleep" is app-level (the CPU keeps running): neither board can wake from a true ``esp_deep_sleep`` on a timer or alarm — the S3's RTC alarm line lands on the TCA9554 expander's pin 3 (polled at 1.3 s), not a wake input, and the P4 carries no RTC IC at all (:doc:`the S3 board <../devices/s3>`, :doc:`the P4 4C <../devices/p4>`). The dark side of the moon — whenever the shown phase has one — is a *computed* backlit surface, not a texture (:doc:`the dark side <../science/darkside>`).

.. figure:: ../img/screens/annot/deep-sleep_s3.png
   :alt: deep sleep — the S3 panel (466, annotated)
   :align: center

   Deep sleep — S3 466×466 (the badges are the legend numbers)

.. figure:: ../img/screens/annot/deep-sleep_p4.png
   :alt: deep sleep — the P4 panel (720, annotated)
   :align: center

   Deep sleep — P4 720×720 (the same UI_PX layout, scaled)

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
     - Moon — the parked static frame
     - The deep transition parks on the same static frame the light-sleep screen holds (the dusk's dark-side endpoint bake was retired 2026-11-13); under the blacklight nothing shows, but the app layer keeps the image live for the alarm poll's wake.
     - :cfunc:`create_screen_deep_sleep` — option ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the countdown that armed this transition)

   * - 2
     - Clock (hidden at the full deep dim)
     - The widget is attached but the panel is off; the app-level sleep keeps the CPU running so the alarm's poll still fires from the dark screen.
     - :cfunc:`create_screen_deep_sleep` — option ``UI_DEEP_SLEEP_COUNTDOWN_SEC`` (the countdown that armed this transition)

   * - 3
     - Sleep indicator
     - The small glyph below the clock at (185, 320) — the alarm-screen's companion (same position, ``GLYPH`` label).
     - :cfunc:`create_screen_deep_sleep`
