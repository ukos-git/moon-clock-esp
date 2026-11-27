The Python API
==============

.. brief:
   Reader: a developer who validates or changes the moon model, or regenerates the device assets.
   They already know: Python and numpy, and that a C core runs on the device.
   After this page they can: find each module's public functions, run them, and know where the Python reference and the C port differ.
   Page type: reference.

The Python package at ``assets/images/moon/moon/`` is the validated reference the project is built on.
The image gate (:doc:`the image gate <../verification/image-gate>`) compares the C core's frames with it, the C core ports its math one to one (:doc:`the C core <../implementation/core-c>`), and the asset pipeline that bakes the device's albedo and elevation model (DEM) lives in it.
This page lists the package layout, each module's public functions and runnable examples, and states where the Python reference and the device path differ.

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
     movie.py       the boot-movie bake: one synodic cycle of
                    device-identical frames -> q90 gray JPEGs ->
                    moon_movie.bin (the flash-resident startup movie)

The package is lazy: ``moon/__init__.py`` has no imports.
The entry points are the modules themselves: ``python -m moon.calibrate``, ``python -m moon.animation``, ``python -m moon.assets`` and ``python -m moon.movie``.

.. _python-surface:

The package in one picture
--------------------------

The short version: the package has thirteen modules, and the repository has five tracked files that import it.
``constants.py`` is the hub that every validation path reaches.
``assets.py`` imports the most modules (seven), and it is the only module whose output the devices carry.
Two more tools, ``render_demo.py`` and ``gen_media.py``, import none of it; they run the compiled binaries instead.

The figures follow the site's usual pattern: one overview of five closed boxes, then one detail figure per box.
The detail figures show the import ledger, the third-party dependencies, the artifacts with their firmware ends, and the importers module by module.

.. graphviz:: ../_graphs/python-surface.dot
   :align: center
   :alt: Five boxes in a column (consumers, package, third parties, outputs, firmware ends) joined by import and write arrows.
   :caption: The Python side as the repository uses it, with no internals. Blue arrows are imports and writes; dashed arrows go to third parties and the firmware. Five tracked files import the package; two import nothing and run binaries instead.

The overview makes three claims:

- The Python reference runs in float64 and the C port in float32; the gate measures their residual (about 1 gray level).
- ``EPHEMERIS "builtin"`` is the same ``eraEpv00`` + ``eraMoon98`` pair that the C core vendors.
- The 21× speed-up on the performance page (2.25 ms against 47.6 ms at 400 px) compares this package's render path with the C core's.

``render_demo.py`` runs the ``moon_sim`` binary, and ``gen_media.py`` stitches uisim PNGs with ffmpeg.
The C half of the same interface is :ref:`the surface in one picture <c-api-surface>`.
The gate's dataflow is in ``image-gate.dot``, the pipeline flow in ``assets-pipeline.dot`` and the compile-and-flash story in ``build-seam.dot``.

The import ledger
~~~~~~~~~~~~~~~~~

Every arrow in the next figure is a ``from .X import`` line in the source, with comments stripped.

.. graphviz:: ../_graphs/python-surface-imports.dot
   :align: center
   :alt: Four stacked columns: the four importing modules, the shader, the five imported leaf modules and the constants hub, joined by the package's internal import arrows.
   :caption: The package's internal imports, read line by line from the source. Arrows point from the importing module to the imported one; the amber node is the ``constants`` hub.

``constants`` imports nothing (only ``from __future__``), yet more modules import it than any other.
The values the figure names live only there: ``POLE_RA_ICRF`` 269.839977, ``SYNODIC_DAYS`` 29.530589, and the ``REF_*`` display constants 400/199.3/199.3/199.5.
The modules import as follows:

