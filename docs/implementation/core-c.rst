The portable C core
===================

This page walks the device-side renderer: the dependency-free C
library in ``components/moon`` that turns a UTC epoch into a 400×400
gray frame. Everything in it compiles unchanged on the host (the
simulator, the gate) and on both device boards (the Waveshare
ESP32-S3-Touch-AMOLED-1.75, "S3", and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC, "4C"); the only platform assumptions
are a float FPU and a ``malloc``-like allocator.

The short version
-----------------

The whole library answers one question: *what does the Moon look like
from Earth at this instant, as pixels?* The input is a single integer
(a UTC epoch); the output is a square of gray brightness values, one
byte per pixel, complete down to the jagged mountain shadows at the
terminator. Four answers cover everything below:

1. **It is a library, not a program.** No tasks, no drivers, no
   hardware headers: the identical source compiles into the host
   simulator running on a PC *and* onto both boards. Its complete list
   of platform wishes is a float FPU and a ``malloc``-like allocator —
   nothing else.
2. **A frame is three steps.** *Where is everything* — the ephemeris
   computes the Sun/Earth/Moon positions for the instant (a few
   milliseconds, once per frame, not per pixel). *How bright is each
   pixel* — the bake repeats the lighting recipe over the whole square
   (~1.3 s: the hot path). *What color* — a look-up table turns the
   gray bytes into display color outside the core, so the core never
   learns a panel exists.
3. **Speed comes from tables, not tricks.** The chip's math hardware
   has no arc tangent, and terrain slopes would mean re-reading the
   height map several times per pixel. The answer is to precompute: a
   1024-entry sine/cosine table, a 257-entry arctangent table (with
   its row-fraction twin), the
   4 MB wall of terrain slopes built once at boot, the 256-entry tone
   curve — and the per-pixel work collapses to a few multiplies and
   table reads.
4. **The contract is bytes, not intentions.** The bake is a pure
   function: same inputs, same bytes, on every platform. That is what
   lets the :doc:`image gate <../verification/image-gate>` compare the
   C core's frames against the Python prototype and NASA's and read
   the agreement as proof, not luck.

Vocabulary
----------

Fourteen terms carry the page — the astronomy terms also have fuller
entries in the :doc:`glossary <../glossary>`:

- **Epoch (UTC epoch)** — seconds since 1970-01-01 00:00 UTC: one
  integer that says "which instant." The library's only input.
- **Ephemeris** — the astronomical formulas that answer "where are the
  Sun, Earth and Moon at this instant?" Computed from theory, not
  looked-up observations.
- **ERFA** — the International Astronomical Union's C implementation
  of the standard ephemeris formulas.
- **Barycenter (EMB)** — the center of mass that two bodies orbit
  together: Earth and Moon both circle their common barycenter, and
  ERFA's positions are relative to it.
- **Vendored** — someone else's source code copied into this tree and
  built with it: no download, no dependency at build time (the
  ``core/erfa/`` directory is ERFA, vendored).
- **Host** — a normal PC, as opposed to the device: the simulator and
  the gate run "on the host."
- **FPU** — *floating-point unit*: the hardware that adds and
  multiplies fractional numbers fast. The core assumes it exists and
  assumes nothing else about the chip.
- **libm** — the standard C math library (``sinf``, ``atan2f``, …):
  correct, software-only, and too slow for the hot path.
- **Hot path** — code that runs once per pixel (160,000 times a
  frame): anything measurably slow there costs seconds, so slowness is
  designed out of it rather than optimized away later.
- **Fixed-point (Q15.15)** — writing a fraction as an integer, with
  15 bits below the binary point (so 1.0 is the integer 32768):
  additions stay exact and cheap. The rotation gather (:doc:`rotation
  and the LUT <rotate-lut>`) works entirely in these.
- **Look-up table (LUT) / linear interpolation** — an array of
  precomputed answers indexed by the question. For a value between two
  entries, *interpolate*: take the two neighbors and slide between
  them in proportion to the distance.
