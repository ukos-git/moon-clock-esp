Chapter 4 — The reference data
==============================

Everything in Part II is validated against real data. This chapter
documents where that data comes from, why it was chosen, and the two
subtle loading traps that would otherwise silently corrupt the
whole validation.

NASA SVS 5415 — the 8760 frames
-------------------------------

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

NASA also exposes the geometry as a JSON API
(``https://svs.gsfc.nasa.gov/api/dialamoon/2025-01-01T00:00``), which
is what the gate's frame selector and the geometry cross-check
(Chapter 5) read.

The gate does not use all 8,760 frames. It uses a **32-frame set
spread across the year** (``moon.validate.select_frames``), chosen so
every phase and a representative range of libration/posangle values
are covered. Chapter 17 documents the selection and the metrics.

The CGI Moon Kit — the two ingredients
--------------------------------------

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
  Kit's relief (Chapter 11).

.. note::
   The Moon Kit's own "plane→sphere" projection script is *not* used.
   At 400–466 px it adds little, and the project's own renderer does
   the projection (Chapter 5) so the camera basis — libration,
   posangle — is computed explicitly and validated against the
   ephemeris.

The two loading traps
---------------------

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

Both traps are exercised by the gate: a wrong albedo load or a wrong
offset breaks the C-vs-NASA MAE immediately (Chapter 17), so they
cannot regress silently.

The Python prototype package
----------------------------

The validated reference implementation is a small Python package at
``assets/images/moon/moon/``. It is the ground truth for the C port:
the C core (Part III) reproduces it 1:1 in math, and the gate's
second check (C vs Python model, MAE ≤ 2 per frame) exists precisely
to keep that fidelity explicit. Part V documents the package's API;
the interactive simulator in Chapter 9 re-implements its core
equations in JavaScript so you can drive them in the browser.
