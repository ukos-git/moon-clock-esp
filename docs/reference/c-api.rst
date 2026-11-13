The C API
=========

The portable core (``components/moon/core``) exposes a small C API —
one header (``components/moon/include/moon.h``), the geometry, the
bake, the rotation, the fixed-point trig, and the tone LUT. It is the
device's interface *and* the host simulator's (the same translation
units build into both, :doc:`the architecture <../implementation/architecture>`), so this page is the reference a
reader uses to build against the core — on the device or on the host.

The design contract
-------------------

Three properties make the core *portable*, and the API is shaped by
them:

- **Pure C, no ESP- or LVGL-dependency.** The core's translation units
  (``moon_astro.c``, ``moon_bake.c``, ``moon_render.c``,
  ``moon_fixp.c`` + the vendored public-domain ERFA) compile into the
  device (Xtensa and RISC-V) and the host (x86) unchanged. No
  ``esp_`` call, no ``lv_`` call, no PSRAM-specific allocator in the
  math path — the *buffers are the caller's* (the core never
  allocates in the bake path, :doc:`the C core <../implementation/core-c>`).
- **float32 in, 8-bit out, Q15.15 for the rotation.** The precision
  contract is explicit at the API boundary: geometry is float32 (the
  ERFA port), the bake writes 8-bit grayscale, the rotation takes
  Q15.15 (int32) trig and writes RGB565. The 256-entry tone LUT and the
  257-entry atan/row-frac tables (:doc:`the C core <../implementation/core-c>`) are the two
  fixed-resolution approximations, and both are *named* in the API
  (``moon_gray565``, the ``moon_sin15``/``moon_cos15`` pair).
- **Row-range variants for progressive display.** Every pixel-passing
  function has a ``*_rows`` sibling (``moon_bake_rows``,
  ``moon_rotate_rows``, ``moon_shade_map_rows``,
  ``moon_reproject_rows``) so the caller can sweep a finished row band
  to the display while the rest is still being computed (the orbit
  bakes rows and the dusk re-projects rows today; the boot sweep was
  the original user, retired 2026-11-13, :doc:`the animation pipelines <../implementation/animations>`).

The geometry
------------

.. code-block:: c
   :caption: moon.h — the geometry

   typedef struct {
       float S[3];         /* Moon->Sun, unit, body frame (pole = +z) */
       float E[3];        /* Moon->geocenter, unit (the visible pole) */
       float right[3];    /* image +x (east left), body frame         */
       float up[3];       /* image +y (celestial north up)            */
       float subsolar_lon; float subsolar_lat;   /* deg (gate/debug) */
       float subearth_lon; float subearth_lat;
       float posangle;    /* deg, east of celestial north */
       float phase_angle_deg;
       float illum;       /* (1 + cos i) / 2 */
       float distance_km;
   } moon_geom_t;

   /* UTC epoch seconds -> body-frame geometry. Cheap ephemeris
    * (eraEpv00 + eraMoon98), validated against NASA SVS 5415:
    * max 0.43 deg subsolar / 0.51 deg subearth / 0.44 deg posangle
    * over the 32-frame gate set. */
   void moon_geometry(int64_t utc_epoch, moon_geom_t *g);

The body-frame vectors (``S``, ``E``, ``right``, ``up``) are the same
basis the Python prototype's ``Geometry`` carries (:doc:`the Python API <python-api>`) — which
is what makes the gate's C-vs-Python comparison like-for-like. The
``subsolar_*`` / ``subearth_*`` / ``posangle`` fields are
*gate/debug-only* (the bake does not read them — it works from the
vectors).

The bake
--------

.. code-block:: c
   :caption: moon.h — the bake (abridged; the full comment is the contract)

   typedef struct {
       const uint16_t *pack;  /* packed glon (low) / glat (high) slope cells */
   } moon_grel_t;

   void moon_bake(const moon_geom_t *g, const uint8_t *albedo,
                  const moon_grel_t *grel, uint8_t *frame, int size,
                  float radius, float cx, float cy, int alb_w, int alb_h,
                  int dark_zero, float dk_sup);

   void moon_bake_rows(..., int y0, int y1, ...);  /* the row-band variant */