- **Gray frame** — the core's output: one brightness byte per pixel,
  0–255. Color exists nowhere inside the core.
- **Pure function / stateless** — output depends only on the inputs:
  call it twice with the same arguments anywhere, get the same bytes;
  nothing hides in global memory. The core's only state is two tables
  built on first use.
- **Lazily built / idempotent** — built on first use rather than at
  startup; and calling the builder a second time does no more than the
  first call did, which makes "build it now, just in case" safe.

The file layout
---------------

**The short version.** Four source files and three headers are the
entire renderer — plus the vendored ERFA routines beneath them. The
tree reads top-down: ``include/`` is what the
rest of the firmware may see (device code includes exactly one
header, ``moon.h``), ``core/`` is the machinery behind it. Read the
API listing below as the pipeline itself: ``moon_geometry`` asks
*where* everything is, ``moon_bake`` answers *how bright* every pixel
is, ``moon_rotate`` *turns* a baked square, ``moon_gray565`` *colors*
it — and each of those has its section further down this page.

::

  components/moon/
  ├── include/
  │   ├── moon.h          — the public API (the only header device code
  │   │                     includes)
  │   ├── moon_consts.h   — the generated constants (:doc:`the reference data <../science/reference-images>`): body
  │   │                     frame, DEM range, earthshine fits, tone LUT
  │   └── moon_assets.h   — the albedo/DEM embedded assets + loaders
  └── core/
      ├── moon_astro.c    — moon_geometry: ERFA ephemeris -> moon_geom_t
      ├── moon_bake.c     — moon_bake/moon_reproject/shade map + the
      │                     relief gradient cache (the hot path)
      ├── moon_render.c   — moon_rotate (Q15.15 gather) + the gray->565
      │                     LUT (moon_render.c)
      ├── moon_fixp.c     — the 1024-entry Q15.15 cos/sin quarter table
      └── erfa/           — vendored public-domain ERFA (eraEpv00,
                            eraMoon98 + the supporting routines)

The public surface is deliberately small:

.. code-block:: c
   :caption: components/moon/include/moon.h (abridged)

   void moon_geometry(int64_t utc_epoch, moon_geom_t *g);
   const uint8_t *moon_albedo(void);
   moon_grel_t *moon_relief(void);                       /* builds the cache once */
   void moon_bake(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel,
                  uint8_t *frame, int size, int radius, int cx, int cy,
                  int alb_w, int alb_h, int dark_zero, float dk_sup);
   void moon_bake_rows(const moon_geom_t *g, ...);       /* one row sweep */
   void moon_shade_map_build(const moon_geom_t *g, ...);/* the dusk's map (host-side only
                                                           since the dusk was retired
                                                           2026-11-13 — shared verbatim) */
   void moon_reproject(const uint8_t *map, int map_w, int map_h,
                       const moon_geom_t *g, uint8_t *frame, int size, ...);
   void moon_rotate(const uint8_t *src, int n, uint16_t *dst, int dst_w,
                    int dst_x, int dst_y, int r, int cx, int cy,
                    int c15, int s15);
   uint16_t moon_gray565(uint8_t g);
   void moon_gray565_set_warm(int on);

The row-level API (``moon_bake_rows`` et al.) exists because the
device's bake tasks publish rows as they finish (:doc:`the animation pipelines <animations>`): a 1.3 s
frame is *displayed* in ~175 ms strips while the rest is still
baking. ``moon_bake`` is the convenience wrapper that calls the
rows function ``size`` times.

The geometry step
-----------------

**The short version.** Before a single pixel can be lit, the code must
know where the Sun and Earth sit relative to the Moon and how the Moon
is tilted at this instant. That part is real astronomy — so it is
delegated to the vetted ERFA routines rather than re-derived here. Two
calls return the positions; ~200 lines turn them into the camera-ready
directions packed into ``moon_geom_t``. The bill is a few milliseconds
once per frame — this is the one step allowed to afford slow, exact
math.

