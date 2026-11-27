The reference data
==================

.. brief:
   Reader: a developer or technical reader who has read How the clock works.
   They already know: what albedo and relief maps are, roughly; basic shell use.
   After this page they can: name the three data sources, fetch and crop the NASA frames, and recognise the two loading traps.
   Page type: explanation.

Every claim in this section is checked against data NASA published.
This page says where that data comes from, why each source was chosen, and which two loading mistakes would quietly spoil the checks.
:doc:`How the clock works <../how-it-works>` introduced the two maps of the Moon and the NASA image test; this page names the exact files behind them.

The short version
-----------------

The project never grades its own homework.
Three sources, each with its own job:

1. The answer key is NASA SVS 5415.
   NASA's visualization group rendered the Moon for every hour of 2025: 8,760 frames made with real ephemeris and real surface data.
   The renderer is scored against these images.
2. The raw material is the CGI Moon Kit.
   To draw the Moon from physics you need to know how bright its surface is and how bumpy it is.
   The Kit ships those two things: an albedo map (how reflective each patch of surface is) and a relief map (how high each patch stands).
3. The yardstick implementation is the Python prototype.
   A small Python package renders the Moon from those two maps.
   The C code on the devices reproduces its maths 1:1, and the gate runs a second check that compares the two directly.
   A porting mistake therefore cannot hide behind a passing NASA comparison.

Two details of the loading can ruin everything without an error message.
The albedo must be read as full 16-bit (a popular image library quietly throws away 15 of the 16 bits per channel).
The texture's longitude sits 180° away from the longitude the ephemeris uses.
The loader guards the first, one shared constant handles the second, and the image gate catches a regression of either at once.

Vocabulary
----------

Eleven terms carry the page.
The :doc:`glossary <../glossary>` has the full definitions.

- **SVS 5415**: the NASA Scientific Visualization Studio release "Moon Phases for 2025", the 8,760-frame answer key.
- **Ephemeris**: the formulas that say where the Sun, Earth and Moon are at any instant.
  NASA's frames are rendered with one.
  The device runs its own compact formulas, and NASA's JSON geometry feed is the referee that checks the two agree.
- **Albedo**: how much light a surface reflects.
  The Moon's dark maria and bright highlands are albedo differences; the albedo map is the surface's paint.
- **LROC WAC**: the Wide Angle Camera on the Lunar Reconnaissance Orbiter, which imaged the whole surface for the albedo map.
- **Relief / DEM**: the shape of the ground.
  A digital elevation model (DEM) stores one height per map pixel.
  Relief is what makes craters cast shadows.
- **LOLA**: the Lunar Orbiter Laser Altimeter, which measured the relief.
- **Equirectangular**: the flat-map format both textures use.
  Longitude runs left to right, latitude top to bottom, and one rectangle covers the whole sphere.
- **16-bit / 8-bit**: how many brightness levels one colour channel stores.
  16-bit gives 65,536 and 8-bit only 256: the difference between smooth crater shading and banded, posterized grey.
- **sRGB**: the standard tone curve pictures are stored through so they look right on a monitor.
  The albedo TIFF's codes run through this curve, so the loader *linearizes* them (undoes the curve) before any lighting maths sees them.
- **Libration**: the Moon's slight rocking, which lets us see a little past its usual edges over a month.
  It changes which features are visible, so a validation set must cover a range of it.
- **Position angle (posangle)**: the compass direction of the disk's "up" as seen from Earth, that is, how the lit side leans.
  With the phase and the libration it is the third quantity the frame selection spreads its picks over.

The answer key: NASA SVS 5415
-----------------------------

The answer key is a public NASA video: one rendered frame for every hour of 2025, NASA's best estimate of how the Moon looked that hour.
The project reduces each frame it uses to a 400×400 PNG with the round disk centred.
The gate scores 32 of the 8,760 frames, picked to cover every phase and a spread of libration and position angle.

