The Python API
==============

The Python package at ``assets/images/moon/moon/`` is the
*validated reference* the whole project is built on: the gate (:doc:`the image gate <../verification/image-gate>`)
image-diffs the C core against its frames, the C core's math ports it 1:1
(:doc:`the C core <../implementation/core-c>`), and the asset pipeline that bakes the device's albedo/DEM
lives in it. This page is its API reference — the package layout,
each module's public surface, and runnable examples, with the honest
note on where the prototype and the device path differ.

Layout
------

.. code-block:: text
   :caption: assets/images/moon/

   moon/
     __init__.py    lazy package (import the module you need)
     constants.py   the single source of truth for every constant
     astro.py       UTC -> body-frame geometry (IAU_Moon, PyERFA)
     sphere.py      unit-sphere sampling + equirect lookup (vectorized)
     renderer.py    the per-pixel displaced-sphere shader
     animation.py   the synodic-month sweep (frames + mp4)
     textures.py    LROC albedo / LOLA height loading (16-bit TIFF)
     calibrate.py   the body-frame pole + prime-meridian fit
     register.py    pixel registration to the NASA reference crops
     metrics.py     MAE / MSE / SSIM (8-bit luminance)
     validate.py    the NASA SVS 5415 correctness gate (the prototype)
     assets.py      the device asset derivation (albedo + int8 DEM +
                    the C header the device compiles)

The package is lazy: ``moon/__init__.py`` carries no imports, and the
entry points are the modules themselves (``python -m moon.calibrate``,
``python -m moon.animation``, ``python -m moon.assets``).

.. _python-surface:

The package in one picture
--------------------------

**The short version.** The package is twelve modules and the repo has
exactly **five tracked importers**. ``constants.py`` is the hub (the
one thing every validation path reaches), ``assets.py`` is both the
widest importer — seven modules — and the only module whose output
the devices carry, and a sixth and seventh tool
(``render_demo.py``, ``gen_media.py``) deliberately never import any
of it: they shell out to the compiled binaries instead. The sketch
draws every import edge, every artifact and every consumer with its
line numbers.

.. graphviz:: ../_graphs/python-surface.dot
   :align: center
   :alt: Twelve module nodes with intra-package import arrows pointing at constants, astro, sphere, textures, renderer, register and metrics; a dashed third-party cluster (astropy, numpy, tifffile, PIL, skimage, scipy, the subprocessed ffmpeg, the never-imported imageio); an artifacts cluster (the per-target blob and manifest, the generated moon_consts.h, the fitted LUT, the sweep mp4); and the consumer cluster — check_gate.py importing six modules, perf_measure.py four, the three docs figure tools, and the dashed pair that shells out instead.
   :caption: The Python side as the repo actually uses it. Arrows inside the package read "imports" (every ``from .X`` line comment-stripped first); blue edges are imports a shipped gate, number or figure depends on; dashed legs are the never-imported pair (``render_demo.py`` runs the ``moon_sim`` binary, ``gen_media.py`` stitches uisim PNGs with ffmpeg) and the transitive ``imageio`` pin. ``assets.py`` is both the widest importer (seven modules) and the only module whose output the devices carry. The C half of the same interface is :ref:`the surface in one picture <c-api-surface>`; the gate's dataflow is image-gate.dot; the pipeline flow is assets-pipeline.dot.

Dependencies
------------

Six things are imported directly: ``astropy`` (PyERFA ephemeris +
units), ``numpy``, ``tifffile`` (the 16-bit LROC albedo), ``Pillow``
(PNG output), ``scikit-image`` (the SSIM implementation and
``register``'s phase cross-correlation) and ``scipy`` (``ndimage``:
the shift in ``register``, the ``uniform_filter1d`` in ``validate``)
— plus ``ffmpeg``, shelled out to by ``animation.make_mp4`` (libx264,
yuv420p, crf 20) rather than imported. ``scikit-image`` drags
``imageio`` into the pin list, but no module of the package imports
it. The full pin list is the package's ``requirements.txt``;
``matplotlib``-free (PIL/ffmpeg for the mp4). The package's own venv is
``assets/images/moon/.venv``; ``host_sim/.venv`` runs the gate, which
imports the same tree.