``moon_astro.c`` is ~200 lines and runs once per frame (a few
milliseconds, dominated by the ERFA series). It computes the Earth and
Moon barycentric positions with the vendored ``eraEpv00`` +
``eraMoon98`` — the *same low-order models* the validated Python
prototype uses via PyERFA's ``builtin`` backend — and builds the body
basis with the calibrated pole and prime meridian from ``moon_consts.h``
(:doc:`the geometry <../science/geometry>`):

.. code-block:: c
   :caption: components/moon/core/moon_astro.c (the two ephemeris calls)

   eraEpv00(d1, d2, pvh, pvb);          /* Earth + EMB barycentric */
   eraMoon98(d1, d2, mpv);              /* Moon around the EMB */
   /* S = unit(sun - moon), E = unit(earth - moon), both rotated into
    * the body frame by the pole + W(JD) Rodrigues basis; the camera
    * basis follows the celestial-north-up, east-left convention. */

In plain words: ``eraEpv00`` answers "where is the Earth (and the
Earth–Moon center of mass) around the Sun," and ``eraMoon98`` answers
"where is the Moon around that center." The comment sketches the rest:
turn both positions into arrows *from the Moon* toward the Sun and
toward the Earth, spin them into the Moon's own rotating frame (the
calibrated pole + meridian from the generated header), and finally
into the camera's "north up, east left" view.

The result fills ``moon_geom_t`` (:doc:`the geometry <../science/geometry>`). Note what it does *not*
do: no trigonometric libm calls on the hot path (the ephemeris itself
uses ``sinf``/``cosf`` in tabled or inline form), no allocation, no
globals. The core is stateless except for two lazily built tables in
``moon_bake.c``.

The lookup tables
------------------

**The short version.** The bake needs an inverse tangent per pixel —
and the Xtensa's math hardware has none, so a software call 160,000
times would sink the frame budget. The fix is a dictionary: precompute
257 answers covering a small slice of the problem, keep only that
slice's table in fast internal RAM, and *fold* every input into the
slice and its answer back out. The code below is the fold — and the
warning under it guards one of this pipeline's two classic failure
modes: the angle convention is [0, 360), not C's (−180, 180].

The Xtensa FPU has no ``atan`` or ``asin``, and the texture
longitude/latitude per pixel would cost a libm call each. Two
257-entry internal-RAM tables replace them:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the tabled atan2)

   static float s_atan_deg[MOON_TBL_N];  /* atan(i/256) in degrees */
   static float s_row_frac[MOON_TBL_N];  /* row fraction for rz in [-1,1] */

   static inline float atan_interp(float t) {          /* t in [0,1] */
       float f = t * 256.0f;
       int i = (int)f;
       float fr = f - (float)i;
       return s_atan_deg[i] + fr * (s_atan_deg[i + 1] - s_atan_deg[i]);
   }

   static inline float atan2_deg(float x, float y) {
       if (x == 0.0f && y == 0.0f)
           return 0.0f; /* poles: atan2(0,0) = 0 in both C and Python */
       float ax = x < 0.0f ? -x : x;
       float ay = y < 0.0f ? -y : y;
       float a;
       if (ay > ax) { /* keep the ratio in [0,1] */
           a = 90.0f - atan_interp(ax / ay);
           if (x < 0.0f)
               a += 180.0f;
           return y >= 0.0f ? a : 360.0f - a;
       }
       a = atan_interp(ay / ax);
       if (x < 0.0f)
           a += 180.0f;
       return y >= 0.0f ? a : 360.0f - a;
   }

Reading the function: the first test reproduces C's ``atan2(0, 0)``
behavior (the poles map to 0 — and, critically, to 0 on the Python
side too, so both worlds agree). The ``ay > ax`` branch is the fold:
swapping the axes' roles turns the ratio into ``ax/ay``, which can
never leave [0, 1] — the table's entire domain — and ``90° − atan``
converts the swapped answer back to the real one. The ``+180°`` and
``360° − a`` adjustments are the fold-back: which quarter of the
circle the original input lived in.

