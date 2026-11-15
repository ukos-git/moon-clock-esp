.. NOTE: shared fragment — see kconfig-board-choice.inc for the conventions
   (no top-level headings, ../<section>/<page> :doc: targets).

.. list-table::
   :header-rows: 1
   :widths: 30 18 52

   * - Constant
     - Value
     - Meaning
   * - ``MOON_POLE_RA_ICRF``
     - ``269.839977``
     - Calibrated IAU_Moon pole right ascension (deg)
   * - ``MOON_POLE_DEC_ICRF``
     - ``68.110423``
     - Calibrated IAU_Moon pole declination (deg)
   * - ``MOON_PRIME_MER_W0``
     - ``40.017251``
     - Prime meridian W₀ (deg, the J2000 epoch value)
   * - ``MOON_PRIME_MER_RATE``
     - ``13.17617251``
     - Prime meridian rotation rate (deg/day)
   * - ``MOON_J2000_JD``
     - ``2451545.0``
     - J2000 epoch (Julian day)
   * - ``MOON_DISK_RADIUS_PX``
     - ``200.0``
     - The moon disk's on-screen radius in the core's 400 px image
       space
   * - ``MOON_IMAGE_SIZE``
     - ``400``
     - The core render image size (px; the UI upscales to the board's
       ``MOON_PX``)
   * - ``MOON_RADIUS_M``
     - ``1737400.0``
     - Mean lunar radius (m), for the relief scale
   * - ``MOON_POSANGLE_SIGN``
     - ``1.0``
     - Position-angle sign convention
   * - ``MOON_TEX_LON_OFFSET``
     - ``180.0``
     - The texture's 180° longitude offset (the LROC albedo's prime
       meridian is opposite the ICRF one)
   * - ``MOON_ALB_W`` / ``MOON_ALB_H``
     - S3/knob: ``864`` / ``432`` · P4: ``2048`` / ``1024``
     - Equirect albedo asset dimensions (row 0 = north, col 0 =
       longitude 0). **Per-board.**
   * - ``MOON_DEM_W`` / ``MOON_DEM_H``
     - S3/P4: ``2048`` / ``1024`` · knob: ``1024`` / ``512``
     - Relief DEM dimensions (int8, same equirect convention).
       **Per-board** — the knob's halved DEM is a 1 MiB relief cache
       instead of 4 MiB, and its gate passes (mean MAE 6.45)
   * - ``MOON_DEM_STEPS``
     - ``254``
     - DEM quantization steps (int8 over the height range)
   * - ``MOON_DEM_MIN_M``
     - S3/P4: ``-8518.19921875`` · knob: ``-8338.822265625``
     - DEM minimum height (m, LOLA) — per-board with the DEM size
       (quantization range of the baked grid)
   * - ``MOON_DEM_MAX_M``
     - S3/P4: ``10416.568359375`` · knob: ``9722.35546875``
     - DEM maximum height (m, LOLA) — per-board with the DEM size
   * - ``MOON_ES_A``
     - ``30.455``
     - Earthshine cubic fit, linear term (8-bit units)
   * - ``MOON_ES_B``
     - ``-11.733``
     - Earthshine cubic fit, quadratic term
   * - ``MOON_ES_C``
     - ``-2.615``
     - Earthshine cubic fit, cubic term
   * - ``MOON_TONE_LUT``
     - 256 entries
     - The 256-entry 8-bit tone LUT (bin 0 = 0, the pure dark side);
       the art pass's warm curve (see :doc:`the art pass
       <../science/art-pass>`). Shown in full in the generated header.
   * - ``MOON_TBL_N``
     - ``257``
     - The bake's atan / row-frac table length (256 intervals;
       ``moon_bake.c``)
   * - ``MOON_GREL_INV``
     - ``0.003``
     - The relief cache's int8 slope quantization step (±half-step
       moves ``L`` by < 0.3 %)
   * - ``MOON_NIGHT_BACKLIT_K``
     - ``0.20``
     - The computed backlit night side's peak level (gray ≈ 51,
       ``dark_zero = 1``; :doc:`the dark side <../science/darkside>`)
   * - ``MOON_EV_EARTH_R_M`` / ``MOON_EV_SUN_R_M`` / ``MOON_EV_AU_M``
     - ``6371000.0`` / ``695700000.0`` / ``149597870.7e3``
     - The eclipse cone's bodies (m) — ``moon_event.h``
   * - ``MOON_EV_UMBRA_TIP_M`` / ``MOON_EV_PENUMBRA_SLOPE``
     - derived
     - The umbra cone's tip distance and the penumbra's linear radius
       slope (formulas over the three constants above; at the mean
       distance: umbra ~4,580 km, penumbra ~8,175 km)
   * - ``MOON_EV_BLOOD_K``
     - ``0.85``
     - The blood-moon lit-side multiplier — ``L *= 1 − 0.85·occ``
       (a total eclipse shows a faint copper disk, not a black hole)

Two model notes:

- **The DEM is a displaced height field, not a normal map.** LOLA
  height is quantized to int8 as
  ``code = round((h − MIN) / (MAX − MIN) · STEPS) − 128``; the bake
  computes a 4-neighbor central-difference normal per pixel and applies
  the displaced-normal Lambert term
  ``L = clip(lns, 0, 1)`` over the whole lit disk, where
  ``lns = (d − G·S) / √(1 + ‖G‖²)`` (unclipped) and ``G`` is the
  central-difference gradient. The physically-exact displaced
  height-field model is what makes the terminator follow real terrain
  (:doc:`lighting <../science/lighting>`).
- **The earthshine is a phase-independent dark-side gradient**:
  ``R = A·x + B·x² + C·x³`` with ``x = max(0, −d)``,
  ``d = r̂ · S`` — the dark side never renders as pure black
  (:doc:`the dark side <../science/darkside>`).
