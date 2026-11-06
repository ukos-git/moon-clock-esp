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

The three changes
-----------------

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

The gating is deliberate: the blend runs **only** in the display bake
(:math:`dark_zero = 1` *and* :math:`dk_sup = 0`). The gate
(:math:`dark_zero = 0`) and the dusk (:math:`dk_sup > 0`) both fall
through to the original hard path, so the validation numbers and the
sleep-screen endpoint are untouched by the soft edge.

The warm tint
-------------

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
