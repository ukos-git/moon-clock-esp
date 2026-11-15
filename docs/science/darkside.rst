The dark side
=============

The lit side has a validated model. The dark side is where the project
made its most deliberate *product* decisions — and the single most
visible one. This page covers the two dark-side models, why the
display uses the computed backlit surface instead of a black overlay or
a flat floor, the anti-solar lighting that makes crater rims readable in
the dark, and the ``dk_sup`` sliver-kill that keeps the sleep screen
from showing a phantom bright rim.

The short version
-----------------

The previous page ended every *lit* pixel at the tone LUT. This page is
about the other pixels — the ones the Sun rejects, where the solar
Lambert came out zero or negative — and it comes down to three
decisions:

1. **There are two dark sides, and a flag picks one.** The image gate
   (:doc:`the image gate <../verification/image-gate>`) validates a
   *physics* dark side: a faint earthshine glow fitted to NASA's
   frames. The panel shows a *product* dark side: the actual lunar
   surface rendered again, as if a dim lamp sat on exactly the opposite
   side of the Moon from the Sun. One parameter switches between them;
   everything else about a frame is identical.
2. **The displayed dark side is computed, not painted.** The first
   version painted the night half in one constant gray, and it was
   rejected: beside a real-textured crescent, a textureless half-disk
   reads as a broken renderer, not as the Moon.
3. **Its level is one honest number.** The backlit brightness is
   ``albedo × anti-solar Lambert × 0.20`` — deliberately *linear*,
   skipping the lit side's tone LUT, so the constant means exactly what
   it says (the brightest possible dark-side pixel is about 51 of 255)
   and can be calibrated against NASA's dark side as a single
   measurement.

Two later sections describe edge-case machinery built for the dusk
animation — a rim-sliver killer and a sampling margin. The dusk was
retired 2026-11-13; the code stays, inert: every caller passes zero,
so the night test reduces to the plain ``lns <= 0``.

Everything this page leans on — the vectors, ``lns``, the tone LUT,
the body frame — was built on :doc:`the geometry <geometry>` and
:doc:`the lighting model <lighting>`.

Vocabulary
----------

Nine terms carry the page (the full definitions live in the
:doc:`glossary <../glossary>`):

- **Dark side** — the pixels of the visible disk where the Sun
  contributes nothing: the solar Lambert ``lns`` is zero or negative
  there.
- **Backlit** — the display model's word for "computed, not painted":
  the same surface (albedo *and* relief) rendered as if lit from the
  anti-solar direction, then dimmed.
- **Anti-solar direction** — :math:`-\mathbf{S}`, the direction exactly
  opposite the Sun. The imaginary lamp that lights the dark side in the
  backlit model sits there.
- **Earthshine** — sunlight reflected off the Earth back onto the
  Moon's night side; the *physics* model's faint glow (the lighting
  page's cubic), which the image gate validates.
- **Lambert** — cosine-law brightness: how directly a surface faces a
  light source, clipped to the 0–1 range. The *solar* Lambert is the
  previous page's ``lns``; the backlit one is its sign flip.
- **Tone LUT** — the lit side's 256-entry calibration table from raw
  brightness to display brightness. The backlit side deliberately
  bypasses it.
- **Sliver** — the thin strip of geometrically lit pixels that can
  cling to the disk rim in a view that should be fully dark — the
  "white ray" bug.
- **dk_sup** — a threshold on the sub-solar cosine ``d``: below it the
  bake is told to write the night side, killing the sliver. A dusk
  remnant; every caller passes ``0.0`` today.
- **Nearest-neighbor (NN)** — sampling a map by taking the *closest*
  cell rather than blending the neighbors; fast, and the reason a
  pixel at a map's lit/dark boundary can grab the wrong side's color.

Two models, one flag
--------------------

**The short version.** One bake function serves two customers, and the
customer picks the night half with a single switch, ``dark_zero``. The
image gate asks for the physics answer (``0`` — earthshine); the panel
asks for the product (``1`` — the computed backlit surface). The lit
side, the terrain, the vectors: all shared. Only the night pixels
differ.

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

**The short version.** The cheapest dark side is paint: fill the night
half with one gray and be done. That was version one, and it failed
twice over — the half-disk had no surface, and next to a real-textured
crescent it looked like a rendering bug. The design goal that came out
of it: *the unlit side is fully calculated, just like the lit side.*

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

**The short version.** Every pixel already carries the number that
decided its daytime brightness — ``lns``, how directly its ground
faces the Sun. Flip that number's sign and it answers the night-side
question instead: *how directly does the same ground face the point
exactly opposite the Sun?* Clip it the same way, and you have the
backlit Lambert.

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

Reading the formula piece by piece: ``lns`` is the unclipped *solar*
Lambert from the previous page — on the night side it is negative, by
exactly the amount the Sun rejects that ground; the ``-`` turns that
rejection into acceptance (the more the Sun rejects a patch, the more
directly the patch faces the anti-solar lamp); and the ``clip`` floors
the lit side at zero (sunlit ground contributes no backlit value) and
caps the most anti-sun-facing ground at full.

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

What those lines do, one by one: ``l = -lns`` takes the Sun-rejection
value the night side carries; the ``if``/``else if`` pair is the clip —
it pins ``l`` into 0–1. ``out = A * l * MOON_NIGHT_BACKLIT_K`` then
multiplies three factors: the ground's paint (the albedo byte, in 0–255
units), how far the slope faces the anti-solar lamp (``l``), and the
dimming constant. The return line rounds to the nearest byte and
clamps to the display range. Three multiplies — no table read.

