The firmware
============

.. brief:
   Reader: a developer who wants to change the firmware.
   They already know: C, FreeRTOS tasks and the LVGL basics.
   After this page they can: find the right page for the part they will change.
   Page type: reference (section index).

This section explains how the firmware is built: which code runs
where, how a computed Moon reaches the screen, and how the clock
decides when to sleep. It is written for people who want to change the
code.

Start with :doc:`architecture`. It gives the map of tasks, buffers and
rules that the other pages refer to. After that, read the page for the
part you want to change.

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Page
     - What it covers
   * - :doc:`architecture`
     - The tasks, the two CPU cores, the buffers, and the rules that
       keep frames from tearing.
   * - :doc:`core-c`
     - The portable C library that turns a time into a 400 × 400 Moon
       image. The same code runs on the boards and on a PC.
   * - :doc:`rotate-lut`
     - Rotating the Moon in the screen plane, and converting gray
       levels to the panel's colour format.
   * - :doc:`display`
     - How a finished frame gets to the panel in one piece.
   * - :doc:`animations`
     - What still moves: the re-bake when the clock is tilted, the
       hourly refresh and the screen fades, plus the animations that
       were removed.
   * - :doc:`compute-progress`
     - What the screen shows while a new Moon is being computed.
   * - :doc:`moon-movie`
     - The precomputed month of Moon phases stored in flash for the
       boot sequence.
   * - :doc:`observer-view`
     - Tilting the Moon to match the view from the chosen time zone's
       city.
   * - :doc:`clock-sleep`
     - The minute tick, the alarm, and the sleep and wake state machine.
   * - :doc:`p4-port`
     - What changed to run the same product on the 720 × 720 4C board.
   * - :doc:`knob-display`
     - How the knob board shares the screens and adds its dial, battery
       gauge and real deep sleep.

.. toctree::
   :hidden:

   architecture
   core-c
   rotate-lut
   display
   animations
   compute-progress
   moon-movie
   observer-view
   clock-sleep
   p4-port
   knob-display
