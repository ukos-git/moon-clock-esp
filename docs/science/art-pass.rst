The artistic pass
=================

The lit side was validated to match NASA. The dark side was a measured,
*correct* choice — and then the product direction came down:

> *"Strictly: we want to be better than NASA images, since we are in a
> simulation. Better, not worse!"*
>
> *"Better, not realistically better, but better from an artistic point
> of view."*

This page documents the pass that followed. It is a **display-only**
pass: the gate-validated physics is untouched (the host gate is run with
a ``--neutral`` flag so none of the artistic constants enter the
validation numbers — the pass is byte-identical to the pre-pass run),
while the display's bake gains higher dark-side contrast, a soft
terminator, and a warm tint calibrated to the measured color balance of
the reference frames.

The short version
-----------------

The previous page ended with a dark side that was *correct*. This page
is the one place in the project that deliberately asks for more than
correct: the user's standing direction — quoted above — is "better
than NASA", and what came out of it is exactly three small changes to
what the panel shows:

1. **Brighten the night half.** The computed dark side was right but
   visibly too dim — about half as visible as the reference frames'
   dark side, which reads as a silhouette rather than a moon in
   shadow. One constant doubled its level, measured against the
   references rather than eyeballed.
2. **Soften the day/night line.** The model switched from night to day
   within a single pixel — a razor edge no moon photo has. A narrow
   blend band at the line now fades one value into the other.
3. **Warm the colors a half-step.** The reference frames read slightly
   warm — a touch more red than blue. The renderer was neutral; the
   panel's final gray-to-color conversion now carries that same
   measured warmth instead.

The safety net is the design's spine: all three changes live in the
*display* path only, and the image gate runs without them (its
``--neutral`` mode), so every physics number on the rest of the site is
provably untouched — the page closes with that proof. Everything the
three changes touch — the backlit surface, the tone LUT, the gray→RGB565
step — is built on :doc:`the dark side <darkside>` and :doc:`the
lighting model <lighting>`.

Vocabulary
----------

Ten terms carry the page (the full definitions live in the
:doc:`glossary <../glossary>`):

- **Artistic pass** — this page's three display-only changes (the
  dark-side level, the terminator softness, the warm tint),
  deliberately outside the gate's physics path.
