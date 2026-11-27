Sources and references
======================

.. brief:
   Reader: anyone who wants to check where a fact or number on this site comes from.
   They already know: the site's main sections.
   After this page they can: find the primary source, in-tree research note or tool behind a claim.
   Page type: reference.

This page lists the primary sources the site cites, the project's in-tree research notes, and the tools that produced the measured numbers.
Each entry links to the page that uses it.

Primary sources
---------------

- NASA SVS 5415: the ground-truth rendered moon (8,760 frames across a synodic cycle, 400×400 crops), the image gate's reference (:doc:`the reference data <../science/reference-images>`).
- CGI Moon Kit (F. Roehrich): the renderer behind SVS 5415 (:doc:`the reference data <../science/reference-images>`).
- LROC albedo and LOLA height (NASA GSFC / USGS): the 16-bit texture and the 2048×1024 elevation model (:doc:`the reference data <../science/reference-images>` and :doc:`the geometry <../science/geometry>`).
- ERFA (the International Astronomical Union's ephemeris routines): the vendored eraEpv00 and eraMoon98 (:doc:`the geometry <../science/geometry>`).
- IAU 2015 lunar rotation: the default body frame that the project's calibrated pole replaces (:doc:`the geometry <../science/geometry>`).
- ESP-IDF 5.5.1 (Espressif): the RTOS and the SPI/QSPI/DSI/DMA drivers (:doc:`the architecture <../implementation/architecture>`, :doc:`the display pipeline <../implementation/display>` and :doc:`the P4 port <../implementation/p4-port>`).
- LVGL 9.6: the UI framework, including the flush path, the arcs and the ``ADV_HITTEST`` contract (:doc:`the architecture <../implementation/architecture>` and :doc:`the round keyboard <../keyboard>`).
- Waveshare ESP32-S3-Touch-AMOLED-1.75 (CO5300 QSPI, CST9217, QMI8658C, PCF85063A, TCA9554): the S3 board (:doc:`the S3 board <../devices/s3>`).
- Waveshare ESP32-P4-WIFI6-Touch-LCD-XC "4C" (JD9365 MIPI-DSI, GT911, remote C6 over SDIO): the P4 board (:doc:`the P4 4C port <../devices/p4>`).

The project's in-tree research
------------------------------

- ``docs/research/moon-rendering.md``: the moon-rendering research, covering the model and the validation approach (:doc:`the geometry <../science/geometry>`).
- ``docs/research/circular-keyboard-alternatives.md``: the round-keyboard research, with a survey of 13 methods and a ranked verdict (:doc:`the round keyboard <../keyboard>`).
- ``docs/research/gltf-feasibility.md``: the rejected 3D/glTF feasibility note, which justifies the 2D bake (:doc:`the lighting model <../science/lighting>`).
- ``docs/_static/keyboard/round-keyboard-lab.html``: the interactive design lab with 13 operable panels, embedded in :doc:`the round keyboard lab <../keyboard-lab>`.
- ``docs/_static/keyboard/sweep-wheel-v2.html``: the standalone operable widget of the shipped wheel, embedded in :doc:`the round keyboard <../keyboard>`.

The HCI literature behind the round keyboard
--------------------------------------------

:doc:`The round keyboard <../keyboard>` cites these human-computer interaction (HCI) papers:

- C-QWERTY (2020): a ring of keys on a circular smartwatch, in QWERTY order.
- COMPASS (CHI 2017): a multi-cursor circular keyboard driven by bezel rotation.
- WrisText (CHI 2018): a wrist whirl over a circular alphabetical layout.
- Crownboard (CHI 2023): crown-rotation scanning; 2.3 % / 0.53 % error.
- RingByte (UIST 2025): text entry with a rotating smart ring and a neural decoder.
- Tap'nSwipe (IJHCI 2025): 9 areas × 4 characters, swiped; beats QWERTY accuracy on a small watch.
- Ng/Brewster/Beruscha/Krautter (CHI 2017): an on-road study of dial against touch (>94 % accuracy for all methods; the certainty argument).

The full source list (37 entries, with URLs) is at the end of ``docs/research/circular-keyboard-alternatives.md``.
:ref:`The thirteen in one picture <thirteen-sketch-lab>` draws which method each paper supports and which hardware constraint ruled each one out.
:ref:`The certainty ladder <certainty-ladder-perf>` converts their speed and error figures to characters per minute, recomputed at figure time.

The tools the numbers came from
-------------------------------

- ``host_sim/check_gate.py``: the image gate, which produces the 32-frame table (:doc:`the image gate <../verification/image-gate>`).
- ``host_sim/bench.c`` (``moon_bench``): the performance bench, the source of the C timings (:doc:`performance <../verification/performance>`).
- ``host_sim/uisim``: the UI simulator, the source of the screenshots (:doc:`the interactive simulator <../science/interactive>` and :doc:`the round keyboard <../keyboard>`).
- ``docs/gen_media.py``: the docs video builder. It makes the fade-to-black switch film and the ``--tour`` promo film, with every promo frame composited into the round product case (:doc:`the build system <build-system>`).
- The serial console (115200 baud) and the ROM reset reason: the device verification surface (:doc:`on-device verification <../verification/device-verify>`).
- ``bench/record_demo.py``: the bench recording harness.
  It records a C920 camera and one passive serial log per board, then cuts them into per-phase comparison frames at the demo tour's ``phase=`` markers (:doc:`demo mode <../verification/demo-mode>`).
- ``bench/knob_repro.sh``: a passive serial watchdog for the knob's "redraw loop".
  It goes red on any ``Rebooting…``, stack-overflow or ``Guru Meditation`` line inside its window, and green on a live touch heartbeat with zero reboots.
  It is the failing-capable check behind the knob_boot 4 KiB stack-overflow fix (:commit:`680ac89c`).

How the site's numbers are measured
-----------------------------------

Every measured number on this site can be reproduced.
The host numbers come from the bench and gate commands in :doc:`the image gate <../verification/image-gate>`, :doc:`on-device verification <../verification/device-verify>` and :doc:`performance <../verification/performance>`.
The device numbers come from accepted captures, to the standard of :doc:`on-device verification <../verification/device-verify>`: progress, timing and zero failure lines.
A number without its capture is a hypothesis, not a verification.
A re-measurement replaces a number only when it meets the same acceptance triple: it ran to its endpoint, within its budget, with zero failure lines.

Where to go next
----------------

- :doc:`the glossary <../glossary>`: the terms these sources use.
- :doc:`the reference data <../science/reference-images>`: how the NASA sources feed the image gate.
