The interactive simulator
=========================

.. brief:
   Reader: someone who wants the model running live, or the JavaScript port's API.
   They already know: the four science pages (or will read them alongside).
   After this page they can: drive Moon Lab and watch each design decision take effect.
   Page type: explanation (with an API reference).

The previous four pages described a model. This page gives you
the model — running live, in your browser, at the exact math the device
runs. Below is **Moon Lab**: a line-faithful JavaScript port of the
portable C core (``moon_bake.c``), driven by the validated PyERFA
ephemeris, precomputed into a small table. Every pixel you see is
computed by your browser from the same equations the ESP32-S3 executes
at 240 MHz — the same displaced-normal Lambert, the same relief
gradient cache (on a 1024×512 DEM downsample), the same tone LUT, the same backlit dark side,
the same soft terminator, the same warm tint.

.. note::
   The simulator is embedded as a page. If it does not appear, open
   ``_static/moonjs/moonlab.html`` directly from the site's static
   directory — it is fully self-contained (no network, no build step).

.. raw:: html

   <iframe src="../_static/moonjs/moonlab.html"
           style="width:100%; min-height:880px; border:0; border-radius:12px;"
           loading="lazy" title="Moon Lab — interactive moon simulator"></iframe>

The short version
-----------------

This page is the previous pages made *touchable*. Moon Lab is the
project's renderer — the same C code that paints the moon on the
devices — translated line-for-line into JavaScript and shipped inside
the page above, so your browser runs the very equations the ESP32-S3
executes at 240 MHz. Nothing is faked for the web: the terrain shadows,
the dark-side fill, the terminator blend and the warm color table are
the device's, and the dates come from the same validated ephemeris.

Three ways to use it, in order of depth:

1. **Play.** Move the date and watch a month of phases pass. This is
   the fastest way to build the intuition the earlier pages assume.
2. **Test the claims.** Every toggle is one decision from an earlier
   page, so you can watch *why* each decision was made: turn the
   terrain off and see the cheap model get the day/night line wrong;
   drag the dark-side level and watch the night half go from silhouette
   to over-bright.
3. **Program it.** The JavaScript API at the bottom of the page is the
   same bake you can call from Node or your own page — and the site's
   own A/B figures are generated with it, which is what keeps the
   simulator, the figures and the device provably in agreement.

The one honest difference from the device is the geometry (where the
Sun and Moon are at each instant): a full ephemeris is far too large
for a browser, so its answers are precomputed into a table — sampled
every 2 hours across 2026 and 2027 by the *same validated Python code*
the image gate runs, then blended between the nearest rows. That
blend's error is far below the ephemeris's own tolerance, so in
practice the browser and the device agree pixel for pixel.

Vocabulary
----------

Twelve terms carry the page (the full definitions live in the
:doc:`glossary <../glossary>`):

- **Moon Lab** — this page's simulator: the portable C core ported to
  JavaScript and embedded as a fully self-contained page (no network,
  no build step).
- **Port** — a translation of existing code into another language that
  keeps its structure line-for-line, as opposed to a fresh
  reimplementation; what makes the browser a trustworthy witness of
  the device's math.
