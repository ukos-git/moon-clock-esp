Chapter 25 — Glossary and references
====================================

The last chapter is the book's index of *terms* and *sources*: the
glossary the running text resolves to (the lunar astronomy, the
rendering model, the device pipeline, and the project's own
jargon), and the references — the primary sources the chapters cite,
the project's in-tree research, and the tools the numbers came from.

Glossary
--------

Lunar astronomy
^^^^^^^^^^^^^^^

- **Body frame** — the Moon's rotating reference frame (pole = +z),
  the IAU_Moon rotation: the calibrated pole
  (RA 269.839977° / Dec 68.110423°) + the prime-meridian rate
  (13.17617251°/day). The frame a surface direction lives in before
  the camera sees it (Chapter 5).
- **Subsolar point** — the surface point where the Sun is zenith
  (``subsolar_lon/lat``); its distance from the subearth point sets
  the terminator.
- **Subearth point** — the surface point under the observer
  (``subearth_lon/lat``); the visible pole of the disk.
- **Position angle (posangle)** — the angle of celestial north east
  of the observer's zenith, in degrees; the camera basis' in-plane
  rotation (Chapters 5, 7).
- **Phase angle** — the Sun–Moon–observer angle ``i``; the
  illumination is ``(1 + cos i) / 2`` (the ``illum``).
- **Libration** — the Moon's apparent wobble (geometric, not
  physical): the posangle sweep + the subearth point's excursion,
  which lets ~59 % of the surface be seen over a cycle. The cheap
  ephemeris carries it (the subsolar/subearth residual, Chapter 5).
- **Synodic month** — 29.530589 days; the phase cycle the boot sweep
  plays (Chapter 14).
- **New-moon epoch** — the instant of zero phase; the sweep's start
  (the two-pass 6 h coarse + 20 min fine scan, Chapter 14).

The rendering model
^^^^^^^^^^^^^^^^^^^

- **Base-sphere direction** — the per-pixel unit vector
  ``r = u·right + v·up + w·E`` (the bake's first step; the
  257-entry atan/row-frac table pass, Chapter 11).
- **Displaced-normal Lambert** — the per-pixel lighting model:
  ``L = clip(lns, 0, 1)`` where ``lns`` is the relief-normal sun dot
  — the mountain shadows at the terminator (Chapters 5, 6).
- **Terminator** — the day/night boundary (``d = r·S = 0``); the
  displaced model's edge (the band-only A/B, Chapter 6).
- **Earthshine** — the phase-independent dark-side gradient
  (``R = A·x + B·x² + C·x³`` in the dark disk fraction ``x``); the
  ``dark_zero = 0`` dark side (Chapter 7).
- **Backlit night side** — the *computed* dark side (``dark_zero = 1``):
  the albedo + relief lit from the anti-solar direction (``−S``) at a
  faint level (peak gray ~51) — the "the unilluminated side is fully
  calculated" decision (Chapter 7).
- **dk_sup** — the dusk's dark-side bound: the dark side is written
  for ``d < dk_sup`` instead of ``d < 0``, killing the geometrically
  lit rim sliver (the "white ray") with the ``MOON_DK_MARGIN`` NN
  erosion (Chapter 7).
- **Shade map** — the pre-shaded body-frame equirect map the dusk
  re-projects (the constant-projection pair, Chapters 7, 14).
- **Constant projection** — the property that makes the dusk cheap:
  the bake's color depends only on the body direction + the epoch
  sun; the camera basis selects the view but never enters the color
  (Chapter 7).
