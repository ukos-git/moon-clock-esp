Rotation and the gray→RGB565 LUT
================================

Two small functions close the core: the **in-plane rotation**
("ball-in-hand" free spin) and the **gray→RGB565** conversion. Both
were latency-optimized for the frame rate of the burn-in spin, which
displayed 8 fps off a once-baked frame; the spin was removed
2026-11-13, and with it the rotation's only device caller — what
remains on the device is the LUT (every displayed frame goes through
``moon_gray565``) and the core's rotation API, used verbatim by
``host_sim`` (and kept, shared, in ``components/moon/core``). Both are
in ``components/moon/core/moon_render.c`` together with the
fixed-point table in ``moon_fixp.c``.

The short version
-----------------

Everything so far produced a square of gray brightness bytes; this
page is the core's last mile — the two steps between that square and
a lit panel, and both are dictionary look-ups rather than math. Four
answers cover it:

1. **The rotation turns a photograph, not a globe.** In-plane
   rotation spins the finished picture about the axis sticking out
   of the screen — exactly "the moon disk turning under your thumb",
   and exactly nothing more (a real 3-D turn would need a re-bake).
   The product that wanted it — the burn-in spin — was retired
   2026-11-13; the function stays, verbatim, in the shared core
   because the host tools still drive it.
2. **Bake once, turn cheaply.** Baking costs ~1.3 s; a display frame
   had 125 ms in the spin era. So the phase is computed once and the
   *result* is rotated every frame: a nearest-neighbor gather whose
   full 400×400 sweep costs ~8 ms on the S3.
3. **One byte read per pixel, integers only.** The gather works
   backwards — each destination pixel *pulls* its source byte (no
   holes, no averaging) — and per frame it needs two integers, the
   cosine and sine, from a lazily built fixed-point table. The inner
   loop is adds, shifts, and one range test: no multiply, no float.
4. **Color is a 256-entry dictionary.** Two hundred fifty-six
   possible grays, one panel format: the gray→RGB565 conversion is
   precomputed once and fused into the gather's instruction stream,
   carrying the art pass's warm tint by default — with two source
   comments recording the real bug it exists to prevent (every gray
   rendered pink).

Vocabulary
----------

Thirteen terms carry the page — the retired spin and the warm tint
also have fuller entries in the :doc:`glossary <../glossary>`:

- **In-plane rotation** — turning a flat picture around the axis
  perpendicular to the screen: every pixel stays the same distance
  from the center, nothing new comes into view. A true 3-D turn (a
  limb sliding into sight) is a different operation — a re-bake.
- **Ball-in-hand** — the product name for the effect: the baked
  moon turning under the viewer's hand the way a physical model
  does.
- **Spin** *(glossary)* — the retired (2026-11-13) device feature
  that called the rotation at 8 fps to fight AMOLED burn-in; the
  only device caller this file ever had.
- **Gather (inverse mapping)** — building an image destination-first:
  each output pixel asks "which input pixel do you want?" and reads
  exactly one. Its opposite, *scatter*, pushes each source pixel
  forward and must then fix the holes left behind — the gather has
  no holes by construction.
- **Nearest-neighbor** — sampling that takes the single closest
  input pixel instead of blending the four around it: a third of
  the work, and at these sizes nothing to see.
- **Fixed-point (Q15.15)** — writing a fraction as an integer with
  15 bits below the binary point (1.0 is the integer 32768):
  multiplying by a cosine becomes one integer multiply and one
  shift, and the running sums stay exact (the same trick the bake's
  tables use, :doc:`the C core <core-c>`).
- **Quarter table / quadrant fold** — store a curve only on the
  quarter turn where it is simple, and answer every other angle
  with mirrors and sign flips: one table, full circle.
- **PSRAM** — the boards' off-chip RAM: it holds the big buffers
  (frames included) at the price of slower reads than internal RAM,
  which is why the gather's load pattern is a design point, not an
  accident.
- **Bandwidth-bound** — limited by how many bytes per second the
  memory delivers rather than by waiting for each read in turn;
  keeping several loads in flight (the 4-px unroll below) converts
  the waiting into throughput, and throughput is where this loop
  comfortably sits.