- ``astro`` → constants.
- ``sphere`` is the numpy-only leaf, with no imports from the package.
- ``textures`` → constants.
- ``renderer`` → constants, sphere (``disk_grid``, ``equirect_bilinear``) and astro (``Geometry``).
- ``animation`` → constants, astro (``geometry``), renderer (``tonemap``, ``to_uint8``, ``render``) and textures (``load_textures``).
- ``calibrate`` → astro only (``body_basis``, ``body_lonlat``, :16).
- ``register`` and ``metrics`` are standalone leaves (numpy and skimage/scipy only).
- ``validate`` → six modules, plus ``register_shift``, ``disk_metrics`` and ``earthshine`` (:12-14) and ``scipy.ndimage.uniform_filter1d`` (:133).
- ``assets`` → seven, the widest import set in the tree.

The third-party dependencies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The next figure shows which module pulls which pinned package.
The full pin list is the package's ``requirements.txt`` (see `Dependencies`_).

.. graphviz:: ../_graphs/python-surface-deps.dot
   :align: center
   :alt: Modules on the left fanning out with dashed arrows to the pinned packages on the right: astropy, numpy, tifffile, PIL, scikit-image, scipy.ndimage, ffmpeg (run as a subprocess) and imageio (never imported).
   :caption: Which module uses which pinned package. The amber nodes are the six packages imported directly; the two dashed nodes are ffmpeg (run as a subprocess) and imageio (a transitive pin).

- ``astropy`` sits where the ephemeris is computed (``get_body_barycentric``, ``Time`` and units); ``calibrate`` and ``validate`` also use ``astropy.time.Time``.
  Its PyERFA builtin path runs ``eraEpv00`` + ``eraMoon98``, the same models the C core vendors.
- ``numpy`` is used by all thirteen modules.
- ``tifffile`` has one user, ``textures`` (the 16-bit LROC load).
- PIL writes the PNGs in ``animation``, ``validate`` and ``assets``, and the gray JPEGs in ``movie``.
- scikit-image provides ``register``'s phase cross-correlation (:13) and ``metrics``' ``structural_similarity`` (:15).
- ``scipy.ndimage`` provides ``register``'s shift (:28/:34) and ``validate``'s ``uniform_filter1d`` (:133).
- ``ffmpeg`` is a tool, not a dependency: ``make_mp4`` runs it as a subprocess (libx264, yuv420p, crf 20).
- ``imageio`` is in the pin list, but the package never imports it; it is scikit-image's transitive pin.

The package's own venv is ``assets/images/moon/.venv``; ``host_sim/.venv`` runs the gate against the same tree.

What the package writes
~~~~~~~~~~~~~~~~~~~~~~~

.. graphviz:: ../_graphs/python-surface-products.dot
   :align: center
   :alt: Three writer modules (assets.py, validate.py, animation.py) pointing to four artifacts: the asset blob and manifest, the generated header, the fitted tone curve with its diff images, and the sweep frames and video. Two dashed arrows lead from the blob and the header into the firmware.
   :caption: The artifacts each module writes. Dashed arrows mark the two artifacts the firmware consumes.

- ``assets.gen_assets_c`` writes ``out/<t>/moon_assets.bin``, plus ``albedo.bin``, ``dem.bin`` and the manifest.
  The blob is 2,470,400 / 4,194,304 / 897,536 B for s3 / p4 / knob; the storage ladder owns those byte counts.
- ``gen_consts_header`` writes ``components/moon/generated/<t>/moon_consts.h``, the header the firmware compiles (pole, W0, the DEM range, the sizes).
  The entry point is ``python -m moon.assets --target s3 p4 knob``; the choices come from ``constants.TARGETS``.
- ``validate`` writes the fitted tone-curve (LUT) file and the render/ref/diff PNGs (:203-206), and prints "paste into constants.py as TONE_LUT" (:170).
- ``calibrate`` prints the pole and prime-meridian fit (:231-240).
  Neither ``calibrate`` nor ``validate`` rewrites ``constants.py`` by itself.
- ``animation.make_frames`` writes ``anim.NNN.png``, and ``make_mp4`` writes ``moon_anim.mp4``.

The root CMakeLists' three ``FATAL_ERROR`` guards check both firmware ends (``build-seam.dot`` and ``assets-pipeline.dot`` cover that story).

Who imports the package
~~~~~~~~~~~~~~~~~~~~~~~

