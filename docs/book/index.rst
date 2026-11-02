The Moon Phase Clock
====================

A physically correct moon-phase clock on a round ESP32 AMOLED panel —
how it was designed, how the math was validated against NASA, how it
runs on an ESP32-S3 (and now an ESP32-P4), and how fast everything is.

This book documents a real, working product end to end: the C core that
bakes a moon frame on a microcontroller, the display pipeline that ships
each frame as one atomic whole-panel flush, the animation system (boot
sweep, burn-in spin, the "dusk"), the sleep/wake machine, the settings
stack, and the validation gates that prove the rendering matches NASA's
rendered moon to within a couple of gray levels per pixel.

It is written as an engineering article, not a datasheet. Every chapter
has the working code, the measured numbers, and the decisions —
including the ones that beat NASA on purpose, and the ones that were
measured, rejected, and documented anyway.

.. figure:: _static/photos/bench_front.jpg
   :alt: The device on the bench stand, showing the current (near new-moon) phase

   The product on the bench stand, photographed for this book (November
   2026). The panel shows the current phase — a thin lit crescent with
   the computed, backlit dark side everywhere else.

Contents
--------

.. toctree::
   :maxdepth: 2
   :caption: Part I — The product

   part1/01-overview
   part1/02-hardware-s3
   part1/03-hardware-p4
   part1/04-reference

.. toctree::
   :maxdepth: 2
   :caption: Part II — The science

   part2/05-geometry
   part2/06-lighting
   part2/07-darkside
   part2/08-artpass
   part2/09-interactive

.. toctree::
   :maxdepth: 2
   :caption: Part III — The implementation

   part3/10-architecture
   part3/11-core-c
   part3/12-rotate-lut
   part3/13-display
   part3/14-animations
   part3/15-clock-sleep
   part3/16-p4port

.. toctree::
   :maxdepth: 2
   :caption: Part IV — Verification and performance

   part4/17-imagegate
   part4/18-device-verify
   part4/19-performance
   part4/20-binary

.. toctree::
   :maxdepth: 2
   :caption: Part V — UX and reference

   part5/21-keyboard
   part5/22-python-api
   part5/23-c-api
   part5/24-build-kconfig
   part5/25-glossary-references

How to read this book
---------------------

- **Part I** is short and needs no prerequisites: what the product is,
  what hardware it runs on, and where the reference images came from.
- **Part II** is the science. It assumes some linear algebra and will
  use a little astronomy; the interactive simulator in Chapter 9 lets
  you drag the phase and watch every model parameter move.
- **Part III** is the implementation. It walks the C core file by file
  with code listings and the panel's DMA constraints; it is where the
  "why is it written this way" lives.
- **Part IV** is the evidence: the image gate, the on-device
  verification, and the performance measurements (fresh benchmark
  runs, not the marketing numbers).
- **Part V** is the UX research (the round keyboard, with the full
  decision record) and the API references.

Conventions
-----------

- Times are UTC unless marked otherwise. "Frame" means one 466×466
  (S3) or 720×720 (P4) whole-panel image.
- "Gray" means 8-bit luminance 0–255; "5-bit" means the panel's R5G6B5
  fidelity after the gray→RGB565 LUT.
- Code listings are quoted from the tracked sources in this repository
  at the commit the book was generated from; line numbers in prose are
  approximate — search for the symbol instead.
- The repository's own documentation (``docs/DOCUMENTATION.md``) is
  generated from the same sources and stays in lockstep with the code;
  this book is the long-form companion, not a replacement.
