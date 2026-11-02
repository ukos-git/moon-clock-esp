Chapter 12 — Rotation and the gray→RGB565 LUT
=============================================

Two small functions close the core: the **in-plane rotation**
("ball-in-hand" free spin) and the **gray→RGB565** conversion. Both
run on every frame the spin displays, so both are latency-optimized;
both are in ``components/moon/core/moon_render.c`` together with the
fixed-point table in ``moon_fixp.c``.

Why a rotation at all
---------------------

The bake renders one fixed phase per frame — baking is ~1.3 s on the
S3, so it cannot follow a hand-held rotation. The product decision
was: the *baked* moon should still turn under the viewer's hand the
way a physical model does. The solution is to bake the phase **once**,
then rotate the baked frame in-plane every display frame with a cheap
nearest-neighbor gather. The in-plane rotation is exact for what it
models (turning the visible disk about the screen normal) and wrong
for what it is not (a true 3-D pose would need a re-bake — which is
what the *orbit* pipeline does when the tilt demands it, Chapter 14).

The fixed-point table
---------------------

The gather needs :math:`\cos\theta` and :math:`\sin\theta` per frame
(240 frames per spin turn). ``moon_fixp.c`` provides them as
**Q15.15** integers (16 fixed bits, values in
:math:`[-2^{15}, 2^{15}-1]`) from a lazily built quarter table:

.. code-block:: c
   :caption: components/moon/core/moon_fixp.c (abridged)

   #define NQ 1024 /* quarter-table entries: 0..pi/2 inclusive */
   static int32_t sin_tab[NQ + 1];

   int32_t moon_sin15(float rad) {
       /* fold rad into [0, pi/2] by quadrant, table look-up + linear
        * interpolate, then re-sign per quadrant */
       ...
   }
   int32_t moon_cos15(float rad) { return moon_sin15(rad + M_PI / 2.0); }

The table is 1025 int32 entries (8 KB) covering [0, π/2]; the
four-quadrant fold (the ``red16`` reduction) covers the full circle.
Linear interpolation between entries gives ~18 bits of angular
precision — the spin's angular error per frame is far smaller than
one pixel of rotation.

The gather
----------

The rotation is an **inverse mapping**: for each destination pixel
:math:`(x, y)`, compute where it came from in the source frame and
gather that one byte. No destination scattering, no interpolation, no
disk test — the disk is centered on the rotation center, so
inside-disk pixels always gather inside the frame:

.. code-block:: c
   :caption: components/moon/core/moon_render.c (moon_rotate_range)

   for (int y = y0; y < y1; y++) {
       int dy = y - cy;
       uint16_t *row = &out[y * size];
       for (int x0 = 0; x0 < size; x0 += 4) {
           int n = size - x0; if (n > 4) n = 4;
           int dx = x0 - cx;
           int32_t fx = (int32_t)dx * c15 + (int32_t)dy * s15;
           int32_t fy = (int32_t)dy * c15 - (int32_t)dx * s15;
           for (int k = 0; k < n; k++) {
               /* Inverse rotation (gather): p = R(-theta) s, Q15.15.
                * |dx|, |dy| < 234, max |fx| ~ 1.6e9 fits int32. */
               int32_t fx15 = fx >> 15; /* arithmetic shift = floor */
               int32_t fy15 = fy >> 15;
               int ix = f_cx + (int)fx15 + ((fx & 32767) > 16383 ? 1 : 0); /* round half up */
               int iy = f_cy + (int)fy15 + ((fy & 32767) > 16383 ? 1 : 0);
               uint8_t gval = 0;
               if ((unsigned)ix < (unsigned)frame_size && (unsigned)iy < (unsigned)frame_size)
                   gval = frame[iy * frame_size + ix];
               row[x0 + k] = g565[gval];
               fx += c15;   /* incremental: exact integer math */
               fy -= s15;
           }
       }
   }

Four properties make this function the performance anchor of the
whole spin (a full 400×400 gather is **~8 ms** on the S3):

1. **Incremental Q15.15** — :math:`f_x, f_y` update by add/subtract
   per pixel (``fx += c15; fy -= s15``), exact integer math, no
   multiply in the inner loop.
2. **4-px unroll** — up to 4 PSRAM source loads are in flight per
   iteration; the gather is bandwidth-bound, not latency-bound.
   (The relief cache build, Chapter 11, is the same lesson at 4
   million cells.)
3. **Fused LUT** — the gathered byte goes straight through
   ``g565[gval]`` in the same instruction stream; the gray→565 step
   costs nothing extra.
4. **Unsigned range test** — ``ix < frame_size`` with unsigned
   :math:`ix` turns "out of frame → black" into a single comparison
   per axis; out-of-frame pixels read ``gval = 0`` (the LUT's bin 0
   is 0), so the disk edge falls off to black with no mask.

``moon_rotate`` runs the whole square; ``moon_rotate_rows(y0, y1)``
runs a row band — the orbit sweep publishes converted rows as they
finish (Chapter 14) and displays them strip by strip under the same
atomic rule.

The gray→RGB565 LUT
-------------------

The panel is RGB565 (5-6-5, no alpha — the CO5300's only useful mode).
The core outputs gray, so the conversion is a 256-entry LUT built
once. The neutral build is round-to-nearest:

.. code-block:: c
   :caption: components/moon/core/moon_render.c (the LUT)

   static void g565_build(void) {
       for (int g = 0; g < 256; g++) {
           int r5 = (g * 31 + 127) / 255;   /* 5 bits */
           int g6 = (g * 63 + 127) / 255;   /* 6 bits — full 6-bit green */
           int b5 = r5;
           if (g565_warm) {
               r5 = (g * 32 + 127) / 255;   /* R x1.038, capped at 31 */
               if (r5 > 31) r5 = 31;
               b5 = (g * 29 + 127) / 255;   /* B x0.933, capped at 31 */
               if (b5 > 31) b5 = 31;
           }
           g565[g] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
       }
       g565_init = 1;
   }

Two comments in the source are worth quoting because they record real
bugs this LUT exists to prevent:

- *"Using the full 6-bit green is what keeps the gray balanced —
  writing the 5-bit value into the 6-bit green field caps green at
  31/63 (~49%) and renders every gray as magenta/pink on the panel."*
- The warm multipliers (R ×1.038, B ×0.933) are calibrated to the
  reference frames' measured lit-region balance (Chapter 8):
  ``moon_gray565_set_warm(0)`` rebuilds the neutral LUT, and the gate
  runs the host simulator with ``--neutral`` so the tint never enters
  a validation number.

Byte order: the LUT emits **LVGL-native** RGB565 (MSB = red, as LVGL
expects in an ``LV_COLOR_FORMAT_RGB565`` draw buffer). The panel's own
byte order (the QSPI io sends 16-bit pixels MSB-first, which is the
*swapped* order) is handled at the panel boundary, not in the core —
the spin's direct write byte-swaps on the way out (Chapter 13), and
the LVGL flush path is pre-swapped by the adapter.

Where these run on the device
-----------------------------

- ``moon_rotate`` — the spin's render task (core 1), once per spin
  frame (~8 ms of the 125 ms frame budget at 8 fps; the panel write is
  the rest, Chapter 13).
- ``moon_rotate_rows`` — the orbit sweep's convert task (rows
  published to the LVGL tick's strip drain).
- ``g565`` — fused into the gather (free) and used by the static
  convert (the wake's one-time gray→565 of the current phase).

The core is done. The remaining question — the one that defines the
device's character — is how a finished frame gets onto a panel whose
DMA path can fit 32 KiB at a time. That is the display pipeline:
Chapter 13.