constants — the single source of truth
---------------------------------------

``moon.constants`` is the module every other module imports its
numbers from, and the one the C port mirrors constant-for-constant.
The public names (values as of the current tree):

.. list-table::
   :header-rows: 1
   :widths: 30 16 54

   * - Name
     - Value
     - Meaning
   * - ``POLE_RA_ICRF`` / ``POLE_DEC_ICRF``
     - 269.839977 / 68.110423 deg
     - the calibrated IAU_Moon body-frame pole (the ``calibrate``
       output against NASA SVS 5415 — not the IAU 2015 default)
   * - ``PRIME_MER_W0`` / ``J2000_JD``
     - 40.017251 / 2451545.0
     - the fitted prime-meridian offset at the J2000 epoch (deg) /
       the epoch the rate is referred to
   * - ``PRIME_MER_RATE_DEG_DAY``
     - 13.17617251
     - the prime-meridian rotation rate (deg/day)
   * - ``IMAGE_SIZE`` / ``DISK_RADIUS_PX``
     - 400 / 200.0
     - the reference resolution (the gate's and the asset's disk)
   * - ``RELIEF_SCALE``
     - 1.0
     - the DEM height scale (m per DEM unit, after the km->m load)
   * - ``TERMINATOR_BAND_RAD``
     - 0.02
     - the legacy band-only blend radius (the old model; kept for
       A/B, :doc:`the lighting model <../science/lighting>`)
   * - ``POSANGLE_SIGN``
     - 1.0
     - the posangle sign convention
   * - ``TEXTURE_LON_OFFSET_DEG``
     - 180.0
     - the LROC albedo's 180° longitude offset (a gate-caught bug; the C
       core carries the same offset)
   * - ``EARTHSHINE_A/B/C``
     - 30.455 / -11.733 / -2.615
     - the cubic earthshine fit ``A·x + B·x² + C·x³`` in the dark-side
       disk fraction
   * - ``TONE_LUT``
     - (256,)
     - the calibrated gray->gray tone curve (the fit, :doc:`the lighting model <../science/lighting>`)
   * - ``GEO_SUBSOLAR_TOL_DEG`` etc.
     - 0.6 / 0.6 / 2.0
     - the geometry tolerances vs NASA's published positions
   * - ``MAE_MAX`` / ``SSIM_MIN``
     - 12.0 / 0.89
     - the prototype gate's thresholds (:doc:`the image gate <../verification/image-gate>`'s C-vs-NASA 12
       comes from here)
   * - ``NUM_INTERMEDIATE_FRAMES``
     - 24
     - the synodic-month sweep's per-month sub-sample
   * - ``EPHEMERIS``
     - ``"builtin"``
     - PyERFA's ``builtin`` ephemeris backend — the same low-order
       models the C core vendors (eraEpv00 + eraMoon98)

astro — the geometry
--------------------

``moon.astro`` turns a UTC instant into the calibrated body-frame
geometry. The public surface:

.. code-block:: python
   :caption: moon.astro public API

   class Geometry:      # the epoch's geometry (dataclass)
       utc                      # the ISO string passed in
       subsolar_lon, subsolar_lat   # deg (body frame)
       subearth_lon, subearth_lat
       posangle                 # deg, east of celestial north
       phase_angle_deg, illum   # (1 + cos i) / 2
       distance_km
       S_body, E_body           # Moon->Sun / Moon->geocenter, unit
       right_body, up_body      # the camera basis — all body frame
       def to_dict(self) -> dict  # the scalar fields (no vectors)

   def geometry(utc: str, ephemeris: str | None = None) -> Geometry
       # "2026-04-15T12:00" -> the epoch's geometry (PyERFA, the
       # ephemeris backend default C.EPHEMERIS)
   def pole_vector() -> np.ndarray
   def prime_meridian(jd: float) -> float
   def body_basis(P, W_deg):        # the ICRF -> body-frame basis
   def body_lonlat(v, P, W_deg)     # body vector -> (lon, lat) deg

