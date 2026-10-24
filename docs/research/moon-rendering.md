# Deep Research: Geometry-Based Moon-Phase Rendering (ESP32)

Date: 2026-10-24 · Depth: thorough · Method: Firecrawl search + scrape (primary
sources only)

## Executive Summary

The goal is to render a physically correct, current moon phase on a constrained
ESP32-S3, validated against NASA's own moon imagery. The approach — a
**textured (albedo) + displaced (height) sphere rendered with real Sun/observer
geometry** — is not a research experiment; it is literally how NASA builds
these images. NASA's **CGI Moon Kit** ships the exact two ingredients (LROC
color/albedo map + LOLA elevation/displacement map) "designed for use in 3D
rendering software," and NASA's **2025 Moon Phase & Libration dataset**
publishes, for every hour of the year, the sub-solar point, sub-Earth point,
position angle, phase, and apparent diameter — which is both the _rendering
input_ and the _ground-truth validation target_.

The compute problem on the ESP is solved by splitting work between a **one-time
Python prototype** (which derives validated assets: albedo, height/normal, and
terminator-band masks) and a **tiny runtime C kernel** that only does cheap
lookups in the bright/dark zones plus a per-pixel `N·S` term confined to the
thin terminator band. A host ("simulator") build of the _same_ C code emits
PNGs that can be image-diffed against the NASA frames, closing the correctness
loop before any hardware is involved.

## Key Findings

