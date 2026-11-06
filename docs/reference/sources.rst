Sources and references
======================

The primary sources the text cites, the project's in-tree
research, and the tools the measured numbers came from.

Primary sources (the text's citations)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **NASA SVS 5415** — the ground-truth rendered moon (8,760 frames
  across a synodic cycle, 400×400 crops; the gate's reference,
  :doc:`the reference data <../science/reference-images>`).
- **CGI Moon Kit** (F. Roehrich) — the renderer behind SVS 5415
  (:doc:`the reference data <../science/reference-images>`).
- **LROC albedo / LOLA height** (NASA GSFC / USGS) — the 16-bit
  texture + the 2048×1024 DEM (:doc:`the reference data <../science/reference-images>` and :doc:`the geometry <../science/geometry>`).
- **ERFA** (the International Astronomical Union's ephemeris
  routines; the vendored eraEpv00 + eraMoon98) (:doc:`the geometry <../science/geometry>`).
- **IAU 2015 lunar rotation** — the *default* body frame the
  project's calibrated pole replaces (:doc:`the geometry <../science/geometry>`).
- **ESP-IDF 5.5.1** (Espressif) — the RTOS + the SPI/QSPI/DSI/DMA
  drivers (:doc:`the architecture <../implementation/architecture>`, :doc:`the display pipeline <../implementation/display>` and :doc:`the P4 port <../implementation/p4-port>`).
- **LVGL 9.6** — the UI framework (the flush path, the arcs, the
  ``ADV_HITTEST`` contract) (:doc:`the architecture <../implementation/architecture>` and :doc:`the round keyboard <../keyboard>`).
- **Waveshare ESP32-S3-Touch-AMOLED-1.75** (CO5300 QSPI, CST9217,
  QMI8658C, PCF85063A, TCA9554) — the S3 board (:doc:`the S3 board <../devices/s3>`).
- **Waveshare ESP32-P4-WIFI6-Touch-LCD-XC "4C"** (JD9365 MIPI-DSI,
  GT911, remote C6 over SDIO) — the P4 board (:doc:`the P4 4C port <../devices/p4>`).

The project's in-tree research
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- ``docs/research/moon-rendering.md`` — the moon-rendering research
  (the model + the validation approach, :doc:`the geometry <../science/geometry>`).
- ``docs/research/circular-keyboard-alternatives.md`` — the
  round-keyboard deep research (the 13-method survey + the ranked
  verdict, :doc:`the round keyboard <../keyboard>`).
- ``docs/research/gltf-feasibility.md`` — the (rejected) 3D/gltf
  feasibility note (the 2D bake's justification, :doc:`the lighting model <../science/lighting>`).
- ``docs/_static/keyboard/round-keyboard-lab.html`` — the
  interactive design lab (the operable 13 panels, embedded in
  :doc:`the round keyboard lab <../keyboard-lab>`) and
  ``docs/_static/keyboard/sweep-wheel-v2.html`` — the standalone
  operable widget of the shipped wheel (embedded in
  :doc:`the round keyboard <../keyboard>`).

The HCI literature (the keyboard's citations, :doc:`the round keyboard <../keyboard>`)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- C-QWERTY (2020) — ring-of-keys on a circular smartwatch, QWERTY
  order.
- COMPASS (CHI 2017) — multi-cursor circular keyboard, bezel
  rotation.
- WrisText (CHI 2018) — wrist whirl to a circular alphabetical
  layout.
- Crownboard (CHI 2023) — crown-rotation scanning; 2.3 % / 0.53 %
  error.
- RingByte (UIST 2025) — rotating smart-ring entry + neural decode.
- Tap'nSwipe (IJHCI 2025) — 9-area × 4-char swipe beats QWERTY
  accuracy on a small watch.
- Ng/Brewster/Beruscha/Krautter (CHI 2017) — the on-road dial vs
  touch (>94 % accuracy all methods; the certainty story).
- The full source list (37 entries, with URLs) is at the end of
  ``docs/research/circular-keyboard-alternatives.md``.

The tools the numbers came from
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- ``host_sim/check_gate.py`` — the image gate (the 32-frame table,
  :doc:`the image gate <../verification/image-gate>`).
- ``host_sim/bench.c`` (``moon_bench``) — the performance bench
  (the C numbers, :doc:`performance <../verification/performance>`).
- ``host_sim/uisim`` — the UI simulator (the screenshots, :doc:`the interactive simulator <../science/interactive>` and :doc:`the round keyboard <../keyboard>`).
- ``docs/gen_media.py`` — the docs video builder (the four
  animation films, :doc:`the build system <build-system>`).
- The serial console (115200) + the ROM reset reason — the device
  verification surface (:doc:`on-device verification <../verification/device-verify>`).

A note on the site's own numbers
--------------------------------

Every measured number in these docs is a *reproducible* one: the host
numbers are the bench/gate commands in :doc:`the image gate <../verification/image-gate>`, :doc:`the image gate <../verification/image-gate>`, :doc:`on-device verification <../verification/device-verify>` and :doc:`performance <../verification/performance>`, and the device
numbers are quoted from the *accepted captures* (:doc:`on-device verification <../verification/device-verify>`'s
standard: progress + timing + zero failure lines). A number without
its capture is a hypothesis, not a verification — the site's
numbers all carry their captures, and the acceptance triple (it ran
to its endpoint, at its budget, with zero failure lines) is the
standard a re-measurement must meet to replace one.

That is the documentation: the product (the board pages), the science,
the implementation, the validation, and the reference — a physically
correct moon on a round panel, measured end to end, with the decisions
— including the ones that
beat NASA on purpose — recorded where they were made.