Two details matter. The ratio is kept in [0,1] (swap + 90° − atan) so
the table needs only the [0, π/2] quadrant; and the result is in the
**[0, 360) convention, measured from +x toward +y** — the same
convention the Python ``moon.astro`` uses. A port that "simplifies"
this to the C ``atan2`` range (−180, 180] will produce a moon that is
correct at the poles and wrong everywhere else (the west half mirrors):
the 180°-convention bug is one of the two classic failures in this
pipeline, next to the 180° texture offset (:doc:`the geometry <../science/geometry>`).

Worst-case table error is a fraction of one texture pixel — well under
one texture row — and is re-verified by the gate after any change.

The relief gradient cache
-------------------------

**The short version.** The bake needs the ground's slope at every
pixel (:doc:`the lighting model <../science/lighting>` explains why),
and deriving it on the fly would mean re-reading the terrain height
map — from the slow PSRAM chip — several times per pixel, on every
frame, forever. So the device pays for it once, at boot: it walks the
height map, computes every cell's slope, rounds each slope to one
byte, and packs the east and north slopes into a single 16-bit word.
From then on the bake reads one prepared word per pixel and derives
nothing. The cache depends only on the terrain, not on the Moon's
phase — one build serves every frame the device ever renders. (The
cell-by-cell read of that code lives on the lighting page, next to
the math it implements.)

The build path is ``moon_relief()`` (lazy, idempotent, called
eagerly at ``moon_ui_init`` on the main task so no two bake tasks can
race the build). ``moon_relief_build`` reads the 2 MB int8 DEM **one
time, whole**, and writes a 4 MB packed PSRAM array:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (one interior cell)

   static inline void relief_cell(const int8_t *row_n, const int8_t *row_m,
                                  const int8_t *row_s, int c, float mps,
                                  float inv_glon, float inv_glat, uint16_t *gpack) {
       float dhlon = ((float)row_m[c + 1] - (float)row_m[c - 1]) * mps;  // east-west
       float dhlat = ((float)row_n[c]     - (float)row_s[c])     * mps;  // north-south
       int8_t qn = slope_q(dhlon * inv_glon), qt = slope_q(dhlat * inv_glat);
       gpack[c] = (uint16_t)((uint16_t)(uint8_t)qn | ((uint16_t)(uint8_t)qt << 8));
   }

The quantization is to **int8 units of 0.003 rad**
(``MOON_GREL_INV``): ``±127 × 0.003 = 0.381 rad`` covers the DEM's
steepest two-cell slopes (the clamp only bites on synthetic input),
and the per-slope error is < 0.3% of :math:`L`. The 4-wide unroll in
``moon_relief_row`` is not a style choice: a sequential per-cell loop
leaves ~12 M PSRAM byte accesses stalling on their own latency —
measured ~4 s for the whole build on the S3; unrolled, the loads
overlap and the build is ~1.3 s.

The row API exists for exactly this: ``moon_relief_row`` processes one
DEM row (three PSRAM rows: north, mid, south), and ``moon_relief_build``
is 1024 calls of it. The cache is **phase-independent** — a pure
function of the DEM — so it is built once at boot and reused for every
frame the device ever bakes.

The per-pixel bake
------------------

**The short version.** This is the 1.3-second loop — the lighting
recipe from :doc:`the lighting model <../science/lighting>` repeated
once per pixel, written for speed rather than for reading. Its shape:
everything a whole row shares is computed once per row (the "row
context"), and inside the pixel loop the minimum survives — a dot
product, the terrain correction, the paint and curve reads, a
rounding, a store — plus a single branch that sends night pixels off
to the dark side's own model.

``bake_px`` (:doc:`the lighting model <../science/lighting>` shows the lit-side steps) is inlined into
``moon_bake_rows`` with a 4-pixel unroll. The row context precomputes
everything the row shares (the ``v`` coordinate, the row fraction, the
albedo row pointer); the per-pixel work is the ``u`` math, the
displaced-normal Lambert, the albedo load, and the LUT:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the hot loop, abridged)

   float d  = rx * g->S[0] + ry * g->S[1] + rz * g->S[2];
   /* tangent dots (:doc:`the lighting model <../science/lighting>`), then the displaced-normal term: */
   float gs = glon * east_s + glat * north_s;
   float lns = (d - gs) / sqrtf(1.0f + glon * glon + glat * glat);
   int is_night = c->dark_zero ? (d < c->dk_sup + MOON_DK_MARGIN) || (lns <= 0.0f)
                               : (lns <= 0.0f);
   if (is_night) {
       /* dark_zero = 1: the computed backlit surface (:doc:`the dark side <../science/darkside>`);
        * dark_zero = 0: the earthshine gradient (:doc:`the lighting model <../science/lighting>`). */
       ...
       return;
   }
   float L = lns > 1.0f ? 1.0f : lns;
   float A = (float)c->alb[r * c->alb_w + col];
   int idx = (int)(A * L + 0.5f);
   ...
   float out = MOON_TONE_LUT[idx] + earthshine(d);
   c->row[x] = (uint8_t)(out + 0.5f);

