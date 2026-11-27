The C API
=========

.. brief:
   Reader: a developer building against the Moon core, on the device or the host.
   They already know: C headers and the architecture page.
   After this page they can: find every core function, its fixed-point contract and its asset seam.
   Page type: reference.

The portable core (``components/moon/core``) exposes a small C API —
three headers in ``components/moon/include``: ``moon.h`` (the
geometry, the observer viewport, the bake, the rotation, the
fixed-point trig, and the tone LUT), ``moon_event.h`` (the sky-event classifier — harvest, blue,
super, micro, blood), and ``moon_assets.h`` (the asset backend seam,
device partition vs host array). It is the
device's interface *and* the host simulator's (the same translation
units build into both, :doc:`the architecture <../implementation/architecture>`;
how many times each of them is actually compiled — across the twelve
link images — is priced at :ref:`the sharing ladder
<sharing-ladder-perf>`; the Python prototype these units port
1:1 has the twin sketch at :ref:`the package in one picture
<python-surface>`), so this page is the reference a
reader uses to build against the core — on the device or on the host.

The design contract
-------------------

Three properties make the core *portable*, and the API is shaped by
them:

- **Pure C, no ESP- or LVGL-dependency.** The core's translation units
  (``moon_astro.c``, ``moon_viewport.c``, ``moon_bake.c``,
  ``moon_render.c``, ``moon_fixp.c`` + the vendored public-domain
  ERFA) compile into the
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
  bakes rows today; the dusk re-projected rows and the boot sweep was
  the original user — both retired :commit:`51119200`, :doc:`the animation pipelines <../implementation/animations>`).

.. _c-api-surface:

The surface in one picture
--------------------------

**The short version.** The core exports **23 entries across three
headers** — and the shipped tree calls 17 of them. The family is the
house pattern: one black-box overview, then one detail sketch per
sealed box — the contract census, the device callers in blue
(``moon_ui.c``, ``knob_moon.c``), the host tools in teal — and the six
entries nobody outside the core ever calls drawn as dashed islands in
the census: the four internal ``*_rows`` helpers, the dusk pair's
retired siblings, and ``moon_deinit``, defined in both backends and
called by no one.

.. graphviz:: ../_graphs/api-surface.dot
   :align: center
   :alt: Five sealed boxes — THE DEVICE CALLERS and THE HOST TOOLS side by side above THE CONTRACT (three headers, 24 entries), which hangs over THE DEFINITIONS and the amber THE ASSET SEAM box — joined by blue, teal and dashed legs.
   :caption: The core's public surface as the family actually uses it — the black-box pass, no internals. Blue edges are calls a shipped frame makes; teal edges belong to a host tool; amber prices the day-scan guard; dashed legs are retired (the dusk pair's whole-frame variants live only in ``bench.c``), internal (the four ``*_rows`` helpers), or dead (``moon_deinit``) — the rotation's dashed era (the spin's removal :commit:`79937e37`) ended :commit:`d177a8fb`, when the observer-view gather brought it back into every shipped frame. Every edge was read out of the source before drawing — comment-stripped, so the ``moon_tfcache.c`` "gray565" mention is a comment at :18, not a call. The boxes open below; the three claims that follow are checkable at the cited line.

**The contract** opens into the census — the three headers, their 24
entries, and where each definition lives:

.. graphviz:: ../_graphs/api-surface-headers.dot
   :align: center
   :alt: Three amber header nodes (moon.h 17 entries, moon_event.h 4, moon_assets.h 3) fanning grey right to twelve entry boxes — geometry, the bake pair, the relief builder, the tone trio, the viewport, the rotation, the dashed rows siblings, the dashed dusk pair, the event classifier, the dashed deinit, and the two backend boxes.
   :caption: ``moon.h`` — 17 entries (:60-:277): float32 in, 8-bit out, Q15.15 for the rotation, and every pixel-passing function ships a ``*_rows`` sibling. ``moon_event.h`` — 4 entries (classify :119; ``any``/``signature``/``occ`` are static INLINE at :124/:132/:143 — "the bake hot path and the host tools share it without a link dependency" :100-102). ``moon_assets.h`` — 3 entries (:23/:29/:32): one seam, two backends, and the bake's caller never sees who answers. The definitions sit behind them: geometry at ``moon_astro.c``:80 (the cheap ``eraEpv00`` + ``eraMoon98`` ephemeris); the bake family in ``moon_bake.c`` (``bake`` :572 delegating the full square to ``bake_rows(0,size)`` at :574; ``relief_build`` :317 calling ``relief_row`` per row at :322; ``shade_map_build`` :773 and ``reproject`` :903 wrapping their rows at :776/:905; the blood occlusion via inline ``occ`` at :450/:735); the tone trio + rotation in ``moon_render.c`` (``gray565`` :124, ``set_warm`` :71, ``set_event`` :88 — ``set_warm(0)`` wins over ``set_event`` :29-31; the first rotate call builds the LUT lazily at :167 and delegates to ``moon_rotate_range``); the trig pair at ``moon_fixp.c``:35/:52; the viewport at ``moon_viewport.c``:60 (ERA + ``eraMoon98`` + the topocentric subtraction); classify at ``moon_event.c``:221. The dashed islands are the third claim below.