A repository-wide search for ``from moon.`` / ``import moon`` finds exactly these consumers:

.. graphviz:: ../_graphs/python-surface-consumers.dot
   :align: center
   :alt: Consumer files on the left (check_gate.py, perf_measure.py, three figure tools, and the dashed pair that imports nothing) with blue arrows to the modules they import.
   :caption: The package's consumers and the modules each one imports. The dashed pair at the bottom imports nothing.

The figure's line numbers, file by file:

- ``host_sim/check_gate.py`` :53-59 imports six modules: assets, constants, astro, metrics (``to_gray`` + ``disk_metrics``), register (``register_shift``) and validate (``_iso`` + ``select_frames``).
  It drives the C side through the ``moon_sim`` binary (``--list frames.txt --size 400 --neutral``), bakes per-target textures from ``constants.TARGETS`` (:104-106) and renders the Python side with ``assets.render_from_assets`` (:147).
- ``docs/tools/perf_measure.py`` :152-155 imports astro, animation (``render_frame``, the 47.6 ms at 400 px on the performance page), textures and constants.
- The three figure tools take one module each: ``docs/nasa_figs.py`` :35 (``register_shift``), ``docs/tools/gen_nasa_quarters.py`` :45 (``astro.geometry``) and ``docs/tools/gen_moonjs.py`` :41 (``constants``).
- ``render_demo.py`` runs ``host_sim/build/moon_sim`` (:26/:61) and ``gen_media.py`` stitches ``host_sim/uisim/out`` PNGs with ffmpeg (:31/:47); neither imports the package.

Dependencies
------------

The package imports six packages directly:

- ``astropy``: the PyERFA ephemeris and units;
- ``numpy``;
- ``tifffile``: the 16-bit LROC albedo;
- ``Pillow``: PNG output;
- ``scikit-image``: the SSIM implementation and ``register``'s phase cross-correlation;
- ``scipy``: ``ndimage``, for the shift in ``register`` and the ``uniform_filter1d`` in ``validate``.

``animation.make_mp4`` runs ``ffmpeg`` as a subprocess (libx264, yuv420p, crf 20) instead of importing it.
``scikit-image`` pulls ``imageio`` into the pin list, but no module of the package imports it.
The package does not use ``matplotlib``; PIL and ffmpeg make the mp4.
The full pin list is the package's ``requirements.txt``.
The package's own venv is ``assets/images/moon/.venv``; ``host_sim/.venv`` runs the gate, which imports the same tree.

constants: the single source of truth
-------------------------------------

Every other module imports its numbers from ``moon.constants``, and the C port mirrors it constant for constant.
The public names, with their values in the current tree:

.. list-table::
   :header-rows: 1
   :widths: 30 16 54

   * - Name
     - Value
     - Meaning
   * - ``POLE_RA_ICRF`` / ``POLE_DEC_ICRF``
     - 269.839977 / 68.110423 deg
     - the calibrated IAU_Moon body-frame pole (the ``calibrate``
       output against NASA SVS 5415, not the IAU 2015 default)
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
     - the legacy band-only blend radius, kept for A/B comparison
       (:doc:`the lighting model <../science/lighting>`)
   * - ``POSANGLE_SIGN``
     - 1.0
     - the posangle sign convention
   * - ``TEXTURE_LON_OFFSET_DEG``
     - 180.0
     - the LROC albedo's 180° longitude offset (a bug the gate caught;
       the C core carries the same offset)
   * - ``EARTHSHINE_A/B/C``
     - 30.455 / -11.733 / -2.615
     - the cubic earthshine fit ``A·x + B·x² + C·x³`` in the dark-side
       disk fraction
   * - ``TONE_LUT``
     - (256,)
     - the calibrated gray->gray tone curve (the fit, :doc:`the lighting model <../science/lighting>`)
   * - ``GEO_SUBSOLAR_TOL_DEG`` etc.
     - 0.6 / 0.6 / 2.0
     - the geometry tolerances against NASA's published positions
   * - ``MAE_MAX`` / ``SSIM_MIN``
     - 12.0 / 0.89
     - the prototype gate's thresholds (the C-vs-NASA limit of 12 in
       :doc:`the image gate <../verification/image-gate>` comes from here)
   * - ``NUM_INTERMEDIATE_FRAMES``
     - 24
     - the synodic-month sweep's per-month sub-sample
   * - ``EPHEMERIS``
     - ``"builtin"``
     - PyERFA's ``builtin`` ephemeris backend: the same low-order
       models the C core vendors (eraEpv00 + eraMoon98)