Reading the loop: ``d`` is the perfect-ball sun cosine — the ball's
answer to "how directly does this point face the Sun"; ``gs`` is the
terrain's correction to that answer, the local slopes folded against
the sun direction; ``lns`` is the honest cosine after the correction,
and ``lns <= 0`` means exactly "this ground faces away from the Sun" —
the night branch. On the lit side: ``L`` clamps the cosine at 1, ``A``
fetches the pixel's paint from the albedo map with a row pointer plus
an offset (no math at all), ``A * L`` indexes the 256-entry tone LUT
that applies the calibrated curve, and ``earthshine(d)`` adds the
faint Earth-reflected glow — built from ``max(0, −d)``, it is zero
for every pixel the Sun is actually above and only lifts the ones the
flat geometry would leave sunless (mountain faces the relief tilts
into the light).

The night-side branch carries both dark-side models (:doc:`the dark side <../science/darkside>`) and the
display-only soft terminator (:doc:`the artistic pass <../science/art-pass>`), selected by
``c->dark_zero`` / ``c->dk_sup`` — the gate's path is byte-identical to
the pre-backlit bake because for ``dark_zero = 0`` the test reduces to
``lns <= 0``.

What the core deliberately does *not* do
-----------------------------------------

**The short version.** The negatives are the portability, stated
negatively. Slow-but-exact arithmetic (doubles, libm) is confined to
the ephemeris, where accuracy earns its cost, and excluded from the
hot path, where it would not. The pixel loops never ask the heap for
memory, so a bake can never stall halfway on an allocator — the
caller owns the destination buffer. And the core has no idea what a
display is; it stops at gray bytes. Together these make the closing
paragraph's promise — *identical bytes on every platform* — a property
of the code rather than a hope.

- **No double precision.** The ephemeris uses doubles internally (the
  ERFA series need it); everything downstream is single precision.
  The gate's residual (:doc:`the image gate <../verification/image-gate>`) bounds the error a single-precision
  terminator position can accumulate: < 1 pixel.
- **No allocation on the bake path.** The caller owns ``frame``; the
  core's only state is the two lazily built tables and the relief
  cache (which the *UI layer*, not the core, decides to build —
  ``moon_relief`` is the one entry point).
- **No color.** The core outputs 8-bit gray. The gray→RGB565 step
  (:doc:`rotation and the LUT <rotate-lut>`) is the only place display color enters, and it is a
  LUT, so the core never knows the panel exists.

The contract that makes the host/device split safe is that
``moon_bake`` is a **pure function of (geometry, albedo, gradient,
parameters)**: same inputs, same bytes, on every platform. The gate
(:doc:`the image gate <../verification/image-gate>`) exploits exactly that property — it compares the C core's
bytes against the Python prototype's bytes, and against NASA's frames,
and both comparisons hold because the pipeline is the same math, not
two implementations of it.

Next: the two remaining core pieces the device path needs — the
fixed-point rotation gather and the gray→565 LUT (:doc:`rotation and the LUT <rotate-lut>`), and
then how a frame actually gets onto the panel (:doc:`the display pipeline <display>`).