**The device callers** — blue edges are paths a shipped frame took;
amber prices the guard:

.. graphviz:: ../_graphs/api-surface-device.dot
   :align: center
   :alt: moon_ui.c and knob_moon.c on the left, the amber classify_here guard beside the clock, fanning blue to the entry boxes — geometry, whole bake, bake_rows, the tone trio, the viewport, the rotation, classify, the amber device backend.
   :caption: The first claim's line numbers beyond the bullets below: ``bake_rows``' two sites are the hourly bounce :1493 and the orbit rows :2349, both ``dark_zero`` 1; ``set_event`` runs four times on the clock (producers :1742/:1859/:2222 plus the replay's ``set_event(NULL)`` :2148); the seam reads sit at :1726/:1731 with the eager warmup at :2863 (the "moon assets unavailable" guard). The knob file is the same shape compressed — its event trio runs ``any`` :119 and ``signature`` :85/:95 alongside classify :82/:94, its seam reads are :55/:56, and its row bakes are ``dark_zero`` 1 like the clock's. Only the clock calls ``moon_rotate`` (the observer gather, :1426) — the knob keeps its fixed north-up viewport; neither device file calls ``moon_gray565_set_warm`` — warm is the shipped default and the knob's identity gather is by construction.

**The host tools** — teal edges; the gate and the bench link the EXACT
core source list (the sharing ladder checks the set equality):

.. graphviz:: ../_graphs/api-surface-host.dot
   :align: center
   :alt: Four host boxes (main.c, bench.c, uisim/stubs.c, the dashed never-linked probes) fanning teal to the entry boxes — geometry, bake, the rotation, the viewport, the tone trio, classify, the dashed dusk pair, the amber host backend.
   :caption: ``main.c`` drives the gate's posture (``bake`` :168 with ``dark_zero`` 0 / ``dk_sup`` 0.0, ``set_warm(0)`` :219 — the ``--neutral`` stance, classify :161, seam :134/:135); ``bench.c`` runs the A/B timing (``set_warm(0)`` :68, ``bake`` :111, ``rotate`` :119) and is the dusk pair's ONLY caller (:125/:129); ``uisim/stubs.c`` is the sim's whole home moon — geometry :457, bake :458 with the seam inline, then the observer gather mirrored from the device leg (``moon_viewport_rot_deg`` :466 behind ``clock_tz_observer``, ``moon_rotate`` :470, else the plain LUT ``gray565`` :475; boot applies the stored zone first, :454) — the file header calls it "bakes the REAL current-phase moon … ``dark_zero`` = 1 like the device static/wake frame". The third probe, ``viewport_test.c``, drives only ``moon_viewport_rot_deg`` (:110) against its astropy golden table.

Three claims the picture makes, each checkable at the cited line:

- **The device owns the bake — and, since :commit:`d177a8fb`, the rotation
  again.** ``moon_ui.c`` drives the geometry at six sites (:1722 boot,
  :1890 hourly producer, :2226 replay final, :2332 orbit, plus the
  new-moon scan :2066/:2081), the whole bake at four (:1750 boot,
  :1913 the hourly monolithic path under reveal-suppression,
  :2244/:2248 the replay final's two branches), ``moon_bake_rows`` at
  two (the hourly bounce :1493, the orbit rows :2349) — and the tone
  dictionary is down to **two** sites: the single ``s_rgb`` build leg
  ``moon_fill_rgb_from_frame`` (:1420), whose **eight** callers are
  reveal restore :1444, tick backstop heal :1568, the canonical
  ``moon_convert_to_rgb`` :1664, the hourly publish :1929, the replay
  mids at their hour centres :2198, the replay final at ``now`` :2261,
  the orbit publish :2371 and the tz re-gather :2999 — internally the
  exact LUT (:1430) or the observer gather (``moon_rotate`` :1426 at
  the ``moon_viewport_rot_deg`` q, :1406); the one convert outside it
  is the reveal band (:1607). The knob file is the same shape
  compressed: geometry :118, ``bake_rows`` row-by-row :128/:145,
  ``gray565`` per row :133/:157 — no rotation, no viewport (fixed
  north-up). ``moon_gray565_set_warm`` is never called on a device —
  warm is the shipped default and the knob's identity gather is "by
  construction, not hope" (``knob_moon.c``:9-13).
- **Classify never runs on the LVGL task.** ``moon_event_classify``'s
  only device callers are ``knob_moon.c`` :82/:94 and, on the clock,
  the static wrapper ``moon_event_classify_here`` (:1280), which
  routes a cold day's 60-day peak scan to the core-1 worker (:1270,
  day tick :3073) and takes the cached path inline when the day is
  warm (:1294) — the freeze this guard prevents is the day-scan story
  in :doc:`the animation pipelines <../implementation/animations>`
  ("Who pays for the day scan").
- **Six entries are exported for a caller that no longer exists (or
  never did).** ``moon_rotate_rows``, ``moon_shade_map_rows``,
  ``moon_reproject_rows`` and ``moon_relief_row`` are wrapped by their
  whole-frame siblings inside the core (:776, :905, :322) and have zero
  external callers; ``moon_shade_map_build``/``moon_reproject`` survive
  only as ``bench.c``:125/:129's A/B subjects; ``moon_deinit`` is
  defined twice (``moon_esp_assets.c``:138, ``moon_host_assets.c``:44
  — "the cache lives for the simulator's process") and called nowhere.
  They stay exported because the host tools' build census links the
  exact core list — :doc:`the sharing ladder
  <../verification/performance>` prices every file here by how many of
  the twelve link images compile it.

The throwaway probes (``dark_test.c``, ``newmoon_test.c``, the tz wave's
``viewport_test.c`` and the compute-progress wave's ``phase_dummy_test.c``
— "Throwaway ... Not part of the gate") hang dashed off the sketch too:
tracked in git, hand-typed ``cc`` lines in their headers, linked by
nobody — ``host_sim/CMakeLists.txt`` builds exactly ``moon_sim`` +
``moon_bench``.
The bake's per-pixel model is :doc:`the core flow sketch
<../implementation/core-c>`; the gray→RGB565 dictionary and the
rotation's last mile are :doc:`rotate-lut
<../implementation/rotate-lut>`; the dark-side branch is
:doc:`darkside <../science/darkside>`.

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
       float shadow_dir[3];   /* eclipse shadow axis, body frame: the
                               * geocentric ANTI-solar unit direction
                               * (the umbra cone's axis) — moon_event.h */
       float shadow_off[3];   /* the closest point of that axis to the
                               * Moon's center (perp. to shadow_dir), m */
       float off_ax;          /* the point's axial coordinate, meters
                               * from the EARTH (see moon_event.h) */
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
vectors). The three ``shadow_*`` fields locate the Earth's shadow in
the body frame; ``moon_event.c``'s classifier turns them into the
per-pixel umbra/penumbra occlusion of a blood moon (the sky events,
below).

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
                  int dark_zero, float dk_sup,
                  const moon_bake_ev_t *bev);

   void moon_bake_rows(..., int y0, int y1, ..., const moon_bake_ev_t *bev);
                                          /* the row-band variant */

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
  gate's path); ``1`` = the **computed backlit
  night side** (albedo + relief lit from the anti-solar direction at a
  faint level — the "the unilluminated side is fully calculated"
  decision). ``dark_zero = 0`` is byte-identical to the old
  earthshine bake for every pixel (the gate's proof).
- ``dk_sup`` — the *dusk-only* dark-side bound: retired with the dusk
  :commit:`f7740500`, the parameter kept in the portable core (shared verbatim
  with the host tools) — every device bake passes ``0.0f`` today.
  With ``dark_zero``, the dark side was written for
  ``d < dk_sup`` instead of ``d < 0``, killing the geometrically-lit
  rim sliver that would have lingered on the dark sleep screen as a bright
  arc (the "white ray", :doc:`the dark side <../science/darkside>`). The sliver is sun-lit, so the
  backlit color clips to pure black there — exactly the old
  flat-floor kill, computed instead of overlaid.
- ``bev`` — the pre-calculated *sky event* (``moon_event.h``, below;
  ``NULL`` = none): when ``bev->blood`` the lit Lambert is multiplied
  per pixel by ``1 − 0.85 × the pixel's umbra occlusion``
  (``MOON_EV_BLOOD_K``) BEFORE the tone LUT, so the eclipse "bite"
  lands in the right place on the disk and a total eclipse still
  shows a faint copper disk, not a black hole. ``NULL`` or an
  all-false event is byte-identical to the pre-event bake (the
  gate's posture).

The per-pixel model (``moon_bake.c``): the base-sphere direction
``r = u*right + v*up + w*E`` (the bake's 257-entry atan/row-frac table
pass), the equirect albedo sample (``MOON_TEX_LON_OFFSET = 180°``,
:doc:`the geometry <../science/geometry>`), the displaced-normal Lambert ``L = clip(lns, 0, 1)`` from
the cached gradient (``lns = (d − G·S)/sqrt(1 + |G|²)``, unclipped),
the phase-independent earthshine (``dark_zero = 0``) or the backlit
computed surface (``dark_zero = 1``), then the 256-entry tone LUT.

The constant-projection pair (the dusk — host-only since :commit:`f7740500`)
----------------------------------------------------------------------------

.. code-block:: c
   :caption: moon.h — the dusk's map + re-project (abridged)

   void moon_shade_map_build(const moon_geom_t *g, const uint8_t *albedo,
                             const moon_grel_t *grel, uint8_t *map,
                             uint8_t *map_dark, int map_w, int map_h,
                             int dark_zero, float dk_sup,
                             const moon_bake_ev_t *bev);
   void moon_shade_map_rows(..., int y0, int y1, ...);

   void moon_reproject(const uint8_t *map, int map_w, int map_h,
                       const moon_geom_t *g, uint8_t *frame, int size,
                       float radius, float cx, float cy);
   void moon_reproject_rows(const uint8_t *map, const uint8_t *map_dark,
                            int map_w, int map_h, const moon_geom_t *g,
                            uint8_t *frame, int size, float radius,
                            float cx, float cy, int y0, int y1,
                            float lit_w);

The pair was the dusk's economics (:doc:`the dark side <../science/darkside>` and :doc:`the animation pipelines <../implementation/animations>`): ``moon_shade_map_build``
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
the old re-project ``dk_sup`` early-return is gone). The companion
``map_dark`` is the lit-fade variant: each pixel lerps
``lit * lit_w + dark * (1 − lit_w)`` of the two nearest-neighbor
samples — the companion is identical to the lit map in the backlit
cells, so the lerp moves exactly the lit cells (the "earth ray" rim
arc eases to black over a spin). The plain gather is
``map_dark = NULL`` / ``lit_w`` ignored — exact, no rounding.

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
                        int r, uint16_t *gpack);

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

The observer viewport (the parallactic tilt)
--------------------------------------------

.. code-block:: c
   :caption: moon.h — the timezone's disk tilt

   float moon_viewport_rot_deg(int64_t utc_epoch,
                               float lat_deg, float lon_deg);

Pure C + the same vendored ``eraMoon98`` as the geometry: the Moon's
**parallactic angle** in degrees (−180..180, east of north) at the
UTC instant for an observer at (lat, lon) — the angle the UI's
``moon_rotate`` gather turns the north-up baked frame by, so the
disk shows as that city sees it. The sidereal reference is the
IAU-2000 Earth Rotation Angle (eraMoon98 answers in ICRF — GMST
would silently add ~0.34° of precession), and the Moon direction is
**topocentric** (the observer's geocentric vector subtracted;
spherical Earth, R = 6371 km) so the lunar parallax rides along.
Golden-tested against astropy 8 in ``host_sim/viewport_test.c``
(16/16, max \|Δ\| 0.29°). The core is display-agnostic about the
image's rotation direction — that is the UI's sign knob. The bake
stays north-up and the TF hour cache zone-agnostic: the gather is
the only tilt path (:doc:`the observer view <../implementation/observer-view>`).

The tone LUT
------------

.. code-block:: c
   :caption: moon.h — the display tone

   /* Gray 0..255 -> RGB565 (round-to-nearest 256-entry LUT;
    * 1 lookup/px). Warm by default (R x1.038 / B x0.933, calibrated
    * to the NASA lit-region balance). */
   uint16_t moon_gray565(uint8_t g);
   void moon_gray565_set_warm(int on);  /* off = neutral (the gate) */
   void moon_gray565_set_event(const moon_event_t *ev);
   /* the sky-event tint; the LUT is the COMPOSITE of every flag set
    * (harvest + super, ...), so one call per day covers the instant.
    * The neutral gate LUT is never tinted — set_warm(0) wins. */

The warm/neutral switch is the art pass's isolation (:doc:`the artistic pass <../science/art-pass>`): the
gate runs the simulator with the neutral LUT (``--neutral``) so its
metrics validate the *physics*, and the device's default is the warm
tint. ``moon_gray565_set_warm(0)`` is the one-line switch that makes
a build gate-configured.

.. _sky-events-capi:

The sky events (moon_event.h)
-----------------------------

.. code-block:: c
   :caption: moon_event.h — the classifier (abridged; the full comment is the rule set)

   typedef struct {
       bool blood;        /* lunar eclipse: Moon center inside the
                             Earth's shadow cone at the epoch */
       bool harvest;      /* full moon whose civil date falls in
                             Sep 22 .. Oct 23 (the closest full moon
                             to the autumn equinox — civil rule) */
       bool blue;         /* 2nd full moon of its civil month (the
                             PREVIOUS full moon shares the month —
                             exact, a month holds at most two) */
       bool super_moon;   /* full moon, geocentric < 357,500 km */
       bool micro_moon;   /* full moon, geocentric > 405,500 km */
       float shadow_dir[3], shadow_off[3], off_ax;
                          /* umbra axis (body frame, meters) — the
                             moon_geom_t fields; valid when blood */
   } moon_event_t;
   typedef moon_event_t moon_bake_ev_t;  /* the bake's 14th argument */

   void  moon_event_classify(int64_t utc_epoch, moon_event_t *ev);
   bool  moon_event_any(const moon_event_t *ev);        /* any flag set */
   int   moon_event_signature(const moon_event_t *ev);  /* 5-bit OR */
   float moon_event_occ(const moon_event_t *ev, const float d[3]);
                                                        /* 0..1 occlusion */

The rules are pinned by a 60-year census — **all 742 full moons of
2000..2060**, geocentric envelope 356,431 .. 406,528 km: the
super/micro bounds sit ~1,000 km *inside* the extremes and fire 60 /
61 times (~once a year each), where the naive perigee/apogee values
would fire at most once in 60 years. "Full moon" is the **moment
test** \|epoch − F\| ≤ 18 h around the exact opposition F (the argmax
of S·E) — a literal S·E > 0.999 dot test loses every full moon whose
ecliptic latitude exceeds 2.56°, roughly half of all of them. The
classification is cheap: a handful of ephemeris evaluations plus the
60-day S·E peak search cached *per calendar day*. The header also
carries the shadow constants (``MOON_EV_EARTH_R_M`` 6,371 km,
``MOON_EV_SUN_R_M`` 695,700 km, ``MOON_EV_AU_M``,
``MOON_EV_UMBRA_TIP_M``, ``MOON_EV_PENUMBRA_SLOPE``,
``MOON_EV_BLOOD_K`` 0.85) — at the mean lunar distance they give the
eclipse figures every almanac prints: umbra ~4,580 km, penumbra
~8,175 km. ``moon_event_occ`` is header-inline (the bake hot path and
the host tools share it without a link dependency): 1 fully inside
the umbra, 0 outside the penumbra, a linear ramp between the cone
edges — the partial-eclipse "bite". The device's day-tick compares
``moon_event_signature`` against the last baked signature so an event
STARTS or ENDS *mid-day* (a lunar eclipse happens at the hour it
happens, not at the UTC midnight the date-change tick sees);
``moon_gray565_set_event`` carries the LUT tint; the 2025-03-14 total
eclipse is the frozen test date (:doc:`the image gate <../verification/image-gate>`).

The asset seam (moon_assets.h)
------------------------------

.. code-block:: c
   :caption: moon_assets.h — one seam, two backends

   const uint8_t *moon_albedo(void);     /* MOON_ALB_W x MOON_ALB_H
                                            equirect u8, row 0 = north,
                                            col 0 = longitude 0 */
   const moon_grel_t *moon_relief(void); /* the relief gradient cache;
                                            NULL = partition missing /
                                            too small, or PSRAM alloc
                                            failed */
   void moon_deinit(void);               /* device: release; host: no-op */

The bake's caller never sees the backends: the **device** reads the
whole int8 DEM from the ``moon_assets`` partition in ONE
``esp_partition_read`` into a transient PSRAM buffer, builds the
cache, and frees the buffer — the DEM is never kept resident, so
steady-state relief memory is the one 4 MB uint16 array, which is
what lets the cache fit the 8 MB pool alongside albedo, ``s_frame``,
``s_rgb`` and LVGL (:doc:`the display pipeline <../implementation/display>`); the **host** links the
generated C arrays (``moon_assets_gen.c``, :doc:`the Python API <python-api>`).

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
             /* dark_zero */ 1, /* dk_sup */ 0.0f, /* bev */ NULL);

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

The next page is the *build* layer that keeps the core building
everywhere — the three boards from one tree, the knob an esp32s3
target of the same project that compiles the same shared components
(:doc:`the build system <build-system>`).
