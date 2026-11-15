The reference data
==================

Everything in this section is validated against real data. This page
documents where that data comes from, why it was chosen, and the two
subtle loading traps that would otherwise silently corrupt the
whole validation.

The short version
-----------------

This project never grades its own homework. Every claim about what the
Moon should look like is scored against data NASA published, and this
page is the receipt for that data: where it came from, which of the
many available NASA products was chosen, and why. Three sources, each
with a different job:

1. **The answer key — NASA SVS 5415.** NASA's visualization group
   rendered the Moon for every hour of 2025: 8,760 frames made with
   real ephemeris and real surface data. Our renderer is scored against
   these images — they are what "correct" looks like.
2. **The raw material — the CGI Moon Kit.** To draw the Moon from
   physics you must know what its surface is made of and how bumpy it
   is. The Kit ships exactly those two things: an albedo map (how
   reflective each patch of surface is) and a relief map (how high each
   patch stands). Everything the renderer paints with comes from here.
3. **The yardstick implementation — the Python prototype.** A small
   Python package renders the Moon from those two maps. The C code that
   runs on the devices reproduces its math 1:1, and the gate runs a
   second check that compares the two directly — so a porting mistake
   cannot hide behind a passing NASA comparison.

Two details in how that data is loaded can silently ruin everything:
the albedo must be read as full 16-bit (a popular image library quietly
throws away 15 of the 16 bits per channel), and the texture's longitude
sits 180° off from what the ephemeris conventions assume. Both traps
were actually hit once during prototyping, both are guarded in one
loader file, and the image gate would catch a regression of either
immediately. The sections below give each source its exact file names,
units and conventions.

Vocabulary
----------

Eleven terms carry the page (the full definitions live in the
:doc:`glossary <../glossary>`):

- **SVS 5415** — the NASA Scientific Visualization Studio release
  "Moon Phases for 2025"; the 8,760-frame answer key the first section
  documents.
- **Ephemeris** — the machinery that says where the Sun, Earth and Moon
  are at any instant. NASA's frames are rendered *with* one; the
  device runs its own compact formulas, and NASA's JSON geometry feed
  is the referee that checks the two agree.
- **Albedo** — how much light a surface reflects. The Moon's dark
  maria and bright highlands are albedo differences; the albedo map is
  the surface's paint.
- **LROC WAC** — the Lunar Reconnaissance Orbiter camera that imaged
  the whole surface to build the albedo map.
- **Relief / DEM** — the shape of the ground itself; a digital
  elevation model stores one height number per map pixel. Relief is
  what makes craters cast shadows.
- **LOLA** — the Lunar Orbiter Laser Altimeter that measured the relief.
- **Equirectangular** — the flat-map format both textures ship in:
  longitude runs left→right, latitude top→bottom, one rectangle
  covering the whole sphere.
- **16-bit / 8-bit** — how many brightness levels one color channel
  stores: 16-bit gives 65,536, 8-bit only 256 — the difference between
  smooth crater shading and banded, posterized gray.
- **sRGB** — the standard tone curve pictures are stored through to
  look right on a monitor. The albedo TIFF's codes run through this
  curve, so they must be *linearized* (the curve undone) before any
  lighting math sees them.
- **Libration** — the Moon's slight rocking that lets us peek a little
  past its usual edges over a month; which features you can see
  changes, so a validation set must cover a range of it.
- **Position angle (posangle)** — the compass direction of the disk's
  "up" as seen from Earth: how the lit side leans. Together with the
  phase and libration it is the third number the frame selection
  spreads its picks over.

NASA SVS 5415 — the 8760 frames
-------------------------------

**The short version.** The answer key is a public NASA video: one
rendered frame for every hour of 2025, NASA's own best guess at what
the Moon actually looked like each hour. The project downloads the
whole set and reduces every frame to a 400×400 PNG with the round disk
centered — the working format the gate diffs against. The gate then
scores 32 of the 8,760 frames, picked to cover every phase plus a
spread of libration and posangle; NASA's JSON feed supplies each pick's
geometry for the cross-check on the geometry page. The shell block
below is the exact download-and-reduce recipe, line by line after it.

