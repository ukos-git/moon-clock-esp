Chapter 1 — What this is
========================

One sentence
------------

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
ESP32-S3-Touch-AMOLED-1.75, Chapter 2) — which is what makes the
animations in Chapter 14 (a two-minute boot sweep, the continuous
burn-in spin, the "dusk") possible on a $20 board.

.. figure:: ../_static/photos/sim_home.png
   :alt: Simulated render of the finished clock on the Waveshare ESP32-S3-Touch-AMOLED-1.75
   :figclass: align-center

   The finished clock, simulated: the home screen composited onto the
   Waveshare ESP32-S3-Touch-AMOLED-1.75's glass (a uisim frame at
   true panel geometry over the Waveshare product photo) — the moon
   at the sim's fixed seed (2026-11-01 12:35 UTC, a waning gibbous),
   the minute, the seconds arc. The bench photographs in the hardware
   chapters show the same product physical; the renders show what the
   *panel* actually shows.

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
   direction), not a black rectangle. Chapter 6 is the model;
   Chapter 17 is the measurement against NASA (mean absolute error
   6.68 gray levels at 5-bit panel fidelity, worst frame 8.90, gate
   limit 12).

2. **Atomic frames, always.**
   Every moon frame reaches the panel as ONE whole-panel flush
   (Chapter 13). The AMOLED has no alpha channel and no refresh
   arbitration the app can rely on, so a torn frame would be a
   half-moon — visibly wrong. The entire UI architecture (including
   the animation system and the sleep transitions) is built around
   this one invariant.

3. **The display is also the sleep surface.**
   The device is a clock that mostly sleeps. Light sleep shows the
   moon in a slow constant-projection spin (the "dusk") and deep
   sleep hides the clock and turns the backlight off. The animations
   are not decoration — they are the burn-in mitigation strategy for
   an always-on display (Chapter 14) and the sleep-state indicators
   (Chapter 15).

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

Chapter 2 covers the S3 in detail; Chapter 3 covers the P4 port and
what it takes (and what it did not) to port a pixel-perfect display
pipeline across panel technologies.

How the book is organized
-------------------------

.. figure:: ../_static/photos/bench_crescent.jpg
   :alt: The panel at the bench, current phase

   The current phase on the panel — a thin lit crescent, the computed
   backlit dark side everywhere else. This is what "physically
   correct" looks like at the new-moon end of the synodic month.

.. list-table::
   :header-rows: 1
   :widths: 10 14 76

   * - Part
     - Chapters
     - Question it answers
   * - I
     - 1–4
     - What is it, what does it run on, and where do the reference
       images come from?
   * - II
     - 5–9
     - What is the moon math, and why is the rendering better than
       NASA's by design?
   * - III
     - 10–16
     - How does the C core work, how do frames reach the panel, how do
       the animations and the sleep machine run?
   * - IV
     - 17–20
     - How do we know it's right, and how fast is everything?
   * - V
     - 21–25
     - How is text entered on a round screen, and what are the APIs?

A note on numbers
-----------------

Every performance number in this book comes from a named
measurement: a benchmark run (Chapter 19 gives the exact commands),
a device serial capture, or a code-size check. Where a number was
superseded by a later change, the older value is left in place with
its date — the binary-size chronology in Chapter 20 is the canonical
record of "what was on the device when".
