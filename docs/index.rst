The Moon Phase Clock
====================

The Moon Phase Clock is a small desk clock that renders the Moon
*physically*: the same crater shadows, the same libration, the same
terminator position you would get from a NASA ephemeris renderer —
computed live on a microcontroller, at the exact phase for the current
UTC time, on a 466×466 round AMOLED panel.

It is not a photo. It is not a clock face with a moon icon. It is a
3-D-shaded rendering of the real lunar surface (LRO WAC albedo + LOLA
relief), lit by the real Sun from the real subsolar point of the
current moment, projected through the real camera libration. The
pipeline is small enough to run in about 1.4 MB of flash and to bake a
full frame in well under a second on the S3 board (the Waveshare
ESP32-S3-Touch-AMOLED-1.75, :doc:`the S3 board <devices/s3>`) — which is what made the
animation stack (the free spin, the boot sweep, the "dusk") possible
on a $20 board; the user retired all three on 2026-11-13, and the
home is a static LVGL phase now
(:doc:`the animation pipelines <implementation/animations>`).

.. figure:: _static/photos/sim_home.png
   :alt: Simulated render of the finished clock on the Waveshare ESP32-S3-Touch-AMOLED-1.75
   :figclass: align-center

   The finished clock, simulated: the home screen composited onto the
   Waveshare ESP32-S3-Touch-AMOLED-1.75's glass (a uisim frame at
   true panel geometry over the Waveshare product photo) — the moon
   at the sim's fixed seed (2026-11-01 12:35 UTC, a waning gibbous),
   the minute, the seconds arc. The renders throughout these docs
   show what the *panel* actually shows.

This documentation covers a real, working product end to end: the C
core that bakes a moon frame on a microcontroller, the display
pipeline that ships each frame as one atomic whole-panel flush, the
retired animation stack (the free spin, the boot sweep and the
"dusk" — all retired 2026-11-13, leaving the fade and the orbit), the
sleep/wake machine, the settings stack, and the validation gates that
prove the rendering matches NASA's rendered moon to within a couple of
gray levels per pixel.

What makes it different
-----------------------

Three properties define the design, and they pull in the same
direction:

1. **Physically correct, not approximately pretty.**
   The lighting model is full-disk displaced-normal Lambert with the
   real relief DEM's 4-neighbor gradients — not the classic
   "cosine-from-viewer" shortcut that every cheap moon renderer uses.
   The result: mountain shadows along the terminator that match
   NASA's own rendered frames, and a dark side that is the *computed*
   backlit surface (albedo and relief lit from the anti-solar
   direction), not a black rectangle. :doc:`The lighting model <science/lighting>` is the model;
   :doc:`the image gate <verification/image-gate>` is the measurement against NASA (mean absolute error
   6.68 gray levels at 5-bit panel fidelity, worst frame 8.90, gate
   limit 12).

2. **Atomic frames, always.**
   Every moon frame reaches the panel as ONE whole-panel flush
   (:doc:`the display pipeline <implementation/display>`). The AMOLED has no alpha channel and no refresh
   arbitration the app can rely on, so a torn frame would be a
   half-moon — visibly wrong. The entire UI architecture (including
   the animation system and the sleep transitions) is built around
   this one invariant.

3. **The display is also the sleep surface.**
   The device is a clock that mostly sleeps. Light sleep is a
   **static dim frame** (panel to 25 %, clock to 20 % alpha — the
   dusk's slow constant-projection spin that used to play the
   transition was retired 2026-11-13, and the dim itself is now the
   sleep indicator) and deep
   sleep hides the clock and turns the backlight off. The hardware
   dim and the fade carry the sleep-state story for
   an always-on display (:doc:`the animation pipelines <implementation/animations>`) and the entry/exit flow itself
   (:doc:`the clock and sleep machine <implementation/clock-sleep>`).

The two boards
--------------