The validation reference is NASA's
`"Moon Phases for 2025" <https://svs.gsfc.nasa.gov/5415/>`_ — 8,760
frames (one per hour of 2025) of the Moon as seen from Earth,
rendered by NASA's visualization group with real ephemeris and real
surface data. The project downloads the 1920×1080 set and crops it to
a 400×400 PNG (the display is round, so the crop is a circle; 400 px
matches the gate's working resolution)::

   mkdir -p moon_phases_2025
   cd moon_phases_2025
   seq -f "%04g" 1 8760 | xargs -P 8 -I {} curl -O \
     "https://svs.gsfc.nasa.gov/vis/a000000/a005400/a005415/frames/1920x1080_16x9_30p/plain/moon.{}.tif"
   cd ..
   mkdir -p final_400x400
   ls moon_phases_2025/moon.*.tif | parallel -j 8 --bar \
     "magick {} -alpha on -background none -trim +repage -filter Lanczos \
      -resize 400x400 -gravity center -extent 400x400 \
      PNG32:final_400x400/{/.}.png"

What those lines do, one by one: the ``mkdir``/``cd`` lines make a
download folder and an output folder. ``seq -f "%04g" 1 8760`` prints
the frame numbers ``0001`` through ``8760``, zero-padded — exactly the
file names NASA uses. ``xargs -P 8 -I {}`` feeds each number into the
URL where ``{}`` sits and runs eight ``curl`` downloads at a time. The
second half hands every downloaded .tif to ImageMagick (``magick``),
again eight at a time via GNU ``parallel``. Inside the ImageMagick
command: ``-trim +repage`` cuts away the empty border around the Moon
and recenters what's left; ``-filter Lanczos -resize 400x400`` shrinks
it with a high-quality resampling filter; ``-gravity center -extent
400x400`` pads the result to an exact 400×400 square with the disk
centered; the ``-alpha on -background none`` pair keeps that padding
transparent, so what lands in ``final_400x400/`` is not a square
thumbnail but a round disk on a cleared field — the shape the round
display and the gate's round mask want. ``{/.}`` is ``parallel``'s
shorthand for the input file's name without its directory and
extension.

NASA also exposes the geometry as a JSON API
(``https://svs.gsfc.nasa.gov/api/dialamoon/2025-01-01T00:00``), which
is what the gate's frame selector and the geometry cross-check
(:doc:`the geometry <geometry>`) read.

The gate does not use all 8,760 frames. It uses a **32-frame set
spread across the year** (``moon.validate.select_frames``), chosen so
every phase and a representative range of libration/posangle values
are covered. :doc:`The image gate <../verification/image-gate>` documents the selection and the metrics.

The CGI Moon Kit — the two ingredients
--------------------------------------

**The short version.** A physical renderer needs exactly two
ingredients: a map of how bright every patch of the surface is (*the
albedo* — the paint) and a map of how high every patch stands (*the
relief* — the terrain). NASA's CGI Moon Kit ships precisely those two,
in big scientific formats: a 4096×2048 16-bit color albedo TIFF and a
per-pixel height grid in kilometers. The device can't carry the full
files; what ships on it is a shrunk-down version of the relief. The
Kit's own projection script is deliberately skipped — the project
renders the sphere itself, so every step of the camera geometry is
explicit and can be validated (next page).

The surface data is NASA's `CGI Moon
Kit <https://svs.gsfc.nasa.gov/4720/>`__, which ships the exact two
things a physical renderer needs:

- **LROC WAC albedo**:
  ``lroc_color_16bit_srgb_4k.tif`` — 4096×2048, **16-bit sRGB RGB
  TIFF**. This is the equirectangular color map of the real lunar
  surface.
- **LOLA relief**: ``ldem_16.tif`` (16 px/deg) — float32, in
  **kilometers**. The USGS 118 m full-resolution LOLA DEM is
  available too; the 2048×1024 int8 DEM used on the device
  (``MOON_DEM_W`` × ``MOON_DEM_H``) is the baked-down version of the
  Kit's relief (:doc:`the C core <../implementation/core-c>`).

.. note::
   The Moon Kit's own "plane→sphere" projection script is *not* used.
   At 400–466 px it adds little, and the project's own renderer does
   the projection (:doc:`the geometry <geometry>`) so the camera basis — libration,
   posangle — is computed explicitly and validated against the
   ephemeris.

The two loading traps
---------------------

**The short version.** Both ingredients share one failure mode: the
load *succeeds* — no error, no warning — and the data is quietly wrong.
A friendly image library discards the albedo's high bits, and the
texture's longitude zero is not where the ephemeris thinks it is. The
symptoms look like renderer bugs rather than data bugs — a flat,
washed-out Moon; a Moon with the day/night line on the wrong side and
the features mirrored — which is exactly why the two are written down
here. Both are guarded in one small loader file, and the gate catches
any regression instantly.

Both ingredients have a silent corruption mode that was hit during
prototype development and is guarded against in
``assets/images/moon/moon/textures.py``:

1. **The albedo must be loaded 16-bit.** PIL silently decimates the
   16-bit sRGB TIFF to 8-bit on load — crushing the range to roughly
   79..255 and discarding the maria/crater contrast. The loader reads
   it with ``tifffile`` (+ ``imagecodecs`` for the LZW tiles) and
   linearizes the sRGB code values. If you see a moon that looks flat
   and washed out, this is the first thing to check.

2. **The texture longitude has a 180° offset.** The albedo texture's
   column 0 is not the prime meridian the way the ephemeris
   conventions assume; the renderer applies a 180° longitude offset
   when sampling. Get this wrong and the terminator lands on the
   wrong hemisphere — the images will *look* like a moon, just with
   the features mirrored.

What the guarded load in ``textures.py`` does, step by step, against
trap 1: ``tifffile.imread`` reads the TIFF's full 16 bits per channel
(never PIL), the values are divided by 65535 to land in 0–1,
``_srgb_linear`` undoes the sRGB display curve so the numbers describe
actual light — which is what the lighting math expects — the three
color channels are then mixed into one gray by their standard
per-channel weights, and the result arrives at the renderer as a
linear-light 0–255 float map. Against trap 2, the offset is one
constant: the renderer adds 180° to the longitude before looking a
point up in the map.

Both traps are exercised by the gate: a wrong albedo load or a wrong
offset breaks the C-vs-NASA MAE immediately (:doc:`the image gate <../verification/image-gate>`), so they
cannot regress silently.

The Python prototype package
----------------------------

**The short version.** Before any C existed there was a Python model:
a small package that reads the two Kit maps and renders a Moon. It
remains the reference implementation — the yardstick the C port must
match 1:1 in math, which the gate's second check (C vs Python, MAE ≤ 2
per frame) exists to enforce. Its API is documented in the reference
section, and the browser simulator re-expresses its core equations in
JavaScript so you can drive the model by hand.

The validated reference implementation is a small Python package at
``assets/images/moon/moon/``. It is the ground truth for the C port:
the C core (:doc:`the portable C core <../implementation/core-c>`) reproduces it 1:1 in math, and the gate's
second check (C vs Python model, MAE ≤ 2 per frame) exists precisely
to keep that fidelity explicit. :doc:`the Python API <../reference/python-api>` documents the package's API;
the interactive simulator in :doc:`the interactive simulator <interactive>` re-implements its core
equations in JavaScript so you can drive them in the browser.