- **Bake** — compute one whole frame pixel by pixel from geometry and
  textures (the C core's ``moon_bake``); the lab shows its wall-clock
  time live.
- **Ephemeris** — the machinery that says where the Sun, Earth and
  Moon are at any instant; here PyERFA, validated on :doc:`the
  geometry <geometry>` page.
- **Geometry table** — the ephemeris's answers precomputed every 2
  hours across both years (8760 rows) and read back by **linear
  interpolation**: blend the two nearest rows in proportion to how far
  your instant sits between them.
- **Terminator** — the day/night boundary on the visible disk.
- **Relief DEM** — the terrain height grid. "Relief DEM: on" tilts each
  surface point's normal by the real terrain so craters cast shadows
  (:doc:`the lighting model <lighting>`); off is the flat sphere — the
  cheap model that gets the terminator wrong, kept for comparison.
- **Backlit dark side / K** — the display's night-half fill and its
  single level constant :math:`MOON_NIGHT_BACKLIT_K` (:doc:`the dark
  side <darkside>`).
- **Terminator softness** — the smoothstep band width
  :math:`MOON_TERM_SOFT` that replaces the razor edge; 0.03 is the
  shipped value.
- **Warm tint** — the display's warm gray→color tone table
  (:doc:`the artistic pass <art-pass>`); off = neutral gray.
- **Base64** — the text encoding that lets binary assets (the albedo
  image, the terrain grid, the tone table) ship inside a JavaScript
  file with no network requests.
- **Isomorphic figures** — the site's A/B figures rendered by this
  same ``moonlab.js`` bake instead of a separate figure engine, so a
  figure can never quietly disagree with the simulator.

A guided tour
-------------

**The short version.** Six steps that walk one synodic month and
re-enact each model decision along the way: each says what to touch,
what you should see, and which earlier page's claim you are watching.
The dates are real 2026 phase instants confirmed against the validated
ephemeris — the simulator's table grid has each one exactly.

1. **Watch a month pass.** Set the date to **2026-04-17 15:00** — new
   moon: the disk is a black circle carrying only the faint backlit
   maria. Step forward: **2026-04-23 18:00** is first quarter,
   **2026-05-01 21:00** full, **2026-05-09 10:00** last quarter. The
   terminator sits exactly where NASA's geometry feed says it must —
   this is :doc:`the geometry <geometry>` page, live.
2. **Break the terminator.** Park on the first quarter and toggle
   **Relief DEM** off. The mountain shadows along the day/night line
   vanish and its shape shifts a touch: you are looking at the flat
   base-sphere model the project rejected (:doc:`the lighting model
   <lighting>`). Toggle it back on.
3. **Dial the night half.** Drag **K** (the backlit dark-side level)
   from 0 to 0.5 while watching the dark half. At 0 it is a silhouette;
   at the shipped 0.20 the maria read as a moon in shadow; past ~0.35
   the relief over-brightens. This is :doc:`the artistic pass
   <art-pass>`'s calibration in one slider.
4. **See the two dark-side models.** Toggle **Backlit dark side** off
   (on a crescent — at full moon there is no night half to compare).
   Off is the gate's earthshine path — physics, no artistic fill; on
   is the display path. Same code, one flag (:math:`dark\_zero`), two
   jobs.
5. **Harden the terminator.** Set **terminator softness** to 0: the
   night-to-day switch happens inside one pixel — the cutout edge the
   artistic pass removed. Back to 0.03 for the shipped value.
6. **Warm it and time it.** Toggle **Warm tint** for the display's
   half-step of warmth, then read the live **bake time**: a 466×466
   frame in JavaScript on a laptop is a few tens of milliseconds, on
   the S3 in C about 1.3 s — the same math, two orders of magnitude
   of hardware (:doc:`performance <../verification/performance>`).

What you can drive
------------------

**The short version.** Six controls, and every one of them is a
decision from an earlier page of this section — the list below names
which. If you only want the feel of the model, drive the date; if you
want to audit a decision, the toggle for it is here.

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

**The short version.** Two halves. Everything that touches a pixel is
a line-faithful port of the C core — the table below pairs each C
function with its JavaScript twin, so auditing the port means reading
the pairs side by side. The one part that could not be ported is the
ephemeris itself (too large for a browser), so its answers ship
precomputed: a 2-hourly geometry table generated by the validated
Python path and blended between rows at run time.

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
``moonlab_data.js`` — the same 8-bit albedo the S3 bakes from, and a
downsample of the same int8 DEM source (the S3's core builds its cache
from the full 2048×1024 grid; the browser carries the halved
downsample).

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

**The short version.** The site's A/B figures come out of *this same
bake*, not a separate figure engine — one renderer means a figure can
never quietly drift from what the simulator (and the device) produce.
The three-line recipe below regenerates them; the rule attached to it
is the point: change a constant in the C core, change it here too,
and all three views stay in agreement.

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
   host_sim/.venv/bin/python docs/tools/assemble_figures.py /tmp/bookfigs

``gen_figures.js`` calls ``MoonLab.bake`` with explicit parameter sets
(K, soft, relief, backlit, warm) and writes PPM frames;
``docs/tools/assemble_figures.py`` (a small PIL script) composes them
into the labeled montages you see in the other
pages. If you change a constant in the C core, you change it here too
— and the figures, the sim, and the device all agree.

The JS API
----------

**The short version.** One class, one call. Load the module, hand the
constructor the embedded data, and ``bake(instant, size, knobs)``
returns a frame as one byte per pixel — the same values the C core
would write. The minimal driver is six lines; what each line does is
spelled out right below it.

``moonlab.js`` exports a single class. The minimal driver:

.. code-block:: javascript
   :caption: Baking one frame (Node or browser)

   const { MoonLab } = require("./moonlab.js");      // or window.MOONLAB
   const lab = new MoonLab(MOONLAB_DATA);            // the data from moonlab_data.js

   const ms  = Date.parse("2026-04-15T12:00:00Z");
   const gray = lab.bake(ms, 466, { K: 0.20, soft: 0.03, relief: true, backlit: true });
   // gray: Uint8ClampedArray, 466*466, row-major, 0 outside the disk

   lab.draw(canvas, gray, 466, /* warm */ true);     // browser only

What those lines do, one by one: ``require("./moonlab.js")`` loads the
port in Node (in a browser the same class is already on ``window`` as
``MOONLAB`` — see the iframe above). ``new MoonLab(MOONLAB_DATA)``
hands the constructor the embedded asset bundle from
``moonlab_data.js`` — the 8-bit albedo, the int8 relief, the tone
table and the geometry table, all base64 — and builds the relief
gradient cache once, up front. ``Date.parse`` converts an ISO UTC
instant to the milliseconds-since-epoch number every entry point of
the API takes. ``lab.bake(ms, 466, {...})`` is the per-pixel loop:
it looks up the geometry for that instant (table interpolation), then
computes the 466×466 frame — the S3 panel's edge length — with the
shipped display constants (dark-side level 0.20, softness 0.03,
terrain on, backlit on). The return is a ``Uint8ClampedArray`` of
466×466 bytes, row-major, one byte per pixel, with 0 outside the round
disk. Finally ``lab.draw`` paints those bytes onto a ``<canvas>`` and
runs them through the tone table (``true`` = warm, ``false`` =
neutral); it is browser-only because Node has no canvas — in Node the
``gray`` array itself is the output (this is how ``gen_figures.js``
consumes it, writing PPM frames).

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