astro: the geometry
-------------------

``moon.astro`` turns a UTC instant into the calibrated body-frame geometry.
The public functions:

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

A verified example, run with ``host_sim/.venv/bin/python`` at the benchmark's epoch:

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

``Geometry`` carries the body-frame vectors ``S_body``, ``E_body``, ``right_body`` and ``up_body``.
The C core's ``moon_geom_t`` carries the same basis (:doc:`the C core <../implementation/core-c>`), so the gate compares C and Python like for like.
The two implementations share the subsolar offset, and the gate's 1-gray floor between C and Python comes from the port alone.

sphere: the sampling
--------------------

``moon.sphere`` is the vectorized sampling the renderer uses:

.. code-block:: python

   def disk_grid(size, radius_px, cx, cy)
       # (yy, xx, r_unit): the base-sphere direction per pixel (the
       # bake's r = u*right + v*up + w*E, vectorized)
   def equirect_bilinear(sample_map, lon_deg, lat_deg)
       # the equirect texture/height lookup (bilinear, wrapped)
   def tangent_normal(r, h_slope_u, h_slope_v, ...)
       # the displaced-sphere normal from the relief slopes

renderer: the shader
--------------------

``moon.renderer`` is the per-pixel model, the C core's exact math in vectorized form:

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

animation: the sweep
--------------------

``moon.animation`` is the host synodic-month sweep model.
It runs on its own with ``python -m moon.animation``; no device code or docs film uses it (:doc:`the animation pipelines <../implementation/animations>`).

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

textures: the 16-bit load
-------------------------

``moon.textures`` loads the LROC albedo and the LOLA height.
The LROC albedo must be loaded at 16 bits (``tifffile``): an 8-bit downcast loses the terminator's low levels before the tone curve sees them (:doc:`the geometry <../science/geometry>`).
Paths resolve inside the repository first; both sources are what ``get_assets.py`` fetches into ``moon-kit/``.
The albedo is the fixed ``moon-kit`` kit file.
The DEM resolves ``$MOON_HEIGHT``, then ``moon-kit/ldem_16.tif``, then the legacy ``/tmp/moon-research/ldem_16.tif``.

.. code-block:: python

   def albedo_path() / height_path() -> str
   def load_albedo(path=None) -> np.ndarray   # float64, sRGB-linear
   def load_height(path=None) -> np.ndarray   # meters (km -> m)
   def load_textures(alb_path=None, hgt_path=None) -> (alb, hgt)

validate: the prototype gate
----------------------------

``moon.validate`` is the Python reference's NASA gate, the one the C gate in :doc:`the image gate <../verification/image-gate>` compares against.
It selects frames from NASA SVS 5415's ``mooninfo_2025.json``, checks the geometry against the published positions (the tolerance constants), fits the tone LUT, and computes the per-frame MAE and SSIM.

``moon.calibrate`` is the one-time body-frame fit (pole and prime meridian) that produced the ``constants`` values.
It prints the fit (``calibrate.py:231-240``), and a person pastes it into ``constants.py``.
Neither ``calibrate`` nor ``validate`` rewrites its own source; ``validate``'s LUT print says "paste into constants.py as TONE_LUT" (``validate.py:170``).

``moon.register`` is the pixel-registration fallback: it finds a rendered crop's shift onto the NASA crop.
The gate's pixel-aligned convention makes it a no-op in the normal path.
In the tree it has two consumers besides its two callers inside the package (``validate`` and ``assets``):

- ``host_sim/check_gate.py:58`` calls ``register_shift`` before the C-vs-Python image diff;
- ``docs/nasa_figs.py:35``, the NASA-comparison figure maker, registers the simulator's crop onto NASA's before it draws the strip.