The same portable C core runs on two hardware targets — the
**Waveshare ESP32-S3-Touch-AMOLED-1.75** (the primary, "S3" below) and
the **Waveshare ESP32-P4-WIFI6-Touch-LCD-XC** (the secondary, "4C"
below):

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Property
     - S3 (primary)
     - P4 4C (secondary)
   * - SoC
     - ESP32-S3 (dual Xtensa LX7, 240 MHz)
     - ESP32-P4 (quad RISC-V, 400 MHz) + remote C6 Wi-Fi over SDIO
   * - Panel
     - 466×466 RGB565 AMOLED, CO5300, QSPI
     - 720×720 round IPS, JD9365, MIPI-DSI
   * - Touch
     - CST9217
     - GT911
   * - Extras
     - QMI8658C IMU, PCF85063A RTC
     - IMU, RTC (same family)
   * - Flash / PSRAM
     - 16 MB flash (2 MB app partition)
     - 32 MB flash (9 MB app partition)

:doc:`the S3 board <devices/s3>` covers the S3 in detail; :doc:`the P4 4C port <devices/p4>` covers the P4 port and
what it takes (and what it did not) to port a pixel-perfect display
pipeline across panel technologies.

How these docs are organized
----------------------------

.. list-table::
   :header-rows: 1
   :widths: 14 70

   * - Section
     - Question it answers
   * - Getting started
     - The minimal, verified way to flash a newly acquired board on
       Linux.
   * - The boards
     - What hardware it runs on, and how to build and flash each
       board (toolchain, identification, pre-configured defaults,
       compile-time settings).
   * - The user interface
     - Every screen, element by element: what each pointing
       control does, where it lives in the code, and which option
       tunes it.
   * - The science
     - Where the reference images come from and the
       geometric/optical model behind the bake (including why the
       rendering is deliberately better than NASA's).
   * - The implementation
     - How the C core works, how frames reach the panel, and how
       the animations and the sleep machine run.
   * - Verification and performance
     - How we know it's right, and how fast everything is.
   * - Reference
     - How text is entered on a round screen, the build system,
       the compile-time reference, and the APIs.

A note on numbers
-----------------

Every performance number in these docs comes from a named
measurement: a benchmark run (:doc:`performance <verification/performance>` gives the exact commands),
a device serial capture, or a code-size check. Where a number was
superseded by a later change, the older value is left in place with
its date — the binary-size chronology in :doc:`the binary-size chronology <verification/binary>` is the canonical
record of "what was on the device when".

Contents
--------

.. toctree::
   :maxdepth: 2
   :caption: Start here

   getting-started

.. toctree::
   :maxdepth: 2
   :caption: The boards

   devices/index

.. toctree::
   :maxdepth: 2
   :caption: The user interface

   ui/index
   keyboard
   keyboard-lab

.. toctree::
   :maxdepth: 2
   :caption: The science

   science/index

.. toctree::
   :maxdepth: 2
   :caption: The implementation

   implementation/index

.. toctree::
   :maxdepth: 2
   :caption: Verification and performance

   verification/index

.. toctree::
   :maxdepth: 2
   :caption: Reference

   reference/index
   glossary

.. toctree::
   :maxdepth: 1
   :caption: The source code

   code/index

.. note:: The ``code/`` entry is deliberately LAST: the reverse
   "Referenced from" index on the generated source pages needs every
   other page's resolved references first (see the ``mooncode``
   extension).

Conventions
-----------

- Times are UTC unless marked otherwise. "Frame" means one 466×466
  (S3) or 720×720 (P4) whole-panel image.
- "Gray" means 8-bit luminance 0–255; "5-bit" means the panel's R5G6B5
  fidelity after the gray→RGB565 LUT.
- Code links (``:cfile:``, ``:cfunc:``, ``:clabel:``) resolve to the
  locally generated source pages under :doc:`code/index`; ``:clabel:``
  line numbers are exact per-line anchors. Line numbers in prose are
  approximate — use the roles or search for the symbol.
- Screenshots are the uisim output (the real unmodified
  ``screens.c`` + LVGL, rendered in software) for both panels;
  device composites place the uisim frame at true panel geometry over
  the product photo. Data images are measured or fitted pixel data —
  diagrams a native mechanism can express are native
  (``.. graphviz::`` / mermaid) instead.