The verified example (``host_sim/.venv/bin/python``, the benchmark's
epoch):

.. code-block:: python
   :caption: One frame's geometry, run 2026-11-15

   >>> import sys; sys.path.insert(0, 'assets/images/moon')
   >>> from moon.astro import geometry
   >>> g = geometry('2026-04-15T12:00')
   >>> g.illum, g.phase_angle_deg
   (0.05396610789642542, 153.13...)   # the waxing crescent at the benchmark epoch
   >>> g.subsolar_lon, g.subsolar_lat   # (-159.25, 0.60) deg
   >>> g.subearth_lon, g.subearth_lat   # (-6.10, -1.71) deg
   >>> g.posangle, g.distance_km        # (338.11, 372443)

The ``Geometry`` carries the *body-frame* vectors (``S_body``,
``E_body``, ``right_body``, ``up_body``) — the same basis the C core's ``moon_geom_t``
carries (:doc:`the C core <../implementation/core-c>`), which is what makes the C-vs-Python gate a
*like-for-like* comparison (the two implementations share the
subsolar offset; the gate's 1-gray C-Python floor is the port alone).

sphere — the sampling
---------------------

``moon.sphere`` is the vectorized sampling the renderer uses:

.. code-block:: python

   def disk_grid(size, radius_px, cx, cy)
       # (yy, xx, r_unit): the base-sphere direction per pixel (the
       # bake's r = u*right + v*up + w*E, vectorized)
   def equirect_bilinear(sample_map, lon_deg, lat_deg)
       # the equirect texture/height lookup (bilinear, wrapped)
   def tangent_normal(r, h_slope_u, h_slope_v, ...)
       # the displaced-sphere normal from the relief slopes

renderer — the shader
----------------------

``moon.renderer`` is the per-pixel model (the C core's exact math,
vectorized):

.. code-block:: python

   def render(g, albedo, height, size=400, radius=199.3, cx=199.3,
              cy=199.5, band_only=False, band_rad=0.02, gain=1.0,
              relief_scale=1.0) -> tuple[raw, dterm]
       # the full shader: the base direction -> the (theta, row_frac)
       # table pass -> the equirect albedo/height sample -> the
       # displaced-normal sun dot -> the raw (0..255) + dterm.
       # band_only=True = the legacy terminator-band model (the A/B).
   def to_uint8(img, max_val=255.0)
   def earthshine(d)          # the cubic dark-side fit (A/B/C)
   def tonemap(raw, dterm, lut)
   def save_png(img, path, alpha=None)
   def reference_alpha(size=400, radius=199.3, cx=199.3, cy=199.5)
       # the disk mask (the alpha channel / the 0-outside convention)

animation — the sweep
----------------------

``moon.animation`` is the host synodic-month sweep model — the renderer
that filmed the device's boot sweep (``docs/gen_media.py``'s
``boot_sweep.mp4``) before that subsystem's retirement 2026-11-13; the
film and the device sweep are gone, the host module stays runnable
standalone with ``python -m moon.animation``
(:doc:`the animation pipelines <../implementation/animations>`):

.. code-block:: python

   SYNODIC_DAYS = 29.530589
   def frame_times(end_utc, n) -> list[str]
       # the n sub-sample times ending at end_utc (the sweep's phases)
   def render_frame(g, alb, hgt, lut, size=400, radius=199.3,
                    cx=199.3, cy=199.5)
       # one frame: geometry -> raw -> tone LUT -> earthshine -> uint8
   def make_frames(end_utc, n, out_dir) -> list[str]
       # render n phase frames (PNG) to out_dir (the demo's source)
   def make_mp4(paths, out_dir, fps=24) -> str | None
   def main()                  # python -m moon.animation