assets: the device derivation
-----------------------------

``moon.assets`` produces the device's flash content: the per-board ``moon_assets`` blob, albedo then DEM.
The blob is 2,470,400 B on the S3, 4,194,304 B on the P4 and 897,536 B on the knob (:doc:`the binary-size chronology <../verification/binary>`).

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

The module is also the pipeline's entry point: ``python -m moon.assets --target s3 p4 knob`` (the default is all targets).
Its other options:

``--out``
   The root directory; each target writes ``<root>/<target>/``.
``--alb WxH``
   Override the albedo size for the selection (equirect 2:1, even W ≤ 4096).
``--json``
   The NASA ``mooninfo_2025.json``.
``--roundtrip``
   Re-parse the produced blob and compare.
``--frames``
   Also render N reference frames.

``gen_consts_header`` and ``gen_assets_c`` are the source of the device's ``moon_consts.h`` and of the ``moon_assets`` partition content.
A change to the asset pipeline is therefore a device change: re-bake and re-flash.

Where the prototype and the device differ
-----------------------------------------

The Python package is the reference; the device runs the port.
Every difference is deliberate, and the gate (:doc:`the image gate <../verification/image-gate>`) measures their combined residual.

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
     - n/a (the prototype renders the physics)
     - the backlit dark side, the soft terminator and the warm tint,
       all on the display path and outside the gate
       (:doc:`the dark side <../science/darkside>` and
       :doc:`the artistic pass <../science/art-pass>`)

The Python reference computes the per-pixel gradient without the cache.
That is why the gate's C-Python floor is about 1 gray level (the quantization, not the model).
It is also why the host C bake (2.25 ms at 400 px, the current ``perf_data.json`` capture, :doc:`performance <../verification/performance>`) is 21× faster than the Python reference (47.6 ms).
The cache and the float32 FPU are the port's two gains, and the gate shows they cost about 1 gray level of fidelity.

The site's own tools on this API
--------------------------------

Five tracked files import the package; two more docs tools never touch it.

- ``host_sim/check_gate.py`` (the C-vs-Python-vs-NASA gate, :doc:`the image gate <../verification/image-gate>`) imports the most: six modules.
  It uses ``moon.assets``, ``moon.constants`` and ``moon.astro`` the way this page's examples do, plus ``moon.metrics`` (``to_gray``, ``disk_metrics``), ``moon.register`` (``register_shift``) and ``moon.validate`` (``_iso``, ``select_frames``).
  It reuses the Python reference's whole validation path instead of re-deriving it (``check_gate.py:53-59``).
- ``docs/tools/perf_measure.py`` compares six engines.
  It times the Python engine itself through ``moon.animation.render_frame`` (the 47.6 ms at 400 px on the performance page is that call), on ``moon.astro`` geometry over ``moon.textures`` loads, at the benchmark's fixed epoch (``perf_measure.py:152-155``).
- ``docs/nasa_figs.py`` registers crops with ``moon.register`` (:35), ``docs/tools/gen_nasa_quarters.py`` places them with ``moon.astro.geometry`` (:45), and ``docs/tools/gen_moonjs.py`` embeds the ``moon.constants`` numbers in the browser simulator (:41).
- ``host_sim/render_demo.py`` and ``docs/gen_media.py`` run binaries instead.
  The demo-film driver runs the ``moon_sim`` binary; it does not import ``moon.animation``, whose ``make_frames`` / ``make_mp4`` remain the host-side sweep API behind ``python -m moon.animation``.
  The docs films stitch uisim screenshots with ffmpeg (:doc:`the animation pipelines <../implementation/animations>`).

To reproduce a measured number, run the same package; that is why the reference stays in the tree.

Where to go next
----------------

- :doc:`the C API <c-api>`: the device's half of the same interface.
- :doc:`the image gate <../verification/image-gate>`: how the gate uses this package.
- :doc:`the moon constants <moon-consts>`: the generated header ``gen_consts_header`` writes.
