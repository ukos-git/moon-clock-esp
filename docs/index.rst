The Moon Phase Clock
====================

A physically correct moon-phase clock on a round ESP32 AMOLED panel —
how it was designed, how the math was validated against NASA, how it
runs on an ESP32-S3 (and an ESP32-P4), and how fast everything is.

This documentation covers a real, working product end to end: the C
core that bakes a moon frame on a microcontroller, the display
pipeline that ships each frame as one atomic whole-panel flush, the
animation system (boot sweep, burn-in spin, the "dusk"), the
sleep/wake machine, the settings stack, and the validation gates that
prove the rendering matches NASA's rendered moon to within a couple of
gray levels per pixel.

.. figure:: _static/photos/bench_front.jpg
   :alt: The device on the 3-D-printed bench stand

   The product on the 3-D-printed bench stand. The 3-D-shaded moon it
   renders — physically correct, down to the computed backlit dark
   side — is simulated from the same C core in :doc:`science/geometry`.

Contents
--------

.. toctree::
   :maxdepth: 2
   :caption: Start here

   getting-started
   overview

.. toctree::
   :maxdepth: 2
   :caption: The boards

   devices/index

.. toctree::
   :maxdepth: 2
   :caption: The user interface

   ui/index
   keyboard

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