.. figure:: ../img/nasa/nasa_quarters.png
   :alt: Five NASA SVS 5415 frames from 2025, from new moon to gibbous, side by side.
   :figclass: align-center

   Five crops from the 8,760-frame series: the four quarter marks and the final frame.
   They are read from the answer-key crops under ``assets/images/moon/final_400x400/`` (``moon.NNNN.png``, 1-based; hour *h* is ``moon.{h+1:04d}.png``, the gate's own mapping).
   ``docs/tools/gen_nasa_quarters.py`` composites the strip from those frames; it never re-renders them.

Each caption's illumination comes from the project's own ``moon.astro.geometry``, the same physics the devices run.
It agrees with the frame's measured lit-pixel fraction to ±0.003.
At the new-moon frame the pixel threshold also counts earthshine.

The release is NASA's `"Moon Phases for 2025" <https://svs.gsfc.nasa.gov/5415/>`_: 8,760 frames of the Moon as seen from Earth, rendered with real ephemeris and real surface data.
The repository commits the 36 frames the pipeline reads (the gate's 32-frame set plus the five frames of the strip above).
They come from the 1920×1080 series and live under ``assets/images/moon/moon_phases_2025/`` (LFS, sha256-pinned).
``python3 assets/images/moon/get_assets.py --refs`` crops each committed frame to a 400×400 PNG.
The crop is a circle because the display is round, and 400 px matches the gate's working resolution.

To fetch and crop the full series by hand, run this from ``assets/images/moon/``.
The download goes to the Internet Archive's copy of the SVS ``/vis/`` tree:

.. code-block:: bash

   mkdir -p moon_phases_2025
   cd moon_phases_2025
   seq -f "%04g" 1 8760 | xargs -P 8 -I {} curl -fL -O \
     "http://web.archive.org/web/2025id_/https://svs.gsfc.nasa.gov/vis/a000000/a005400/a005415/frames/1920x1080_16x9_30p/plain/moon.{}.tif"
   cd ..
   mkdir -p final_400x400
   ls moon_phases_2025/moon.*.tif | parallel -j 8 --bar \
     "magick {} -alpha on -background none -trim +repage -filter Lanczos \
      -resize 400x400 -gravity center -extent 400x400 \
      PNG32:final_400x400/{/.}.png"

What the lines do:

- The ``mkdir``/``cd`` lines make a download folder and an output folder.
- ``seq -f "%04g" 1 8760`` prints the frame numbers ``0001`` to ``8760``, zero-padded, which are NASA's file names.
- ``xargs -P 8 -I {}`` puts each number into the URL where ``{}`` sits and runs eight ``curl`` downloads at a time.
- The second half hands every ``.tif`` to ImageMagick (``magick``, or ``convert`` on IM6 hosts; ``get_assets.py`` detects which).
  GNU ``parallel`` again runs eight at a time.
- ``-trim +repage`` cuts away the empty border around the Moon and recentres what is left.
- ``-filter Lanczos -resize 400x400`` shrinks it with a high-quality resampling filter.
- ``-gravity center -extent 400x400`` pads the result to a 400×400 square with the disk centred.
- ``-alpha on -background none`` keeps that padding transparent.
  What lands in ``final_400x400/`` is a round disk on a cleared field, the shape the round display and the gate's round mask want.
- ``{/.}`` is ``parallel``'s shorthand for the input file name without its directory and extension.

NASA also publishes the geometry as a JSON API (``https://svs.gsfc.nasa.gov/api/dialamoon/2025-01-01T00:00``).
The gate's frame selector and the geometry cross-check (:doc:`the geometry <geometry>`) read it.

The gate uses a 32-frame set spread across the year (``moon.validate.select_frames``).
The set covers every phase and a representative range of libration and position angle.
:doc:`The image gate <../verification/image-gate>` documents the selection and the metrics.

The raw material: the CGI Moon Kit
----------------------------------

A physical renderer needs two inputs: how bright every patch of surface is (the albedo, the paint) and how high every patch stands (the relief, the terrain).
NASA's `CGI Moon Kit <https://svs.gsfc.nasa.gov/4720/>`__ ships both, in large scientific formats:

- LROC WAC albedo: ``lroc_color_16bit_srgb_4k.tif``, a 4096×2048 *16-bit sRGB RGB TIFF*.
  It is the equirectangular colour map of the real lunar surface.
- LOLA relief: ``ldem_16.tif`` (16 px/deg), float32, in *kilometres*.
  The USGS 118 m full-resolution LOLA DEM also exists.
  The device carries a 2048×1024 int8 DEM (``MOON_DEM_W`` × ``MOON_DEM_H``), a baked-down version of the Kit's relief (:doc:`the C core <../implementation/core-c>`).

The device cannot carry the full files.
What ships on it is a shrunk version of each map, described in :doc:`the lighting model <lighting>`.

.. note::
   The project does not use the Moon Kit's own "plane→sphere" projection script.
   At 400–466 px it adds little.
   The project's renderer does the projection itself (:doc:`the geometry <geometry>`), so the camera basis (libration, position angle) is computed explicitly and validated against the ephemeris.

The two loading traps
---------------------

Both inputs have the same failure mode: the load succeeds with no error and no warning, and the data is wrong.
The symptoms look like renderer bugs rather than data bugs, which is why they are written down here.

1. *The albedo must be loaded 16-bit.*
   PIL quietly reduces the 16-bit sRGB TIFF to 8-bit on load.
   That crushes the range to roughly 79..255 and discards the maria and crater contrast.
   The loader reads the file with ``tifffile`` (plus ``imagecodecs`` for the LZW tiles) and linearizes the sRGB codes.
   If the Moon looks flat and washed out, check this first.
2. *The texture longitude is 180° from the body frame.*
   Column 0 of the albedo map is the map's own longitude 0.
   The Kit's maps are georeferenced 180° of longitude away from the IAU_Moon body frame the ephemeris uses.
   The renderer adds 180° to the body longitude before every lookup.
   Without the offset, every pixel samples the opposite side of the map: the disk still looks like a moon, but it shows the far side's features.

The guarded load in ``assets/images/moon/moon/textures.py`` handles trap 1, step by step:

1. ``tifffile.imread`` reads the full 16 bits per channel (never PIL).
2. Dividing by 65535 puts the values in 0–1.
3. ``_srgb_linear`` undoes the sRGB display curve, so the numbers describe actual light, which the lighting maths expects.
4. The three colour channels are mixed into one grey with the standard per-channel weights.
5. The renderer receives a linear-light map of floats in 0–255.

Trap 2 is one constant, ``TEXTURE_LON_OFFSET_DEG = 180.0`` in ``assets/images/moon/moon/constants.py``.
The asset build writes it into each board's ``moon_consts.h`` as ``MOON_TEX_LON_OFFSET``, so the C core and the Python prototype use the same number.
An edge-NCC placement scan against the NASA frames confirms it: the match peaks sharply at exactly 180° (NCC 0.45, against 0.18 or less elsewhere), at every primary phase, with no mirror or latitude flip needed.

The gate exercises both traps.
A wrong albedo load or a wrong offset breaks the C-vs-NASA MAE (mean absolute error) at once (:doc:`the image gate <../verification/image-gate>`), so neither can regress unnoticed.

The yardstick: the Python prototype
-----------------------------------

The validated reference implementation is a small Python package at ``assets/images/moon/moon/``.
It reads the two Kit maps and renders a Moon.
The C core (:doc:`the portable C core <../implementation/core-c>`) reproduces it 1:1 in maths.
The gate's second check (C against the Python model, MAE ≤ 2 per frame) keeps that match explicit.

:doc:`The Python API <../reference/python-api>` documents the package.
:doc:`The interactive simulator <interactive>` re-implements its core equations in JavaScript, so you can drive them in the browser.

Where to go next
----------------

- :doc:`Geometry and ephemeris <geometry>`: how the clock turns a time into the Sun, Earth and camera directions.
- :doc:`The image gate <../verification/image-gate>`: how the 32 frames are picked and scored.
- :doc:`The Python API <../reference/python-api>`: the prototype package, function by function.
