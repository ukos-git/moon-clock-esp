The Moon Phase Clock
====================

The Moon Phase Clock is a small round desk clock that shows the Moon as
it looks right now. It does not use a photo or an icon. A
microcontroller works out where the Sun, the Earth and the Moon are at
this moment and then draws the Moon pixel by pixel, from NASA maps of
the real lunar surface. You get the right phase, crater shadows along
the day/night line, and the slight wobble that lets us see a little
around the Moon's edges over a month.

.. raw:: html

   <video src="_static/img/promo.mp4" controls muted playsinline
          style="display: block; width: 100%; margin: 1em auto;"
          aria-label="The finished clock in its round case, showing every screen in turn"></video>

The film shows the clock in its round case. Every frame comes from the
project's own simulator, which runs the real firmware's screen code on
a PC. The Moon in it is fixed to one moment (1 November 2026,
12:35 UTC, a waning gibbous) so the pictures in these docs are
repeatable.

What the clock does
-------------------

- **Shows the real Moon.** The phase, the shadows and the Moon's tilt
  are computed for the current time, and for the city of the time
  zone you pick. The picture is refreshed once an hour.
- **Keeps time by itself.** It sets its clock over Wi-Fi, then turns
  the radio off. One tap opens the settings.
- **Wakes you up.** A daily alarm with a choice of weekdays, or a
  repeating interval timer (a "pomodoro"), with four alarm sounds.
- **Sleeps when nobody is looking.** After a minute without touch or
  movement it dims, and later it goes dark. A touch or a tilt wakes it.
- **Matches NASA.** An automated test compares the clock's Moon with
  NASA's own computer-rendered Moon. On average the two differ by
  about 7 brightness steps out of 255 (:doc:`the image gate
  <verification/image-gate>`).

Find your way in
----------------

Pick the path that matches what you want to do. Each one starts simple
and links to the detail when you need it.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - I want to…
     - Start here
   * - understand how it works
     - :doc:`How the clock works <how-it-works>` explains moon phases,
       how a small chip can draw the Moon, and how we check the result.
       School maths and physics are enough.
   * - build one
     - :doc:`Getting started <getting-started>` takes you from a new
       board to a working clock on your desk: install the tools,
       build, flash, connect to Wi-Fi.
   * - use one
     - :doc:`Using the clock <using-the-clock>` walks through the
       screens, the settings, the alarm and the sleep modes.
   * - change the firmware
     - :doc:`The firmware <implementation/index>` explains how the code
       is put together, then :doc:`the reference <reference/index>`
       lists every setting, constant and API.
   * - check the science
     - :doc:`The science <science/index>` derives the geometry and the
       lighting model; :doc:`verification <verification/index>` shows
       the measurements behind every claim.

The boards
----------

The same code runs on three boards from Waveshare. You need only one.

.. list-table::
   :header-rows: 1
   :widths: 26 22 52

   * - Board
     - Screen
     - Good to know
   * - :doc:`ESP32-S3-Touch-AMOLED-1.75 <devices/s3>`
     - 466 × 466 round AMOLED
     - The main target and the default build. Has a real-time clock
       chip, a motion sensor and a speaker output.
   * - :doc:`ESP32-P4-WIFI6-Touch-LCD-XC <devices/p4>` ("4C")
     - 720 × 720 round LCD
     - The largest screen and the fastest chip. No clock chip: it keeps
       time with Wi-Fi and its own crystal.
   * - :doc:`ESP32-S3-Knob-Touch-LCD-1.8 <devices/knob>` ("knob")
     - 360 × 360 round LCD
     - Has a turning dial and runs on a battery. No motion sensor,
       clock chip or speaker.

:doc:`The boards <devices/index>` compares them in detail, and
:doc:`the housing <housing>` describes a 3D-printed case that fits all
three.

About these docs
----------------

The site has two layers. The pages under **Start here** assume no
background beyond school. Everything under **In depth** is written for
people who want to change or verify the clock, and it assumes you know
some C and are happy to read equations.

A few conventions hold throughout:

- Times are in UTC unless a page says otherwise.
- "Gray level" means a brightness from 0 (black) to 255 (white).
- Screenshots come from the simulator, which runs the unchanged
  firmware screen code on a PC (:doc:`the build system
  <reference/build-system>`). Photos of the case are composites of a
  simulator frame and a product photo.
- Every number comes from a named measurement: a benchmark, a serial
  log from a real board, or a size check. :doc:`Performance
  <verification/performance>` gives the commands that produce them.
- Links such as :cfile:`components/moon/core/moon_bake.c` open the
  source code inside this site (:doc:`the source code <code/index>`).

The firmware is licensed under GPL-2.0-or-later (the ``LICENSE`` file
in the repository root). This documentation is licensed under CC BY-SA
4.0 (``docs/LICENSE``). Third-party credits, including the
public-domain NASA imagery and the fonts, are in the ``NOTICE`` file.

.. toctree::
   :hidden:
   :caption: Start here

   how-it-works
   getting-started
   using-the-clock

.. toctree::
   :hidden:
   :caption: Build your own

   devices/index
   housing

.. toctree::
   :hidden:
   :caption: In depth: the screens

   ui/index
   keyboard
   keyboard-lab

.. toctree::
   :hidden:
   :caption: In depth: the science

   science/index

.. toctree::
   :hidden:
   :caption: In depth: the firmware

   implementation/index

.. toctree::
   :hidden:
   :caption: In depth: verification

   verification/index

.. toctree::
   :hidden:
   :caption: Reference

   reference/index
   glossary

.. toctree::
   :hidden:
   :caption: Source code

   code/index

.. The code/ entry must stay the LAST toctree entry: the "Referenced
   from" index on the generated source pages needs every other page's
   resolved references first (see the mooncode extension).