textures — the 16-bit load
--------------------------

``moon.textures`` loads the LROC albedo and the LOLA height. The
non-obvious fact (:doc:`the geometry <../science/geometry>`): the LROC albedo **must be loaded
16-bit** (``tifffile``) — the 8-bit downcast loses the terminator's
low levels before the tone curve sees them:

.. code-block:: python

   def albedo_path() / height_path() -> str
   def load_albedo(path=None) -> np.ndarray   # float64, sRGB-linear
   def load_height(path=None) -> np.ndarray   # meters (km -> m)
   def load_textures(alb_path=None, hgt_path=None) -> (alb, hgt)

validate — the prototype gate
-----------------------------

``moon.validate`` is the *prototype's* NASA gate (the one the C gate
in :doc:`the image gate <../verification/image-gate>` image-diffs against): it selects frames from NASA SVS
5415's ``mooninfo_2025.json``, checks the geometry against the
published positions (the tolerance constants), fits the tone LUT, and
runs the per-frame MAE/SSIM. ``moon.calibrate`` is the one-time body
frame fit (pole + prime meridian) that produced the ``constants``
values — it *prints* the fit (``calibrate.py:231-240``) and a human
pastes it into ``constants.py``; neither it nor ``validate`` (whose
LUT print says "paste into constants.py as TONE_LUT",
``validate.py:170``) ever rewrites its own source.
``moon.register`` is the pixel-registration fallback (a rendered
crop's shift to the NASA crop) — the gate's pixel-aligned convention
makes it a no-op in the normal path; in-tree it has two consumers,
``host_sim/check_gate.py:58`` (``register_shift``, before the C-vs-Python
image diff) and ``docs/nasa_figs.py:35`` (the NASA-comparison figure
maker, which registers the simulator's crop onto NASA's before drawing
the strip) — beside its two in-package callers, ``validate`` and
``assets`` themselves.

assets — the device derivation
------------------------------

``moon.assets`` is the module that *produces the device's flash
content* (the per-board ``moon_assets`` blob — albedo then DEM,
2,470,400 B S3 / 4,194,304 B P4 / 897,536 B knob, :doc:`the binary-size chronology <../verification/binary>`):

.. code-block:: python

   DEM_W, DEM_H = 2048, 1024    # the DEFAULT int8 relief DEM size —
                                # per-board sizes are constants.TARGETS[t]
                                # ("alb" / "dem", e.g. knob 1024x512)
   DEM_STEPS = 254              # the quantization range (-128..126)
   def bake_albedo(full, width, height)   # full-res -> equirect u8
                                # (BOTH sizes required — the per-board
                                # TARGETS[t]["alb"], row 0 = north)
   def bake_dem(h_full, width, height)    # full-res height -> int8
   def relief_lns(r_body, g, dem, ...)     # the DEM relief-normal sun dot
   def render_from_assets(g, alb, dem=None, ...)
       # one frame FROM THE ASSET MAPS with the exact device lighting
       # (full-disk displaced-normal Lambert, L = clip(lns, 0, 1))
   def gen_consts_header(out_path, dem_lo, dem_hi, alb_w, alb_h,
                         dem_w=DEM_W, dem_h=DEM_H)
       # the moon_consts.h the device compiles (the calibrated
       # pole/prime-meridian + the DEM range + the asset sizes)
   def gen_assets_c(out_path, alb, dem)
       # the C array the device flash-bakes (the moon_assets blob)

The module is also the pipeline entry point —
``python -m moon.assets --target s3 p4 knob`` (the default is all
targets). Its surface beyond ``--target``: ``--out`` (root dir — each
target writes ``<root>/<target>/``), ``--alb WxH`` (override the
albedo size for the selection — equirect 2:1, even W ≤ 4096),
``--json`` (the NASA ``mooninfo_2025.json``), ``--roundtrip``
(re-parse the produced blob and compare), ``--frames`` (also render
N reference frames).

The ``gen_consts_header`` / ``gen_assets_c`` pair is the *source* of
the device's ``moon_consts.h`` and the ``moon_assets`` partition
content — a change to the asset pipeline is a *device* change (re-bake
+ re-flash), not a host-side convenience.

Where the prototype and the device differ
-----------------------------------------

The prototype is the *reference*; the device is the *port*. The
differences are all deliberate, and the gate (:doc:`the image gate <../verification/image-gate>`) measures
their combined residual:

.. list-table::
   :header-rows: 1
   :widths: 24 38 38

   * - Aspect
     - Prototype (Python)
     - Device (C core)
   * - Precision
     - float64, vectorized (SIMD)
     - float32, scalar (FPU)
   * - Relief
     - per-pixel equirect height + bilinear slope
       (no cache)
     - the int8 DEM + the packed gradient cache (16-bit load/px,
       :doc:`the C core <../implementation/core-c>`)
   * - Ephemeris
     - PyERFA ``builtin`` (eraEpv00 + eraMoon98)
     - vendored ERFA, the same models (eraEpv00 + eraMoon98)
   * - Tone LUT
     - the calibrated fit (``TONE_LUT``)
     - the same fit, 256-entry, warm by default
   * - The display constants
     - n/a (the prototype renders the *physics*)
     - the backlit dark side, the soft terminator, the warm tint —
       all *display-path* (:doc:`the dark side <../science/darkside>`, :doc:`the dark side <../science/darkside>` and :doc:`the artistic pass <../science/art-pass>`), outside the gate

The prototype's per-pixel gradient *without* the cache is why the
gate's C-Python floor is ~1 gray (the quantization, not the model) —
and why the host C bake (2.25 ms @ 400 px, the current ``perf_data.json``
capture, :doc:`performance <../verification/performance>`) beats the
prototype (47.6 ms) by 21×: the cache and the float32 FPU are the
port's two wins, and the gate proves they cost ~1 gray of fidelity.

The site's own tooling, on this API
-----------------------------------

Five tracked files import the package; a further pair of docs tools
deliberately never touch it:

- ``host_sim/check_gate.py`` (the C-vs-Python-vs-NASA gate,
  :doc:`the image gate <../verification/image-gate>`) is the deep
  one: it imports **six** modules — ``moon.assets`` /
  ``moon.constants`` / ``moon.astro`` exactly the way this page's
  examples do, plus ``moon.metrics`` (``to_gray``, ``disk_metrics``),
  ``moon.register`` (``register_shift``) and ``moon.validate``
  (``_iso``, ``select_frames``) — the prototype's whole validation
  path reused rather than re-derived (``check_gate.py:53-59``).
- ``docs/tools/perf_measure.py`` is the six-engine comparator: it
  times the *Python engine itself* through ``moon.animation.render_frame``
  — the 47.6 ms at 400 px the performance page quotes is that call —
  on ``moon.astro`` geometry over ``moon.textures`` loads at the
  benchmark's fixed epoch (``perf_measure.py:152-155``).
- ``docs/nasa_figs.py`` registers crops with ``moon.register``
  (:35), ``docs/tools/gen_nasa_quarters.py`` places them with
  ``moon.astro.geometry`` (:45), and ``docs/tools/gen_moonjs.py``
  embeds the ``moon.constants`` numbers in the browser sim (:41).
- ``host_sim/render_demo.py`` and ``docs/gen_media.py`` are the pair
  that shells out instead: the demo-film driver runs the ``moon_sim``
  *binary* (it does not import ``moon.animation``; the package's own
  ``make_frames``/``make_mp4`` stand as the host-side sweep API behind
  ``python -m moon.animation``), and the docs' films stitch **uisim**
  screenshots with ffmpeg (:doc:`the animation pipelines <../implementation/animations>`).

A reader who wants to *reproduce* a measured number runs the same
package — which is the point of keeping the reference in-tree and
tracked.

The next page is the device's half of the same interface: the C
API the core exposes (:doc:`the C API <c-api>`).