- **RGB565** — the panel's 16-bit color format: 5 bits red, 6
  green, 5 blue; the human eye reads green most finely, so green
  gets the extra bit — and the LUT's quoted comments below show
  what happens if you forget that the field really is 6 bits wide.
- **Byte order (MSB-first)** — which half of a 16-bit pixel travels
  the wire first; LVGL and this panel's QSPI io disagree, the
  adapter swaps at the boundary, and the core therefore only ever
  speaks LVGL-native.
- **Round-to-nearest (integer idiom)** — add half a step before
  truncating division: ``(g * 31 + 127) / 255`` scales to a 5-bit
  field, and the pre-added half does the rounding the integer
  division cannot.
- **Warm / neutral LUT** — the two builds of the same dictionary:
  warm ships (the lit side tinted like the reference frames,
  :doc:`the artistic pass <../science/art-pass>`), neutral is what
  the image gate validates against, so no display choice enters a
  validation number.

Why a rotation at all
---------------------

**The short version.** One number decides this page: 1.3 s. That is
what producing one moon frame costs on the S3, against a 125 ms
display budget in the spin era — so the picture can never be
*re-made* to follow a turning hand. It can, however, be *turned*:
compute the phase once, then present a rotated copy of that one
result whenever you like. The copy models a printed disk spinning
under your thumb exactly, and models a ball turning in 3-D not at
all — seeing around the limb means asking the expensive question
again, which is what the orbit pipeline does when a real tilt
demands it (:doc:`the animation pipelines <animations>`).

The bake renders one fixed phase per frame — baking is ~1.3 s on the
S3 board (the Waveshare ESP32-S3-Touch-AMOLED-1.75), so it cannot
follow a hand-held rotation. The product decision
was: the *baked* moon should still turn under the viewer's hand the
way a physical model does. The solution is to bake the phase **once**,
then rotate the baked frame in-plane every display frame with a cheap
nearest-neighbor gather. The in-plane rotation is exact for what it
models (turning the visible disk about the screen normal) and wrong
for what it is not (a true 3-D pose would need a re-bake — which is
what the *orbit* pipeline does when the tilt demands it, :doc:`the animation pipelines <animations>`).
That product decision outlived its justification and was retired
2026-11-13 ("It does more things worse than good") — the gather and
its rationale stay in the shared core, exercised by the host tools,
while the orbit remains the live tilt pipeline.

The fixed-point table
---------------------

**The short version.** Turning a picture takes two numbers per call
— the cosine and sine of the current angle — and the chip pays
software prices for trigonometry. The core's answer is the one the
bake gives its own slow math (:doc:`the C core <core-c>`):
precompute. One small static table holds samples of a single quarter
turn as Q15.15 integers; every other angle is folded into that
quarter, mirrored and re-signed; and cosine is simply sine a quarter
turn ahead. The table builds itself on first use, is read twice per
frame, and after that the gather never calls the math library.

The gather needs :math:`\cos\theta` and :math:`\sin\theta` per frame
(the spin era needed them eight times a second). ``moon_fixp.c`` provides them as
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

Reading the table: ``fixp_build`` walks the quarter turn in even
steps, takes the real sine of each step once — in double, once per
run, where the cost is irrelevant — and scales it by 32768 into an
integer. ``red16`` folds any angle, positive or negative, into one
full circle of table steps; the quadrant unpacking then mirrors the
answer: the second quadrant is the table walked backwards, the
third and fourth negate it. ``moon_cos15`` is a one-liner because
cosine *is* sine advanced by a quarter turn. And the values
deliberately live in 32-bit variables: 32768 — cosine's maximum —
is one too many for a signed 16-bit one, and wrapping it to −32768
would mirror the disk outright (the source header of
``moon_fixp.c`` records exactly that).

The table is 1025 int32 entries (8 KB) covering [0, π/2]; the
four-quadrant fold (the ``red16`` reduction) covers the full circle.
Linear interpolation between entries gives ~18 bits of angular
precision — far smaller than one pixel of rotation per frame.

The gather
----------