The parameters the device always passes (and the two that carry the
*display* decisions):

- ``grel`` — the one-time relief gradient cache (``moon_relief_build``
  output); ``NULL`` = relief-free Lambert (the A/B, :doc:`the lighting model <../science/lighting>`). The
  cache is the perf win (2 PSRAM loads/px instead of 6, :doc:`the C core <../implementation/core-c>`).
- ``alb_w`` / ``alb_h`` — the albedo's dimensions: the device passes
  ``MOON_ALB_W``/``MOON_ALB_H`` (864×432) for the full texture, or a
  smaller SRAM albedo for the fast boot animation.
- ``dark_zero`` — the *display* decision, :doc:`the dark side <../science/darkside>`: ``0`` = the
  phase-independent earthshine gradient (the validated model — the
  gate's and ``pole_test``'s path); ``1`` = the **computed backlit
  night side** (albedo + relief lit from the anti-solar direction at a
  faint level — the "the unilluminated side is fully calculated"
  decision). ``dark_zero = 0`` is byte-identical to the old
  earthshine bake for every pixel (the gate's proof).
- ``dk_sup`` — the *dusk-only* dark-side bound (``0.0f`` for every
  other caller): with ``dark_zero``, the dark side is written for
  ``d < dk_sup`` instead of ``d < 0``, killing the geometrically-lit
  rim sliver that would linger on the dark sleep screen as a bright
  arc (the "white ray", :doc:`the dark side <../science/darkside>`). The sliver is sun-lit, so the
  backlit color clips to pure black there — exactly the old
  flat-floor kill, computed instead of overlaid.

The per-pixel model (``moon_bake.c``): the base-sphere direction
``r = u*right + v*up + w*E`` (the bake's 257-entry atan/row-frac table
pass), the equirect albedo sample (``MOON_TEX_LON_OFFSET = 180°``,
:doc:`the geometry <../science/geometry>`), the displaced-normal Lambert ``L = clip(lns, 0, 1)`` from
the cached gradient (``lns = (d − G·S)/sqrt(1 + |G|²)``, unclipped),
the phase-independent earthshine (``dark_zero = 0``) or the backlit
computed surface (``dark_zero = 1``), then the 256-entry tone LUT.

The constant-projection pair (the dusk)
---------------------------------------

.. code-block:: c
   :caption: moon.h — the dusk's map + re-project (abridged)

   void moon_shade_map_build(const moon_geom_t *g, const uint8_t *albedo,
                             const moon_grel_t *grel, uint8_t *map,
                             int map_w, int map_h, int dark_zero,
                             float dk_sup);
   void moon_shade_map_rows(..., int y0, int y1, ...);

   void moon_reproject(const uint8_t *map, int map_w, int map_h,
                       const moon_geom_t *g, uint8_t *frame, int size,
                       float radius, float cx, float cy);
   void moon_reproject_rows(..., int y0, int y1);

The pair is the dusk's economics (:doc:`the dark side <../science/darkside>` and :doc:`the animation pipelines <../implementation/animations>`): ``moon_shade_map_build``
builds the **pre-shaded body-frame equirect map** once per epoch (the
color = the body direction + the epoch's fixed body-frame sun — the
camera basis never enters the color), and ``moon_reproject`` renders
the map through a *(folded)* camera basis — the orbit/in-plane/pole
rotations applied to ``g`` — as a **pure gather** (no lighting
recompute). A fixed surface point keeps its color across the pole
spin; only the projection rotates. The re-project's dark side is
*correct by construction* (the map carries the whole color, including
the backlit night side; the ``dk_sup`` margin erosion is applied to
the *map* at build time, so a dark pixel can never sample a lit cell —
the old re-project ``dk_sup`` early-return is gone).

The relief cache
----------------

.. code-block:: c
   :caption: moon.h — the gradient cache

   /* Build the relief gradient cache: the DEM's 4-neighbor
    * central-difference slopes, quantized to int8, packed into ONE
    * MOON_DEM_W x MOON_DEM_H uint16 array (glon = longitude/east in
    * the low byte, glat = latitude/north in the high byte). Phase-
    * independent: build once at boot, bake forever. */
   void moon_relief_build(const int8_t *dem, int dem_w, int dem_h,
                          uint16_t *pack);
   void moon_relief_row(const int8_t *row_n, const int8_t *row_m,
                        const int8_t *row_s, int dem_w, int dem_h,
                        int r, uint16_t *pack);

``moon_relief_build`` is the one-time boot cost (~1.3 s on the S3,
:doc:`performance <../verification/performance>`) that makes every later bake cheap. The quantization
(``int8`` slope, the ``MOON_DEM_STEPS = 254`` DEM range) is the
prototype-vs-device floor the gate measures (:doc:`the image gate <../verification/image-gate>`).

The rotation (the spin-era gather)
----------------------------------

.. code-block:: c
   :caption: moon.h — the Q15.15 in-plane rotation

   void moon_rotate(const uint8_t *frame, int frame_size,
                    uint16_t *out, int size, int cx, int cy,
                    int f_cx, int f_cy, int32_t cos15, int32_t sin15);
   void moon_rotate_rows(..., int y0, int y1);

   int32_t moon_sin15(float rad);   /* Q15.15, 1025-entry quarter table */
   int32_t moon_cos15(float rad);

The spin's per-frame cost, measured in the spin era and still the
host tools' cost (:doc:`rotation and the LUT <../implementation/rotate-lut>`): the Q15.15 quarter-symmetric
table (``moon_fixp.c``), the nearest-neighbor gather (1 PSRAM
read/px), the fused ``moon_rotate`` + ``moon_gray565`` LUT (no
separate gray->565 pass). ``cx/cy`` are the *output* centre,
``f_cx/f_cy`` the *frame* centre — the spin integrated theta
time-based and converted it through this pair; ``host_sim`` uses the
same shape.

The tone LUT
------------

.. code-block:: c
   :caption: moon.h — the display tone

   /* Gray 0..255 -> RGB565 (round-to-nearest 256-entry LUT;
    * 1 lookup/px). Warm by default (R x1.038 / B x0.933, calibrated
    * to the NASA lit-region balance). */
   uint16_t moon_gray565(uint8_t g);
   void moon_gray565_set_warm(int on);  /* off = neutral (the gate) */

The warm/neutral switch is the art pass's isolation (:doc:`the artistic pass <../science/art-pass>`): the
gate runs the simulator with the neutral LUT (``--neutral``) so its
metrics validate the *physics*, and the device's default is the warm
tint. ``moon_gray565_set_warm(0)`` is the one-line switch that makes
a build gate-configured.

The constants header
--------------------

``moon_consts.h`` is **generated** (``python -m moon.assets``,
:doc:`the Python API <python-api>`) — ``do not edit``:

.. code-block:: c
   :caption: moon_consts.h (abridged)

   // GENERATED by `python -m moon.assets` from moon/constants.py
   #define MOON_POLE_RA_ICRF   269.839977   /* calibrated IAU_Moon */
   #define MOON_POLE_DEC_ICRF  68.110423
   #define MOON_PRIME_MER_W0   40.017251
   #define MOON_PRIME_MER_RATE 13.17617251
   #define MOON_J2000_JD       2451545.0
   #define MOON_TEX_LON_OFFSET 180.0        /* the LROC albedo offset */
   #define MOON_ALB_W 864   #define MOON_ALB_H 432
   #define MOON_DEM_W 2048  #define MOON_DEM_H 1024
   #define MOON_DEM_STEPS 254
   #define MOON_ES_A 30.455 #define MOON_ES_B -11.733 #define MOON_ES_C -2.615
   static const unsigned char MOON_TONE_LUT[256] = { ... };

The calibration constants (pole + prime meridian) are the
``moon.calibrate`` output (:doc:`the Python API <python-api>`) — the body frame is *fitted*
against NASA SVS 5415, not the IAU 2015 default. The ``MOON_TONE_LUT``
is the calibrated fit (:doc:`the lighting model <../science/lighting>`). A change to any of these is a
*device* change (re-gen the header, re-bake the assets, re-flash, and
the gate must re-run — the standing rule, CLAUDE.md).

Building against the core (host)
--------------------------------

The host build links the *exact* device translation units
(the ``moon_bench`` build is the canonical example):

.. code-block:: bash
   :caption: host_sim/CMakeLists.txt (the core as a source list)

   set(MOON_CORE_SRCS
       ../components/moon/core/moon_astro.c
       ../components/moon/core/moon_bake.c
       ../components/moon/core/moon_render.c
       ../components/moon/core/moon_fixp.c
       # + the vendored ERFA translation units
   )
   add_executable(moon_sim main.c ${MOON_CORE_SRCS})
   add_executable(moon_bench bench.c ${MOON_CORE_SRCS})
   target_link_libraries(moon_sim m)
   target_link_libraries(moon_bench m)

A minimal host caller (the bench's shape):

.. code-block:: c
   :caption: A one-frame bake on the host (abridged)

   moon_geom_t g;
   moon_geometry(strtoll(utc, NULL, 10), &g);

   uint16_t *grel = malloc(MOON_DEM_W * MOON_DEM_H * sizeof(uint16_t));
   moon_relief_build(dem, MOON_DEM_W, MOON_DEM_H, grel);  /* once */

   uint8_t *frame = malloc(size * size);
   moon_bake(&g, albedo, (moon_grel_t){ .pack = grel }, frame, size,
             radius, cx, cy, MOON_ALB_W, MOON_ALB_H,
             /* dark_zero */ 1, /* dk_sup */ 0.0f);

   uint16_t *rgb = malloc(size * size * sizeof(uint16_t));
   moon_rotate(frame, size, rgb, size, cx, cy, cx, cy,
               moon_cos15(theta), moon_sin15(theta));      /* the spin-era gather;
                                                            * host tools today */
   /* rgb is RGB565 (the device bswaps on the S3; the P4 is identity —
    * the display pipeline's byte-order seam). */

What the API deliberately does *not* do
----------------------------------------

The omissions are the design:

- **No allocation in the bake path.** Every buffer is the caller's
  (the device's PSRAM/internal-RAM split is the caller's concern —
  :doc:`the display pipeline <../implementation/display>`). The core's only allocation is the *caller's*
  ``moon_relief_build`` output (the one-time cache).
- **No color, no gamma, no display.** The bake writes 8-bit
  *luminance*; the ``moon_gray565`` LUT is the *one* color step, and
  it is a lookup (not a per-pixel computation). The warm/neutral
  switch is the *only* display knob in the core.
- **No ephemeris upgrade path.** The core is pinned to eraEpv00 +
  eraMoon98 (the validated models). A higher-order ephemeris is a
  *re-validation* (the gate's NASA residual would move) — a project
  decision, not an API option.
- **No row-band *bake* of a rotating frame.** The progressive display
  is the *output*'s (the ``*_rows`` variants sweep the *result*);
  the spin's per-frame gather was whole-frame (the 8 ms budget,
  :doc:`performance <../verification/performance>`).

The device wraps the API in the display pipeline (``moon_panel_draw_bitmap`` + the atomic-frame invariant, :doc:`the architecture <../implementation/architecture>`)
— the core never sees the panel. That separation is why the gate can
run the *device's math* on the host and call it verified: the math
and the display are different layers, and only the math is portable.

The next page is the *build* layer that keeps the core (and the
two boards) building from one tree (:doc:`the build system <build-system>`).