The peak dark-side gray is :math:`255 \times K` — with :math:`K = 0.20`
that is a peak of about **51**, a level at which the maria and highland
structure are clearly visible without the dark side competing with the
lit side. (The value's history — how it got from a flat floor to this
computed surface, and why :math:`K` was doubled — is the artistic pass,
:doc:`the artistic pass <art-pass>`.)

Why linear gray, not the tone LUT
---------------------------------

**The short version.** The lit side pushes its raw brightness through
the tone LUT — a calibrated curve with deliberate distortions (a shadow
lift, a highlight compression) tuned for *sunlit* pixels. Running the
already-faint backlit side through those distortions would ruin it, so
the backlit path skips the LUT entirely. A side effect is a gift: the
dimming constant stays an honest dial, and calibrating it against
NASA's dark side is one number's job.

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

**The short version.** Which half of the disk is dark depends on where
the viewer stands — that is the camera's choice. But the *color* a dark
pixel gets depends only on the ground under it and the epoch's fixed
Sun; the camera never enters the formula. So the dark-side colors can
be computed once into a body-frame map and reused for every camera
angle, and a given crater keeps its exact color from frame to frame.
That is what made the retired dusk spin cheap and flicker-free.

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

**The short version.** At the end of the retired dusk animation the
screen was meant to go fully dark — but because the spin never changed
the view latitude, a hairline at the disk rim kept grazing the Sun and
stayed lit: a bright arc nicknamed the "white ray". The cure rides a
free property of the backlit model: a *sunlit* pixel drawn in the
backlit style is **black**. So you don't fight the sliver's lighting —
you just re-classify those pixels as night, and black comes out the
other side. The re-classifier is ``dk_sup``, a bound every caller
passes as 0 today.

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

Reading the conditional: with the earthshine model (``dark_zero = 0``,
the false branch) the night test is the plain ``lns <= 0``; with the
backlit model the night region additionally starts at ``d < dk_sup`` —
the grazing sliver is routed through ``moon_backlit_night``, and
because its ``lns`` is positive (that sliver *is* sun-lit), the clip
inside the function makes it pure black. With ``dk_sup`` at 0 the extra
test is dead and both branches agree.

Every caller passes ``dk_sup = 0.0f`` today (the dusk was the only
non-zero caller, retired 2026-11-13), so the test reduces to
:math:`lns \le 0` — byte-identical to the pre-backlit bake.

The shade-map margin (NN aliasing)
-----------------------------------

**The short version.** The re-projected frames read the pre-shaded map
cell by cell — nearest cell wins. At the map's lit/dark boundary that
is a hazard: a night pixel just below the boundary can round *up* into
a lit cell and come back lit, re-creating the white ray one layer
down. The fix is preventive erosion: when the map is built, the dark
region is extended outward *just more* than a nearest-neighbor lookup
can possibly jump — so the wrong cell is unreachable by construction.

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
and the equirect map spans :math:`2\pi` in longitude but only
:math:`\pi` in latitude, so that is
:math:`\pi/\text{map}_w + \pi/(2\,\text{map}_h) \approx 0.0073`
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

**The short version.** Three callers share this one bake: the image
gate asks for the physics night side, the display bakes ask for the
backlit one, and the dusk (retired) was the only caller that ever set
``dk_sup``. The table is the guest list. The point of recording it is
the isolation: the gate never sees the backlit path, so its image-diff
numbers certify the lit-side physics no matter how the display's dark
side is tuned.

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