**The short version.** The rotation works backwards. Rather than
pushing each source pixel where it lands — scatter, which leaves
holes where nothing landed and double-writes where two did — the
function stands at each destination pixel, asks which source pixel
it wants, and reads exactly that one byte. The asking costs one
rotation formula paid once per group of four pixels; every pixel
after the group's first just steps one fixed increment further
along the same line. The four properties below are what squeezed
400×400 into ~8 ms.

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

Reading the loop, line by line:

- ``dy = y - cy`` and ``dx = x0 - cx`` — coordinates measured from
  the rotation center: the rotation formula only means anything
  around that point.
- ``fx = dx * c15 + dy * s15`` and ``fy = dy * c15 - dx * s15`` —
  the rotation itself, in Q15.15, run *backwards*: they answer
  "which source point lands here", the inverse map that makes this
  a gather. The four multiplies are paid once per group of four
  pixels, not per pixel.
- ``fx >> 15`` — Q15.15 back to whole pixels; the arithmetic shift
  rounds toward minus infinity (floor).
- ``((fx & 32767) > 16383 ? 1 : 0)`` — the discarded fraction (the
  low 15 bits): if it exceeds half a pixel, step one further. Floor
  plus this test is round-to-nearest — the *nearest-neighbor*
  sample, decided with a bit-and and a compare.
- ``f_cx +`` … — the source frame's own center: destination and
  source centers are separate arguments, which is how the host
  tools draw a rotated frame into a differently centered canvas.
- ``if ((unsigned)ix < (unsigned)frame_size && ...)`` — the whole
  in-bounds test in two comparisons: casting a negative to unsigned
  turns it into a huge number, so one ``<`` catches both the
  below-zero and above-the-edge cases. Out-of-frame leaves
  ``gval = 0`` — black, no mask needed (property 4 below).
- ``row[x0 + k] = g565[gval]`` — the gathered gray becomes a panel
  color in the same breath: the LUT read is fused into the gather
  (property 3 below).
