The interactive simulator
=========================

The previous four pages described a model. This page gives you
the model — running live, in your browser, at the exact math the device
runs. Below is **Moon Lab**: a line-faithful JavaScript port of the
portable C core (``moon_bake.c``), driven by the validated PyERFA
ephemeris, precomputed into a small table. Every pixel you see is
computed by your browser from the same equations the ESP32-S3 executes
at 240 MHz — the same displaced-normal Lambert, the same 2048×1024
relief gradient cache, the same tone LUT, the same backlit dark side,
the same soft terminator, the same warm tint.

.. note::
   The simulator is embedded as a page. If it does not appear, open
   ``_static/moonjs/moonlab.html`` directly from the site's static
   directory — it is fully self-contained (no network, no build step).

.. raw:: html

   <iframe src="../_static/moonjs/moonlab.html"
           style="width:100%; min-height:880px; border:0; border-radius:12px;"
           loading="lazy" title="Moon Lab — interactive moon simulator"></iframe>

What you can drive
------------------

- **Date** — any 2-hour instant across 2026 **and 2027**. The phase,
  illumination, and terminator position follow the validated ephemeris.
- **Backlit dark-side level (K)** — :math:`MOON_NIGHT_BACKLIT_K`, the
  constant :doc:`the artistic pass <art-pass>` calibrated. Drag it from 0 (silhouette) to 0.5
  (too loud) and watch the dark side's relief emerge and then
  over-brighten.
- **Terminator softness** — :math:`MOON_TERM_SOFT`, the smoothstep band
  width. 0 is the hard gate path; 0.03 is the shipped display value.
- **Relief DEM** — on = the displaced-normal model (:doc:`the lighting model <lighting>`); off =
  the flat base-sphere :math:`\max(0,d)`, the cheap model that gets the
  terminator wrong.
