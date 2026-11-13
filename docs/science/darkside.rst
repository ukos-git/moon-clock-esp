The dark side
=============

The lit side has a validated model. The dark side is where the project
made its most deliberate *product* decisions — and the single most
visible one. This page covers the two dark-side models, why the
display uses the computed backlit surface instead of a black overlay or
a flat floor, the anti-solar lighting that makes crater rims readable in
the dark, and the ``dk_sup`` sliver-kill that keeps the sleep screen
from showing a phantom bright rim.

Two models, one flag
--------------------

The bake's ``dark_zero`` flag selects which dark side is written:

- ``dark_zero = 0`` — the **earthshine** model (:doc:`the lighting model <lighting>`): the dark
  side is a faint phase-independent gradient. This is the *validated*
  model — it is what the image gate runs, and
  it is byte-identical to the original pre-backlit bake for every
  :math:`d`.
- ``dark_zero = 1`` — the **computed backlit** surface: the dark side is
  the actual lunar surface (albedo *and* relief) lit from the
  anti-solar direction at a faint level. This is the *display* model —
  what the device shows.

The flag is a per-caller parameter, not a global: the same ``moon_bake``
serves both the gate (physics) and the display (product), and nothing
else changes between the two paths.

Why not a black overlay
-----------------------

The obvious dark side is a flat black disk behind the lit crescent. The
project's first version used a constant floor (``MOON_NIGHT_FLOOR``) —
a single gray level painted over everything the Sun didn't light. It had
two failures:

1. **It was not the surface.** A flat floor has no maria, no crater
   rims, no relief — the dark side looked like a silhouette, not a
   moon.
2. **It read as an artifact.** Against the lit side's real texture, a
   dead-black half-disk looked like a rendering bug, not the Moon.

The user's requirement (and the design goal it drove): *"the
unilluminated side is fully calculated — no black overlay, no flat
floor."* The backlit model is the direct answer: render the same
surface you render on the lit side, just lit from the opposite
direction.

The anti-solar Lambert
----------------------

The backlit surface is lit from the **anti-solar** direction
(:math:`-\mathbf{S}`). A surface point's backlit Lambert is the mirror
of its solar Lambert:

.. math::

   L_{\text{anti}} = \mathrm{clip}(-lns,\; 0,\; 1)

where :math:`lns` is the *solar* displaced-normal Lambert from :doc:`the lighting model <lighting>`.
This is not a coincidence — it is the model. A point that the Sun rejects
(:math:`lns < 0`) is, by construction, the same point lit from the other
side, at exactly the strength the solar Lambert rejected. The result is
that the dark side shows the **same relief** as the lit side — crater
rims that face the viewer catch the faint anti-solar light, valleys fall
off — at a dim scale. That is what makes the dark side read as a real
moon rather than a blob.

The level is set by a single constant, :math:`MOON_NIGHT_BACKLIT_K`, and
the backlit gray is a *direct linear* gray (not the tone LUT):

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the backlit night color)

   #define MOON_NIGHT_BACKLIT_K 0.20f

   static inline uint8_t moon_backlit_night(float A, float lns) {
       float l = -lns;
       if (l < 0.0f) l = 0.0f;
       else if (l > 1.0f) l = 1.0f;
       float out = A * l * MOON_NIGHT_BACKLIT_K;
       return (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
   }

The peak dark-side gray is :math:`255 \times K` — with :math:`K = 0.20`
that is a peak of about **51**, a level at which the maria and highland
structure are clearly visible without the dark side competing with the
lit side. (The value's history — how it got from a flat floor to this
computed surface, and why :math:`K` was doubled — is the artistic pass,
:doc:`the artistic pass <art-pass>`.)

Why linear gray, not the tone LUT
---------------------------------

The backlit side deliberately bypasses ``MOON_TONE_LUT``. Two reasons:

- The LUT's **shadow lift** (its bin 1 maps to 27, not 1) is tuned for
  the lit side's faint terminator pixels. Applied to the already-faint
  backlit side, it would over-brighten it.
- The LUT is **non-linear** in the highlights. The backlit model's
  contract is *peak gray = 255 × K* — a linear relationship so the
  constant is directly interpretable. The LUT's highlight rolloff would
  break that contract.

The linear gray also has a side benefit: it keeps the backlit side a
pure function of :math:`A`, :math:`lns`, and :math:`K`, so the
"calibrate :math:`K` to NASA's dark side" measurement in :doc:`the artistic pass <art-pass>` is a
single, honest number.

The constant-projection property
--------------------------------

A subtle but load-bearing fact: the backlit color is a function of the
body-frame surface direction and the epoch's fixed Sun only. The camera
basis (which direction the viewer sees) selects *which* direction is on
screen, but never enters the *color*. That is what made a pole spin at a
fixed phase (the light-sleep "dusk", retired 2026-11-13,
:doc:`the animation pipelines <../implementation/animations>`) possible: the fully-lit
body-frame color map is **constant** — it can be built once and every spin
frame is just a re-projection of that one static map. No per-frame
lighting recompute, and a fixed surface point keeps exactly its color
on every frame (no terminator flicker). This is why the dusk was cheap
and stable, and why the backlit dark side is *static* while the
sphere it is projected onto rotates.

The dusk's rim sliver: ``dk_sup`` and the "white ray" (retired with the dusk)
------------------------------------------------------------------------------

The backlit model has one edge case the earthshine model never had. The
dusk spun the camera about the pole toward a fully-dark endpoint. But
the pole spin **never changes the view latitude** — the sub-Earth
latitude is fixed — so at the endpoint the visible disk can still carry
a thin **geometrically lit** rim sliver (points with :math:`0 < d <
d_{\max}` where :math:`d_{\max} = \sqrt{1 - (\mathbf{E}\cdot\hat{\mathbf{S}})^2}`),
even though it should be dark.

Left lit, that sliver lingers on the otherwise-dark sleep screen as a
bright arc around the disk edge — the bug logged as the **"white
ray."** The fix is a per-caller bound, ``dk_sup``: the bake writes the
backlit night side for :math:`d < dk_sup` instead of :math:`d < 0`.
Because that sliver *is* sun-lit (:math:`lns > 0`), its backlit value
:math:`\mathrm{clip}(-lns,0,1)` is exactly 0 — pure black. The sliver is
killed without a special case:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the dark-side bound)

   /* dk_sup (dusk only, 0.0f everywhere else): the night side is written
    * for d < dk_sup instead of d < 0 -- the dusk's endpoint view still
    * carries a geometrically LIT rim sliver (d up to sqrt(1 - E.S(phi*)^2),
    * the pole spin never changes the view latitude): left lit it lingers
    * on the otherwise-dark sleep screen as a bright arc (the "white ray").
    * Since that sliver is sun-lit (lns > 0), its backlit value clips to
    * pure black. Every other caller passes 0.0f. */
   int is_night = c->dark_zero ? (d < c->dk_sup) || (lns <= 0.0f) : (lns <= 0.0f);