- **Tone LUT** — the 256-entry calibrated gray→gray curve (the fit
  to NASA's lit-region balance, Chapter 6); ``MOON_TONE_LUT``.
- **Tint (warm LUT)** — the display's gray→RGB565 warm cast
  (R ×1.038 / B ×0.933); the art pass's one display knob
  (``moon_gray565``, Chapters 8, 12).
- **Gate (image gate)** — the host validation: the C core's frames
  image-diffed against NASA SVS 5415 (≤ 12 MAE/frame) and the Python
  prototype (≤ 2 MAE/frame), the 32-frame set (Chapter 17).

The device pipeline
^^^^^^^^^^^^^^^^^^^

- **Atomic frame** — the invariant: every moon-pipeline frame hits
  the panel as ONE whole-panel ``lv_refr_now`` under ``s_rgb_mtx``;
  the two sanctioned exceptions (the fade-to-black, the spin's direct
  write) (Chapter 10).
- **s_rgb_mtx / s_panel_mtx** — the two locks: the *image* lock
  (bake/rotate/display of the moon buffer) and the *panel-io* lock
  (LVGL flushes, the spin's strip writes, the 0x51 brightness)
  (Chapter 10).
- **Spin** — the burn-in rotation: a once-baked frame re-projected by
  the Q15.15 gather at 8 fps, written *directly* to the panel under
  ``s_panel_mtx`` (LVGL bypassed for that frame), time-based theta
  with active (300 s/turn) / settled (600 s/turn) rates (Chapters 12,
  14).
- **Sweep** — the boot animation: the last synodic month as 336
  frames (240 px intermediate + a full-res final), boot-only
  (wakes are static) (Chapter 14).
- **Dusk** — the light-sleep entry: the constant-projection pole
  spin to the dark side (the shade map + the re-projects), the
  backlight dim to 25 %, the RTC countdown armed on the last frame
  (Chapters 14, 15).
- **phi\*** — the dusk's endpoint latitude (``atan2(−B, −A)``), the
  pole angle that puts the visible disk fully in the dark side
  (Chapter 14).
- **DPI FIFO underrun** — the P4 scanout failure: the DPI controller
  cannot fetch the strip from XIP PSRAM fast enough (the 103/65-row
  strip fix, Chapter 16).
- **Strip** — the flush/write unit: the S3's 25-row (23,300 B)
  buffer, the P4's 103/65-row buffer (the DMA cap + the FIFO budget)
  (Chapters 13, 16).
- **Panel-io watchdog** — the ``panel_wdg`` task: a stuck panel-io
  (the lost-DMA-completion freeze, Chapter 13) restarts the board
  after ``PANEL_IO_WATCHDOG_LIMIT_MS`` (3000 ms).
- **Fade-to-black** — the sanctioned screen-switch exception: the
  CO5300's *hardware* brightness ramp (0x51, 12 steps, 240 ms)
  writes no pixels — the new screen's one atomic flush runs while
  the panel is still black (Chapters 13, 14).
- **Keep-mask** — the spin's UI overlay: the ring band + the
  full-screen ``lv_snapshot`` (rotated by the panel step, the moon
  hidden) so the clock stays legible over the rotating moon
  (Chapters 12, 14).
- **90°-step orientation** — the tilt roll: whole-screen orientation
  in 90° steps (the int4-quantized g + the 1 s windowed mean + the
  integer dot/cross quadrant), the +90° mount base (Chapter 15).
- **Departure wake** — the tilt-wake model: the wake fires on
  *departure* from a settled-pose EMA (two bars + a 2-sample
  streak), not raw per-sample deltas (Chapter 15).
- **moon_assets** — the 2,470,400 B flash partition: the 864×432
  equirect albedo + the 2048×1024 int8 relief DEM (Chapters 20, 24).
- **Relief gradient cache** — the one-time boot build of the DEM's
  quantized slopes into a packed uint16 array (the 2 PSRAM loads/px
  bake, Chapters 11, 19).

Project jargon
^^^^^^^^^^^^^^^

- **The art pass** — the display-constant pass (the backlit dark
  side, the soft terminator, the warm tint) that is *deliberately*
  outside the gate's physics path (Chapter 8).
- **The standing rule** — CLAUDE.md's doc rule: after every change,
  fix ``docs/doc_template.md``, regenerate ``docs/DOCUMENTATION.md``,
  commit them with the code (Chapter 24).
- **The clean-tree gate** — the stop-hook that commits an in-flight
  working tree; the ``wip:`` commits in the project's history are
  its records (Chapter 20).
- **The port map** — the two serial ports: ``/dev/ttyACM0`` = P4,
  ``/dev/ttyACM1`` = S3 (the peer's boards; the S3 flash happens on
  its owner's timing).
- **The wheel (v2)** — the shipped 12-sector hybrid password
  keyboard (Chapter 21).
- **The lab** — the interactive design lab at
  ``docs/keyboard/round-keyboard-lab.html`` (the 13 operable
  input-method panels, Chapter 21).
- **uisim** — the host UI simulator: the real screens rendered to
  PNG with re-driven indev timestamps (``host_sim/uisim``).
- **moon_sim / moon_bench** — the host C-sim (the gate's C frames)
  and the host performance bench (the exact device core, Chapter 19).
- **A/B** — the relief-on/off comparison (the PSRAM-load-latency
  floor, Chapter 19).

References
----------

Primary sources (the chapters' citations)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **NASA SVS 5415** — the ground-truth rendered moon (8,760 frames
  across a synodic cycle, 400×400 crops; the gate's reference,
  Chapter 4).
- **CGI Moon Kit** (F. Roehrich) — the renderer behind SVS 5415
  (Chapter 4).
- **LROC albedo / LOLA height** (NASA GSFC / USGS) — the 16-bit
  texture + the 2048×1024 DEM (Chapters 4, 5).
- **ERFA** (the International Astronomical Union's ephemeris
  routines; the vendored eraEpv00 + eraMoon98) (Chapter 5).
- **IAU 2015 lunar rotation** — the *default* body frame the
  project's calibrated pole replaces (Chapter 5).
- **ESP-IDF 5.5.1** (Espressif) — the RTOS + the SPI/QSPI/DSI/DMA
  drivers (Chapters 10, 13, 16).
- **LVGL 9.6** — the UI framework (the flush path, the arcs, the
  ``ADV_HITTEST`` contract) (Chapters 10, 21).
- **Waveshare ESP32-S3-Touch-AMOLED-1.75** (CO5300 QSPI, CST9217,
  QMI8658C, PCF85063A, TCA9554) — the S3 board (Chapter 2).
- **Waveshare ESP32-P4-WIFI6-Touch-LCD-XC "4C"** (JD9365 MIPI-DSI,
  GT911, remote C6 over SDIO) — the P4 board (Chapter 3).

The project's in-tree research
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- ``docs/research/moon-rendering.md`` — the moon-rendering research
  (the model + the validation approach, Chapter 5).
- ``docs/research/circular-keyboard-alternatives.md`` — the
  round-keyboard deep research (the 13-method survey + the ranked
  verdict, Chapter 21).
- ``docs/research/gltf-feasibility.md`` — the (rejected) 3D/gltf
  feasibility note (the 2D bake's justification, Chapter 6).
- ``docs/keyboard/round-keyboard-lab.html`` — the interactive
  design lab (the operable 13 panels, Chapter 21).

The HCI literature (the keyboard's citations, Chapter 21)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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
  Chapter 17).
- ``host_sim/bench.c`` (``moon_bench``) — the performance bench
  (the C numbers, Chapter 19).
- ``host_sim/uisim`` — the UI simulator (the screenshots, Chapters
  9, 21).
- ``docs/gen_media.py`` / ``docs/gen_doc.py`` — the docs media + the
  generated doc (Chapter 24).
- The serial console (115200) + the ROM reset reason — the device
  verification surface (Chapter 18).

A note on the book's own numbers
--------------------------------

Every measured number in this book is a *reproducible* one: the host
numbers are the bench/gate commands in Chapters 17–19, and the device
numbers are quoted from the *accepted captures* (Chapter 18's
standard: progress + timing + zero failure lines). A number without
its capture is a hypothesis, not a verification — the book's
numbers all carry their captures, and the acceptance triple (it ran
to its endpoint, at its budget, with zero failure lines) is the
standard a re-measurement must meet to replace one.

That is the book. Twenty-five chapters: the product (Parts I–II),
the implementation (Part III), the validation (Part IV), and the
reference (Part V) — a physically correct moon on a round panel,
measured end to end, with the decisions — including the ones that
beat NASA on purpose — recorded where they were made.