- **Backlit dark side** — on = the display path (:math:`dark_zero=1`,
  the computed backlit surface); off = the validated earthshine model
  (:math:`dark_zero=0`, the gate's path).
- **Warm tint** — on = the display's warm gray→RGB565 LUT; off = neutral.

The bake time is shown live (a 466×466 frame on a laptop runs in a few
tens of milliseconds in JavaScript — compare that to the S3's ~1.3 s in
C on octal PSRAM (:doc:`performance <../verification/performance>`); this
page is its own performance lesson).

How it is built
---------------

The simulator is a *port*, not a re-derivation. ``moonlab.js`` is a
line-faithful translation of the C core:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - C core
     - JavaScript (``moonlab.js``)
   * - ``bake_px`` (``moon_bake.c``)
     - ``MoonLab.prototype.bake`` (the per-pixel loop)
   * - ``moon_relief_build`` / ``moon_relief_row``
     - ``MoonLab.prototype._buildRelief`` (the gradient cache)
   * - ``moon_backlit_night``
     - ``MoonLab.prototype._backlit``
   * - ``earthshine``
     - ``MoonLab.prototype._earthshine``
   * - ``g565_build`` (``moon_render.c``)
     - ``MoonLab.prototype._g565`` (warm/neutral LUT)
   * - (geometry)
     - ``MoonLab.prototype.geometryAt`` (table lerp)

The geometry — the one thing that cannot be cheaply computed in the
browser without a full ephemeris library — is precomputed by
``docs/tools/gen_moonjs.py`` using the *same code path* as the
validated Python prototype (``moon.astro``: PyERFA ephemeris + the
calibrated IAU_Moon body frame), sampled 2-hourly across both years
(8760 rows) into a :math:`\text{Float32}` table. ``geometryAt`` linearly interpolates that
table and renormalizes the vectors; the error is well below the
ephemeris's own tolerance.

The two texture assets (the 864×432 albedo and a 1024×512 downsample of
the relief DEM) and the 256-entry tone LUT are embedded base64 in
``moonlab_data.js`` — the same 8-bit albedo and int8 DEM the C core
bakes from.

.. figure:: ../_static/figs/phase_strip.png
   :alt: A full synodic month, rendered by the simulator
   :figclass: align-center

   One synodic month (new → crescent → quarter → gibbous → full →
   gibbous → quarter → crescent), rendered by the simulator at the
   display settings (backlit dark side, K = 0.20, soft terminator, warm
   tint). The relief-DEM mountain shadows are visible at every
   terminator; the backlit dark side carries the maria across the whole
   cycle.

Isomorphic figures
------------------

The A/B figures in this section (:doc:`the artistic pass <art-pass>`, :doc:`the image gate <../verification/image-gate>` and :doc:`performance <../verification/performance>`) are generated from the
**same** ``moonlab.js`` bake, not from a separate "figure engine" — a
separate renderer could drift from the reader-facing sim, which would be
the worst possible failure for a page about validation. The pipeline
is:

.. code-block:: bash
   :caption: Regenerating the site's moon figures

   # 1. The geometry table + textures (needs the gate venv: numpy, astropy)
   host_sim/.venv/bin/python docs/tools/gen_moonjs.py

   # 2. The A/B frames (needs node; writes PPM to a scratch dir)
   node docs/tools/gen_figures.js /tmp/bookfigs

   # 3. The montages (PIL, from the same gate venv)

``gen_figures.js`` calls ``MoonLab.bake`` with explicit parameter sets
(K, soft, relief, backlit, warm) and writes PPM frames; a small PIL
script composes them into the labeled montages you see in the other
pages. If you change a constant in the C core, you change it here too
— and the figures, the sim, and the device all agree.

The JS API
----------

``moonlab.js`` exports a single class. The minimal driver:

.. code-block:: javascript
   :caption: Baking one frame (Node or browser)

   const { MoonLab } = require("./moonlab.js");      // or window.MOONLAB
   const lab = new MoonLab(MOONLAB_DATA);            // the data from moonlab_data.js

   const ms  = Date.parse("2026-04-15T12:00:00Z");
   const gray = lab.bake(ms, 466, { K: 0.20, soft: 0.03, relief: true, backlit: true });
   // gray: Uint8ClampedArray, 466*466, row-major, 0 outside the disk

   lab.draw(canvas, gray, 466, /* warm */ true);     // browser only

.. code-block:: javascript
   :caption: The geometry + phase API

   const g = lab.geometryAt(ms);      // { S, E, right, up, phase, illum }
   const p = lab.phaseLabel(ms);      // { name, illum, phase }

   // p.name is one of: "new moon", "waxing crescent", "first quarter",
   // "waxing gibbous", "full moon", "waning gibbous", "last quarter",
   // "waning crescent"

The ``bake`` parameters:

.. list-table::
   :header-rows: 1
   :widths: 18 20 62

   * - param
     - default
     - meaning
   * - ``K``
     - required
     - backlit dark-side level (:math:`MOON_NIGHT_BACKLIT_K`)
   * - ``soft``
     - 0
     - terminator smoothstep half-width (:math:`MOON_TERM_SOFT`, :math:`lns` units)
   * - ``relief``
     - true
     - displaced-normal model (false = flat base-sphere Lambert)
   * - ``backlit``
     - false
     - true = display path (:math:`dark_zero=1`); false = gate/earthshine path

The C equivalent, for reference:

.. code-block:: c
   :caption: The same frame from the C core

   moon_geom_t g;
   moon_geometry(utc_epoch, &g);
   uint8_t frame[SIZE * SIZE];
   moon_bake(&g, albedo, &grel, frame, SIZE, RADIUS, CX, CY,
             MOON_ALB_W, MOON_ALB_H,
             /* dark_zero */ 1,          /* backlit */
             /* dk_sup    */ 0.0f);

The model is now something you can touch. The implementation section
turns to how this math actually runs on a microcontroller, how
a frame gets onto a panel, and how fast every one of these steps really
is.