Every caller passes ``dk_sup = 0.0f`` today (the dusk was the only
non-zero caller, retired 2026-11-13), so the test reduces to
:math:`lns \le 0` — byte-identical to the pre-backlit bake.

The shade-map margin (NN aliasing)
-----------------------------------

The dusk's *intermediate* frames did not re-bake — they re-projected
the pre-shaded body-frame map (:doc:`the animation pipelines <../implementation/animations>`). That map is sampled
**nearest-neighbor**, so a dark pixel near the :math:`d = dk_sup`
boundary can round to a *lit* cell and re-light the sliver. The fix is a
margin, applied when the map is *built* (not when it is re-projected):
the map writes the backlit night side for :math:`d < dk_sup +
MOON_DK_MARGIN`, eroding the lit region outward by the worst-case
nearest-neighbor offset (a dark pixel can round to a cell whose center
is up to half a longitude-cell plus half a latitude-cell away in :math:`d`;
on the unit sphere :math:`|\nabla(\hat{\mathbf{r}}\cdot\mathbf{S})| = 1`,
so that is :math:`\pi/\text{map}_w + \pi/\text{map}_h \approx 0.0073`
for the 864×432 map):

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (the map-build margin)

   /* The re-project samples the map NEAREST-NEIGHBOR, so a dark pixel
    * (d < dk_sup) near the d = dk_sup boundary rounds to the cell whose
    * CENTER is up to half a theta-cell + half a lat-cell away in d.
    * |grad(r . S)| = 1 on the unit sphere, so that is (pi/map_w + pi/map_h)
    * ~= 0.0073 for the 864x432 map. Write the backlit night side for
    * d < dk_sup + MOON_DK_MARGIN so every cell a dark pixel can round to
    * is backlit (pure black when the cell is sun-lit, lns > 0). */
   #define MOON_DK_MARGIN 0.010f

With the margin, a dark pixel *cannot* sample a lit cell and re-light the
sliver; the :math:`moon_reproject` path is a pure gather with no dark-side
bound of its own. The lit region the frames show is the body-fixed
:math:`d > dk_sup + \text{margin}` region — constant across frames (it only
rotates) — and it agrees with the final :math:`dk_sup` bake at the endpoint,
so the last re-projected frame and the dark-side bake match with no pop.

Where each path runs
--------------------

.. list-table::
   :header-rows: 1
   :widths: 24 30 46

   * - Path
     - dark_zero
     - dk_sup
   * - Image gate (``check_gate.py``)
     - 0 (earthshine)
     - 0.0
   * - Static bakes (display; the boot sweep's and the dusk's
       rows retired with them 2026-11-13)
     - 1 (backlit)
     - 0.0
   * - Dusk frames (map build + final bake;
       retired 2026-11-13)
     - 1 (backlit)
     - the endpoint :math:`dk_sup`

The gate validates the physics (earthshine, :math:`dark_zero=0`); the
display ships the product (backlit, :math:`dark_zero=1`). The two never
contaminate each other — the backlit path is *invisible* to the gate,
which is exactly what the project wanted: the image-diff numbers prove
the lit-side physics, while the artistic dark side is a free, measured
display choice (the next page).
