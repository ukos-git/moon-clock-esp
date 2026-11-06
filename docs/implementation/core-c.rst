The portable C core
===================

This page walks the device-side renderer: the dependency-free C
library in ``components/moon`` that turns a UTC epoch into a 400×400
gray frame. Everything in it compiles unchanged on the host (the
simulator, the gate) and on both device boards (the Waveshare
ESP32-S3-Touch-AMOLED-1.75, "S3", and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-XC, "4C"); the only platform assumptions
are a float FPU and a ``malloc``-like allocator.

The file layout
---------------

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
   void moon_shade_map_build(const moon_geom_t *g, ...);/* the dusk's map */
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

The result fills ``moon_geom_t`` (:doc:`the geometry <../science/geometry>`). Note what it does *not*
do: no trigonometric libm calls on the hot path (the ephemeris itself
uses ``sinf``/``cosf`` in tabled or inline form), no allocation, no
globals. The core is stateless except for two lazily built tables in
``moon_bake.c``.

The lookup tables
------------------

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

The night-side branch carries both dark-side models (:doc:`the dark side <../science/darkside>`) and the
display-only soft terminator (:doc:`the artistic pass <../science/art-pass>`), selected by
``c->dark_zero`` / ``c->dk_sup`` — the gate's path is byte-identical to
the pre-backlit bake because for ``dark_zero = 0`` the test reduces to
``lns <= 0``.

What the core deliberately does *not* do
-----------------------------------------

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