- ``fx += c15; fy -= s15`` — why no multiply appears again: one
  pixel over in the destination is one fixed rotation-step over in
  the source, so the next source address is an addition away — and
  integer additions are exact, so no rounding error can drift along
  the row (the comment's "exact integer math").

.. figure:: ../_static/figs/rotate_gather.png
   :alt: The gather standing at destination pixels pulling source bytes through the inverse map, and the stored quarter turn folded into the full circle
   :align: center

   The two lookups, drawn (``docs/tools/gen_rotate_gather.py``).
   Left: the dashed square is the baked source frame turned
   −θ, the solid blue square the destination — each blue dot is a
   destination pixel, the amber dot the source byte it names
   through ``R(−θ)``; the hollow four are one unroll group, whose
   members are one ``fx += c15`` / ``fy −= s15`` pair apart.
   Right: the lazily built ``moon_fixp`` table — one quarter turn
   stored (1024 entries, 8 KB), the remaining three quadrants
   folded in by mirror and sign flip, Q15.15 so ``cos 0`` is the
   integer 32768.

Four properties make this function the performance anchor of the
rotation (a full 400×400 gather is **~8 ms** on the S3 — the number
that made the spin's 125 ms frame budget work):

1. **Incremental Q15.15** — :math:`f_x, f_y` update by add/subtract
   per pixel (``fx += c15; fy -= s15``), exact integer math, no
   multiply in the inner loop.
2. **4-px unroll** — up to 4 PSRAM source loads are in flight per
   iteration; the gather is bandwidth-bound, not latency-bound.
   (The relief cache build, :doc:`the C core <core-c>`, is the same lesson at 4
   million cells.)
3. **Fused LUT** — the gathered byte goes straight through
   ``g565[gval]`` in the same instruction stream; the gray→565 step
   costs nothing extra.
4. **Unsigned range test** — ``ix < frame_size`` with unsigned
   :math:`ix` turns "out of frame → black" into a single comparison
   per axis; out-of-frame pixels read ``gval = 0`` (the LUT's bin 0
   is 0), so the disk edge falls off to black with no mask.

``moon_rotate`` runs the whole square; ``moon_rotate_rows(y0, y1)``
is the same operation on a row band. Neither runs on the device
today: the spin — the only device caller — was retired 2026-11-13,
and the row-publishing sweeps never gathered at all (the orbit bakes
on a basis pre-rotated by ``moon_orbit_geometry`` and publishes
converted rows through ``moon_bake_rows``,
:doc:`the animation pipelines <animations>`). They stay in the shared
core, verbatim — ``host_sim`` and the bench drive ``moon_rotate``.

The gray→RGB565 LUT
-------------------

**The short version.** The core speaks gray; the panel speaks
16-bit color. Converting pixel by pixel with multiplies would spend
real time answering a question that has only 256 possible answers —
so all 256 are computed once into a dictionary and the per-pixel
conversion is one array read. The dictionary ships *warm* (the lit
side tinted toward the reference frames' measured balance) and
rebuilds on request; the block below is its entire build cost.

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

Reading the build:

- ``int r5 = (g * 31 + 127) / 255;`` — scale gray's 0–255 range to
  the red field's 0–31 range: multiply by 31, and pre-add half the
  divisor so the truncating integer division lands round-to-nearest
  instead of always down. The ``g6`` line is the same idiom for
  green's 0–63 field.
- ``int b5 = r5;`` — in the neutral build blue copies red exactly:
  equal channels are what "gray" means, so neutrality is
  structural, not tuned.
- The warm branch re-runs the idiom with 32 for red and 29 for blue
  where neutral used 31 — the ×1.038 / ×0.933 multipliers lifted
  and lowered around the neutral value — and the ``> 31`` caps keep
  the lifted top grays from overflowing their fields.
- ``g565[g] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);`` — pack
  the three fields into one 16-bit pixel: the shifts slide each
  field to its slot (red bits 15–11, green 10–5, blue 4–0) and the
  bitwise-or writes them there together. The layout is the panel's
  format made literal — which is exactly why the pink-gray bug
  quoted below is invisible in the source and obvious on the panel.

Two comments in the source are worth quoting because they record real
bugs this LUT exists to prevent:

- *"Using the full 6-bit green is what keeps the gray balanced —
  writing the 5-bit value into the 6-bit green field caps green at
  31/63 (~49%) and renders every gray as magenta/pink on the panel."*
- The warm multipliers (R ×1.038, B ×0.933) are calibrated to the
  reference frames' measured lit-region balance (:doc:`the artistic pass <../science/art-pass>`):
  ``moon_gray565_set_warm(0)`` rebuilds the neutral LUT, and the gate
  runs the host simulator with ``--neutral`` so the tint never enters
  a validation number.

Byte order: the LUT emits **LVGL-native** RGB565 (MSB = red, as LVGL
expects in an ``LV_COLOR_FORMAT_RGB565`` draw buffer). The panel's own
byte order (the QSPI io sends 16-bit pixels MSB-first, which is the
*swapped* order) is handled at the panel boundary, not in the core —
the spin era carried a byte-swap inline on its direct writes
(:doc:`the display pipeline <display>`, retired with the spin), and
the LVGL flush path is pre-swapped by the adapter.

Where these run on the device
-----------------------------

**The short version.** The file's two halves live different lives on
the hardware: the rotation has no device caller left — its only
one, the spin, was removed 2026-11-13, and the host tools drive it
verbatim — while the LUT is unavoidable, sitting in every path that
reaches a panel. Only the first bullet invites "why keep dead
code?" and it has an answer: the core is shared byte-for-byte
between device and host, and the host is where the rotation's
guarantees are tested.

- ``moon_rotate`` / ``moon_rotate_rows`` — **no device caller** since
  the spin's removal (2026-11-13); the host tools
  (``host_sim/main.c``, ``host_sim/bench.c``) drive the whole-frame
  variant verbatim.
- ``g565`` — the live path, everywhere: fused into the host gather
  (free) and, on the device, the static convert
  (``moon_convert_to_rgb``), the orbit's per-row convert, the TF
  cache's replay tint (``moon_tfcache`` runs the fetched frame
  through the *same* LUT as a computed one) and the knob's companion
  (``knob_moon``).

The core is done. The remaining question — the one that defines the
device's character — is how a finished frame gets onto a panel whose
DMA path can fit 32 KiB at a time. That is the display pipeline:
:doc:`The display pipeline <display>`.