1. **NASA publishes the rendering inputs _and_ the ground truth, per hour.**
   The `mooninfo_2025.json` file (8760 records, one per hour of 2025) carries,
   for every hour, `subsolar{lon,lat}`, `subearth{lon,lat}`, `posangle`,
   `phase`, `diameter`, `distance`, `j2000{ra,dec}`. This is both the geometry
   to render and the target to validate against.
   [NASA SVS 5415](https://svs.gsfc.nasa.gov/5415).

2. **The exact texture + displacement assets are free and already local.** The
   CGI Moon Kit provides the LROC color map (4096×2048, sRGB 16-bit — already
   at `assets/images/moon/moon-kit/lroc_color_16bit_srgb_4k.tif`) and the LOLA
   displacement map at 64/16/4 pixels-per-degree (float km, or uint16
   half-meters), all "centered on 0° longitude."
   [NASA SVS 4720](https://svs.gsfc.nasa.gov/4720).

3. **Astronomy is a standard vector problem, best done with astropy.**
   Barycentric/topocentric positions of Sun, Moon, and Earth give the sub-solar
   and sub-Earth points and the libration directly; the position angle (roll)
   comes from the Moon's axis orientation. astropy's `get_body_barycentric`
   (ICRF) / `get_body` (GCRS), with built-in PyERFA or JPL DE432s/DE430
   ephemerides, is the right tool.
   [astropy API](https://docs.astropy.org/en/stable/api/astropy.coordinates.get_body_barycentric.html)

4. **Libration = the sub-Earth point + position angle.** Longitudinal libration
   reaches ±7°54′ (orbital eccentricity), latitudinal ±6.7° (axial tilt); the
   sub-Earth point is the _apparent center_ of the disk, and the position angle
   is the roll of the Moon's north pole. All three are given directly in
   `mooninfo_2025.json`.
   [Wikipedia: Libration](https://en.wikipedia.org/wiki/Libration)

5. **The phase/terminator is pure Sun-relative geometry.** The illuminated
   fraction is `(1+cos i)/2` where `i` is the Sun–Moon–observer (phase) angle;
   the terminator is the great circle 90° from the sub-solar point. Mountain
   shadows at the terminator come from the surface **normal** (from LOLA
   height) dotting the **sun direction** — and this is the _only_ place the ESP
   needs real math.

6. **Validation is well-trodden.** `skimage.metrics` provides SSIM, PSNR, MSE,
   and NRMSE; `skimage.registration.phase_cross_correlation` aligns rendered vs
   reference frames (already used in this repo's
   `assets/images/moon/alpha_mask.py`).

## Detailed Analysis

### 1. Data & assets (primary sources)

| Asset                                                       | Source                                                                                                                                      | Format                                      | Status                                                                                |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------- | ------------------------------------------------------------------------------------- |
| Hourly geometry (subsolar/subearth/posangle/phase/diameter) | [NASA SVS 5415](https://svs.gsfc.nasa.gov/5415)                                                                                             | `mooninfo_2025.json`, 8760 records          | **Downloaded & verified**                                                             |
| Hourly reference frames (ground-truth images)               | NASA SVS 5415 (1920×1080 plain, alpha)                                                                                                      | TIFF/PNG                                    | Already local: `assets/images/moon/{moon_phases_2025, final_400x400}/`                |
| Albedo/color map                                            | [NASA SVS 4720](https://svs.gsfc.nasa.gov/4720) CGI Moon Kit                                                                                | `lroc_color_16bit_srgb_4k.tif` (4096×2048)  | **Already local** `assets/images/moon/moon-kit/`                                      |
| Height/displacement (LOLA)                                  | [NASA SVS 4720](https://svs.gsfc.nasa.gov/4720) / [USGS LRO LOLA DEM 118m](https://astrogeology.usgs.gov/search/map/moon_lro_lola_dem_118m) | `ldem_*.tif` (64/16/4 px-deg, km or uint16) | To download (Moon Kit `ldem_16.tif`, 16 px/deg); USGS full-res 8 GB GeoTIFF available |

Key notes:

- The color maps are built from the **Hapke-normalized LROC WAC mosaic** (643
  nm→R, 566 nm→G, 415 nm→B), and cover only 70°N–70°S — the polar caps are
  filled with lower-resolution monochrome LRO laser albedo (they "comprise >20%
  of map pixels but 6% of the surface," and stay in shadow so they're rarely
  visible). NASA states the maps are "**optimized for aesthetics, not
  science**" — fine for this use.
- The displacement maps are "centered on 0° longitude" (equirect), available at
  64/16/4 pixels-per-degree, as float TIFFs in kilometers relative to a radius
  of 1737.4 km, or unsigned 16-bit TIFFs in half-meters.
- Verified `mooninfo_2025.json` record schema (frame 1):
  `{"time": "01 Jan 2025 00:00 UT", "phase": 1.47, "age": 1.065, "diameter": 1877.5, "distance": 381744, "j2000": {"ra": 19.7533, "dec": -25.9205}, "subsolar": {"lon": 162.189, "lat": -1.52}, "subearth": {"lon": -4.627, "lat": 6.051}, "posangle": 350.387}`

### 2. Astronomy geometry (sub-solar / sub-Earth / libration)

Two independent, cross-checkable routes:

- **Vector route (recommended):** at time _t_, get barycentric positions of
  Sun, Moon, and Earth (astropy, ICRF). The **sub-solar point** = direction
  from Moon to Sun, projected onto the Moon's body-fixed frame; the **sub-Earth
  point** = direction from Moon to Earth (this _is_ the apparent disk center,
  encoding libration in longitude/latitude). The **position angle** = rotation
  of the Moon's north axis vs. celestial north (the roll). Camera for the "view
  from Earth" = place the camera along the sub-Earth direction, roll by
  `posangle`, zoom by `diameter`.
- **Classical spherical route (cross-check):** derive the sub-solar point from
  the phase angle
  - the Moon's ecliptic inclination (`gss = acos(cos lss · cos mss)`).
    [castor2.ca](http://www.castor2.ca/16_Calc/01_Moon/04_Sub_Solar/index.html)

**Ephemeris precision:** astropy's built-in (PyERFA) ephemeris is approximate.
For sub-degree sub-solar-point accuracy (which matters for terminator
placement) use the **JPL DE432s** ephemeris (~10 MB, valid 1950–2050 — covers
the validation window) via `jplephem`, or DE430 (~115 MB, 1550–2650) for the
long haul.

**Validation target:** `mooninfo_2025.json` gives `subsolar{lon,lat}`,
`subearth{lon,lat}`, `posangle`, `phase` for every hour — so the geometry
module's output is checked field-by-field against NASA, not just visually.

### 3. Lighting & rendering

Per-pixel model for a displaced sphere, view from Earth:

- **Camera** along the sub-Earth direction, rolled by `posangle`, FOV from
  `diameter`.
- **Sun direction** = toward the sub-solar point.
- **Surface normal** `N` from the LOLA height gradient (precomputed, or live
  from a stored normal map).
- **Lambert term** `L = max(0, N·S)`; illuminated fraction `(1+cos i)/2`.
- **Albedo** `A` from the LROC color map.
- Pixel = `A · L` (plus the terminator relief band).

**The terminator band (the "in-between zone"):** the thin strip within ±(a few
degrees) of the terminator great-circle. Only there do we evaluate the
expensive `N·S` using the height-derived normal; everywhere else it's a flat
lookup (bright zone = `A`, dark zone ≈ 0). This matches the project's confirmed
"live normal, band only" runtime decision.

**Tooling:** for _per-pixel control that matches NASA and runs on CPU_, a
**NumPy displaced-sphere ray-caster** (one analytic ray–sphere hit per
convex-sphere pixel, sampling albedo + height per pixel) is the pragmatic
choice; GPU libs (nvdiffrast/PyTorch3D/PyVista) add dependency weight with
little benefit at 400–466 px. The CGI Moon Kit's own "plane→sphere"
texture-projection map documents the equirectangular→sphere mapping.

### 4. Validation & metrics

- **Geometry correctness:** compare computed `subsolar/subearth/posangle/phase`
  vs. `mooninfo_2025.json` across a set of hours (8 primary phases +
  intermediates) — field-by-field, sub-degree / sub-percent thresholds.
- **Image correctness:** render the sphere, register to the NASA crop with
  `skimage.registration.phase_cross_correlation`, then compute
  `skimage.metrics.structural_similarity` (SSIM), `peak_signal_noise_ratio`
  (PSNR), and MSE/NRMSE over a full synodic cycle. The C simulator PNGs get the
  _same_ diff, proving the C path matches the Python path matches NASA.

## Contrarian Views and Risks

- **The existing `alpha_mask.py` is the wrong approach for this goal.** It's
  data-driven per-phase (α = luminance ratio between full/new frames + a 2D
  libration _rotation_). It bakes a static phase into per-hour masks and cannot
  produce a physically correct, arbitrary-date terminator with mountain relief.
  The geometry renderer supersedes it; its registration
  (`phase_cross_correlation`) and MAE/diff ideas are reused.
- **All source assets are gitignored / local-only.** `assets/` is not in git;
  the LOLA DEM and 4K albedo live on disk. The asset-derivation pipeline must
  be deterministic and reproducible (pinned versions + a documented download
  step), or the maps must be committed as derived, compressed binaries.
- **Asset size vs. the 1792K app / 128K SPIFFS.** A 400×400 albedo (8-bit) +
  height (8-bit) + terminator-band mask won't fit 128K SPIFFS; it needs a
  dedicated raw flash partition or app embedding. This is the dominant size
  constraint.
- **Ephemeris download:** DE432s/DE430 is a one-time network fetch (10–115 MB).
  Fine offline once cached, but it's a dependency of the prototype.
- **16-bit sRGB → 8-bit panel:** the AMOLED is 16-bit RGB565, self-luminous, no
  backlight; dithering/gamma shift the look vs. the 8-bit NASA frames. The
  image-diff must be run at 8-bit/565 to be representative.
- **IMU on a shared I2C bus** (deferred): the QMI8658C (0x6A) shares GPIO14/15
  with the RTC (0x51), TCA9554 (0x20), and touch (0x15); bus contention and the
  board's `BSP_CAPS_IMU 0` are integration risks handled in the follow-up
  phase.

## Open Questions

1. **Ephemeris tier** — is built-in PyERFA "close enough," or is DE432s needed
   for sub-degree sub-solar accuracy? (Plan: start with built-in, escalate to
   de432s if validation exceeds 0.5° — decided empirically in Phase 1.)
2. **Final asset budget** — exact bit-depths + resolution for
   albedo/normal/band-masks that fit the 1792K app + PSRAM (decided in the
   asset-derivation phase, gated by image-diff quality).
3. **Pass thresholds** — concrete SSIM/PSNR/NRMSE cutoffs for the "matches
   NASA" gate (plan: MAE < 12/255 and SSIM > 0.90, tuned once then frozen in
   `constants.py`).
4. **Startup animation** — frame count + target fps for the last-month
   (29.5-day) sweep (plan: 120–240 subsampled frames, ≤ every 3 h).

## Sources

- [NASA SVS 5415 — Moon Phase and Libration, 2025](https://svs.gsfc.nasa.gov/5415)
  — per-hour subsolar/subearth/posangle/phase/distance + reference frames;
  `mooninfo_2025.json`.
- [NASA SVS 4720 — CGI Moon Kit](https://svs.gsfc.nasa.gov/4720) — LROC color +
  LOLA displacement maps for 3D rendering.
- [USGS Astrogeology — Moon LRO LOLA DEM 118m](https://astrogeology.usgs.gov/search/map/moon_lro_lola_dem_118m)
  — full-resolution global LOLA LDEM (8 GB GeoTIFF).
- [astropy — `get_body_barycentric`](https://docs.astropy.org/en/stable/api/astropy.coordinates.get_body_barycentric.html)
  /
  [Solar System ephemerides](https://github.com/astropy/astropy/blob/main/docs/coordinates/solarsystem.rst)
  — barycentric/topocentric positions; built-in vs JPL DE430/DE432s
  ephemerides.
- [Wikipedia — Libration](https://en.wikipedia.org/wiki/Libration) —
  longitudinal (±7°54′) / latitudinal (±6.7°) libration; sub-Earth = apparent
  center.
- [Wikipedia — Phases of the Moon](https://en.wikipedia.org/wiki/Phases_of_the_Moon)
  — phase/illumination and orientation.
- [castor2.ca — Finding the Moon's Sub-Solar Point](http://www.castor2.ca/16_Calc/01_Moon/04_Sub_Solar/index.html)
  — classical spherical sub-solar computation.
- [scikit-image — `skimage.metrics`](https://scikit-image.org/docs/stable/api/skimage.metrics.html)
  — SSIM, PSNR, MSE, NRMSE.
- Local: `assets/images/moon/alpha_mask.py`, `assets/images/moon/README.md`,
  `assets/images/moon/moon-kit/`, `assets/images/moon/final_400x400/`.

## Rerun Inputs

```
workflow: firecrawl-deep-research
topic: ESP32 geometry-based moon-phase rendering (NASA-validated)
depth: thorough
output: markdown
```