- **Image gate** — the host check that scores the simulator's renders
  against NASA's reference frames (:doc:`the image gate
  <../verification/image-gate>`); the arbiter of *physics*.
- **``--neutral``** — the gate's run mode: neutral LUT, physics dark
  side, so no artistic constant ever enters a validation number.
- **Byte-identical** — producing the same output bytes as a previous
  run; the strongest available proof that the pass cannot affect the
  gate.
- **Backlit / K** — the previous page's model and its single dimming
  constant ``MOON_NIGHT_BACKLIT_K``; this pass moves exactly that one
  number (0.10 → 0.20).
- **Mean level** — the average gray value over a region: the "how
  visible" of a surface, and the measurement that settled the K
  decision.
- **Terminator** — the day/night boundary line on the visible disk.
- **Smoothstep** — the S-shaped blend curve: pinned at 0 below the
  band and 1 above it, eased through the middle by 3t² − 2t³ so it
  joins the flat ends without a kink.
- **Warm tint** — color biased toward red/yellow instead of neutral
  gray; applied here only at the pipeline's last step, never in the
  lighting.
- **RGB565** — the panel's 16-bit color format: 5 red bits, 6 green,
  5 blue. The pipeline's last step maps each gray byte to one of these
  words.

The three changes
-----------------

**The short version.** The same three decisions in their exact units:
two constants inside the bake, one character change inside the final
gray→RGB565 table. Each gets its own section below — this is the
one-screen version.

1. **The dark-side level doubles** — ``MOON_NIGHT_BACKLIT_K`` goes
   0.10 → **0.20** (peak gray 25 → ~51), calibrated so the dark side's
   mean level matches the reference frames' rendered dark side instead
   of sitting at about half of it.
2. **A soft terminator** — a narrow smoothstep band at
   :math:`lns = 0` blends the backlit night into the lit side, replacing
   the hard 0→LUT-floor step that read as a cutout edge.
3. **A warm tint** — the gray→RGB565 LUT's red channel is lifted
   (×1.038) and blue lowered (×0.933), calibrated to the measured
   red/blue balance of the reference frames' lit regions.

All three are gated off of the validation path. The display is *better
than the reference* by construction and by measurement — and the physics
that earns the gate is provably unchanged.

Why the dark side was too dark
------------------------------

**The short version.** Nothing here was eyeballed. Two moons are
compared by one number — the *mean level*, the average gray of the
unlit half — and at the old constant the dark side sat at about half
the reference frames' value, which reads as a silhouette instead of a
moon in shadow. The fix doubles one constant; the table below is the
calibration measurement, sim against reference at four moon phases.

The computed backlit dark side (:doc:`the dark side <darkside>`) is *correct* — but
"correct" at :math:`K = 0.10` meant the unlit surface peaked at gray
~25, with a mean in a waxing-crescent frame of about **4.4**. The
reference frames' rendered dark side sits at a mean of about **8–9** in
the same geometry: roughly *twice* as visible. A dark side at half the
reference's level reads as a rendering artifact (a silhouette); at the
reference's level it reads as a moon that happens to be in shadow.

The fix is one constant, and the calibration is a single honest
measurement — the dark-side mean of the sim vs the reference, at
matched phases, in neutral gray:

.. list-table::
   :header-rows: 1
   :widths: 22 22 22 22

   * - Gate frame
     - phase (illum)
     - sim dark mean
     - NASA dark mean
   * - 715
     - ~0.08 (crescent)
     - 8.0
     - 8.9
   * - 2259
     - ~0.25
     - 4.1
     - 4.9
   * - 4420
     - ~0.50
     - 2.9
     - 3.5
   * - 1817
     - ~0.90
     - 1.0
     - 1.5

Dark-side mean (8-bit gray), sim (K = 0.20) vs NASA, at four matched
gate frames. The sim now sits at ~90% of the reference's dark-side
level across the phase range — "better, not worse", and no longer half
as visible.

.. figure:: ../_static/figs/k_ab.png
   :alt: The backlit dark-side level K, A/B at a waxing crescent
   :figclass: align-center

   The backlit dark-side level ``K``, A/B at a waxing crescent (the
   simulator's own bake, warm tint). Top-left K=0.05 (silhouette),
   top-right K=0.10 (the pre-pass value), bottom-left **K=0.20 (the
   display value)** — the maria and highlands resolve — bottom-right
   K=0.35 (too loud; the dark side starts to compete with the lit side).

The soft terminator
-------------------

**The short version.** The hard model makes the day/night line a
cliff: one pixel the value is the night side's, the next it jumps to
the *dimmest value the lit side is allowed to have* — a cutout edge.
Real moon photos carry a few-pixel ramp there instead. The fix computes
*both* values for every pixel near the line and blends them along an
S-curve; a little way off the line, the blend degenerates to the old,
unmodified values.

At the exact terminator (:math:`lns = 0`), the hard model steps from the
backlit night value straight to the LUT's lit-side floor. On a real moon
photo the terminator is a soft ramp — the Sun's angular diameter,
atmosphere-free scattering in the reference's pipeline, and the
relief-blur all land within a few pixels. The pass blends the two with a
narrow **smoothstep** over a band of width ``MOON_TERM_SOFT`` (0.03
:math:`lns` units ≈ 6 px each side at the 400 px disk) centered on
:math:`lns = 0`:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the soft-terminator blend)

   /* Soft terminator (display only: dark_zero = 1, dk_sup = 0). Blend
    * the lit value and the backlit night value across a narrow
    * smoothstep band at lns = 0. Far lit (lns >= band) and far dark
    * (lns <= -band) reduce to the unmodified values; the dusk
    * (dk_sup > 0) and the gate (dark_zero = 0) fall through to the
    * original path. */
   if (c->dark_zero && c->dk_sup == 0.0f) {
       int idx = (int)(A * L + 0.5f);
       if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
       float litv  = MOON_TONE_LUT[idx] + earthshine(d);
       float darkv = (float)moon_backlit_night(A, lns);
       float t = lns <= -MOON_TERM_SOFT  ? 0.0f
                 : lns >= MOON_TERM_SOFT ? 1.0f
                                         : (lns + MOON_TERM_SOFT) / (2.0f * MOON_TERM_SOFT);
       t = t * t * (3.0f - 2.0f * t);          /* smoothstep */
       float out = darkv + (litv - darkv) * t;
       c->row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
       return;
   }

What those lines do, one by one: the opening ``if`` is the display-only
gate — the blend exists *only* in the bake that ships (backlit *and* no
dusk bound), and everyone else falls through to the original hard path
below it. ``idx`` is the lit side's raw brightness (paint times
lighting) rounded to a table slot and clamped into the LUT's 0–255
range; ``litv`` is then what a *lit* pixel would show — the tone LUT's
entry plus the faint earthshine. ``darkv`` is what a *night* pixel
would show: the previous page's backlit function. ``t`` is the position
across the blend band — pinned to 0 on the far night side, 1 on the far
lit side, and between them the fraction of the way from one edge to the
other. The next line bends that fraction into the S-curve, and
``darkv + (litv - darkv) * t`` is the weighted average the S-curve
drives: all night at t = 0, all lit at t = 1, a smooth mix in between.
The last line rounds to a byte like every other write in the bake, and
``return`` skips the hard path entirely.

The gating is deliberate: the blend runs **only** in the display bake
(:math:`dark_zero = 1` *and* :math:`dk_sup = 0`). The gate
(:math:`dark_zero = 0`) and the dusk's map path
(:math:`dk_sup > 0` — retired with the dusk 2026-11-13, the
parameter kept in the portable core) both fell through to the
original hard path, so the validation numbers and the — then live —
sleep-screen endpoint stayed untouched by the soft edge.

The warm tint
-------------

**The short version.** The reference frames are measurably warm —
slightly more red than blue — and that warmth is a *camera* choice, not
moon physics: a physically rendered moon is gray. So the pass does not
touch the lighting at all; it biases the pipeline's very last step,
where each gray byte becomes a panel color word, and calibrates the
bias against the reference's measured balance.

The reference frames are not neutral gray. Their lit regions carry a
measured warm balance — red above green above blue — a "yellow taint"
that is a **post-processing choice** in the reference pipeline, not
physics (the Moon's regolith is a neutral-to-slightly-blue reflector; a
physically rendered moon would be neutral). The question the pass had to
answer was whether to simulate that taint at all, and if so, at what
level.

The answer: *yes, calibrated*. The device's final step is a
gray→RGB565 LUT (:doc:`rotation and the LUT <../implementation/rotate-lut>`), and the pass gives that LUT a warm
character — red lifted, blue lowered — matched to the measured red/blue
balance of the reference's lit regions (~1.07):

.. code-block:: c
   :caption: components/moon/core/moon_render.c (the warm gray->565 LUT)

   static void g565_build(void) {
       for (int g = 0; g < 256; g++) {
           int r5 = (g * 31 + 127) / 255;      /* neutral: 5 bits */
           int g6 = (g * 63 + 127) / 255;      /* 6 bits          */
           int b5 = r5;
           if (g565_warm) {
               r5 = (g * 32 + 127) / 255;      /* R x1.038, capped at 5 bits */
               if (r5 > 31) r5 = 31;
               b5 = (g * 29 + 127) / 255;      /* B x0.933, capped at 5 bits */
               if (b5 > 31) b5 = 31;
           }
           g565[g] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
       }
       g565_init = 1;
   }

   void moon_gray565_set_warm(int on) {
       if (on != g565_warm) { g565_warm = on; g565_build(); }
   }

What those lines do, one by one: the loop runs once over all 256 gray
levels — the table is built once and then read once per pixel.
``r5``/``g6``/``b5`` are the neutral conversion: gray ``g`` scaled into
the panel's three bit-fields, with the ``+ 127`` before the ``/ 255``
being round-to-nearest for integer division. Green uses all six of its
bits, and a comment in ``moon_render.c`` warns why that matters:
writing the 5-bit value into the 6-bit green field caps green at about
half and renders every gray magenta/pink on the panel. The
``if (g565_warm)`` branch is the pass itself: the red multiplier moves
from 31 to 32 (the ×1.038) and blue from 31 to 29 (the ×0.933), each
capped at the 5-bit maximum so the brightest grays saturate red rather
than overflow the field. The pack line then drops the three fields at
their RGB565 offsets — red in the top 5 bits, green in the middle 6,
blue in the low 5. Below the loop, ``moon_gray565_set_warm`` is the
switch: flipping it rebuilds the table, and the gate's ``--neutral``
path is what calls it with 0.

Measured on a full-ish frame through this LUT, the lit-region red/blue
balance is **1.104** (R−G +4.2, G−B +7.9), bracketing the reference's
~1.07 — warm enough to read as the reference's moon, not so warm that it
reads as a tungsten lamp. The neutral LUT is retained behind
``moon_gray565_set_warm(0)``; the gate runs the host simulator with
``--neutral`` so the tint never enters a validation number.

.. figure:: ../_static/figs/tint_ab.png
   :alt: Neutral vs warm gray-to-RGB565 LUT, A/B at a full-ish phase
   :figclass: align-center

   The gray→RGB565 LUT, A/B at a full-ish phase. Left: neutral (the
   gate's choice). Right: **warm (the display's choice)** — the lit
   region reads a half-step warmer, matching the reference frames'
   measured color balance.

.. figure:: ../_static/figs/art_ab.png
   :alt: The full artistic pass, A/B
   :figclass: align-center

   The full artistic pass, A/B (waxing crescent). Top-left: earthshine
   dark side, neutral LUT (the pre-pass, gate-validated look). Top-right:
   earthshine, warm. Bottom-left: backlit dark side, neutral. **Bottom
   right: backlit + soft terminator + warm LUT — the shipped display
   look.** Each step is an isolated constant; the physics underneath is
   the same bake.

The gate stays byte-identical
-----------------------------

**The short version.** "The physics didn't change" has to be more than
a promise — so it is checked the hardest way possible: run the gate
exactly as before and its output is the *same bytes* as before the
pass, the same PASS line with the same MAE and SSIM numbers. All three
constants sit behind switches the gate never flips, so the validation
literally cannot observe them.

Because every artistic constant is display-gated, the image gate (run
with ``--neutral``) produces **byte-identical** output to the pre-pass
run — the same PASS with the same numbers:

.. code-block:: text
   :caption: Host image gate, post art pass (--neutral, dark_zero = 0)

   == C-simulator gate (32-frame set) ==
     C vs NASA  : mean MAE 6.68 (max 8.90, 0 frames > 12)  mean SSIM 0.908
     C vs Python: mean MAE 1.03 (max 1.31, 0 frames > 2)
     gate: C-NASA per-frame MAE <= 12  AND  C-Python per-frame MAE <= 2
     ->  PASS

The pass is *provably invisible to the validation*: the dark-side
level, the terminator softness, and the tint are all outside the
gate's path. The lit-side physics — the part the gate measures — is
untouched, and the "better than NASA" claim is a display decision the
gate neither certifies nor forbids. That separation is the point of the
two-constant design: **physics is validated, art is measured.**

What the pass is not
--------------------

**The short version.** Three ways to misread the page, each worth
closing off: this is not new lighting physics, not a filter painted on
afterwards, and not part of what the gate certifies.

- It is not a different lighting model. The displaced-normal Lambert,
  the relief gradient, and the albedo are exactly the validated ones.
- It is not a filter applied after the fact. The constants sit *inside*
  the bake (the dark-side level and terminator) and inside the final
  LUT (the tint), so the device — not a post-processor — produces the
  look.
- It is not free of the gate. The soft-terminator band *does* move a
  small number of pixels across the 27-gray LUT-floor boundary at the
  edge, and the gate-validated path is the one that stays byte-identical
  — the display path is where the band lives, by the
  :math:`dark_zero \land dk_sup = 0` gate above.

The shipped look — backlit dark side at the reference's level, a soft
terminator, and a warm tint calibrated to the reference's measured
balance — is what the next page lets you drive interactively.
