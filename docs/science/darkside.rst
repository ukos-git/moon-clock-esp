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
   the geometry, terrain and lit side are the same bake — with one
   display-only addition: the display's terminator is blended, not
   stepped (the branch below).
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
differ — plus, on the display, the narrow terminator blend band the
shipped branch adds around :math:`lns = 0` (drawn next).

The bake's ``dark_zero`` flag selects which dark side is written:

- ``dark_zero = 0`` — the **earthshine** model (:doc:`the lighting model <lighting>`): the dark
  side is a faint phase-independent gradient. This is the *validated*
  model — it is what the image gate runs, and
  it is byte-identical to the original pre-backlit bake for every
  :math:`d`.
- ``dark_zero = 1`` — the **computed backlit** surface: the dark side is
  the actual lunar surface (albedo *and* relief) lit from the
  anti-solar direction at a faint level. This is the *display* model —
  what the device shows, blended into the lit side at the terminator
  (the shipped branch, drawn next).

The flag is a per-caller parameter, not a global: the same ``moon_bake``
serves both the gate (physics) and the display (product), and nothing
else changes between the two paths.

The branch in one picture
-------------------------

**The short version.** The prose above describes two night sides and
one night test. The picture adds the thing the prose leaves out: the
branch *every shipping display bake takes* is neither night branch — it
computes both sides and smoothstep-blends them across a narrow band at
:math:`\lns = 0` (``moon_bake.c:479-493``), returning before the
``is_night`` test below is ever evaluated. The hard test is the *gate's*
path (and, until the retirement, the dusk's); the blend's *why* — the
cutout-edge report and the "NASA adds a gaussian blur at the boundary"
quote — belongs to the artistic pass (:doc:`the artistic pass <art-pass>`,
"The soft terminator").

The family is the house pattern: one black-box overview of the branch
anatomy, then four detail sketches — the per-pixel inputs, the shipped
blend, the hard night test, the backlit color.

.. graphviz:: ../_graphs/dark-side.dot
   :align: center
   :alt: Six sealed boxes — THE PER-PIXEL INPUTS and THE CALLERS' KNOBS above THE SHIPPED BRANCH and THE HARD NIGHT TEST, with MOON_BACKLIT_NIGHT and the dashed THE MAP-BUILD EROSION at the bottom.
   :caption: The dark branch as an anatomy picture — the black-box pass. The branch *every shipping display bake takes* is neither night branch: it computes both sides and smoothstep-blends them, returning before ``is_night`` is ever evaluated (``moon_bake.c:479-493``); the hard test is the gate's path (and, until the retirement, the dusk's). The blend's *why* — the cutout-edge report and the "NASA adds a gaussian blur at the boundary" quote — belongs to :doc:`the artistic pass <art-pass>`, "The soft terminator". The boxes open below; the three claims come after them.

**The per-pixel inputs** — the shared model every caller pays for:

.. graphviz:: ../_graphs/dark-side-inputs.dot
   :align: center
   :alt: Input boxes d, lns, A and the amber blood and body-frame notes on the left, es and L in the middle, all flowing right into the blue THE BLEND box.
   :caption: The inputs, verbatim: :math:`d = r_{body}·S` (``moon_bake.c:7``) drives earthshine; :math:`\lns = (d − gs)/√(1+glon²+glat²)` (:434) is the displaced-normal SOLAR Lambert — negative means the Sun rejects this ground; :math:`L = \mathrm{clip}(\lns, 0, 1)` (:436) is the lit-side brightness in; :math:`A` is the albedo nearest-neighbour tap (:459-470, +0.5 round) read for every IN-DISK pixel — the dark side needs it as much as the lit one; :math:`es(d) = a·x + b·x² + c·x³` with :math:`x = \max(−d, 0)` (:143-146) is "Phase-independent earthshine" (:142), zero on every :math:`d ≥ 0`, so the lit-side addition is a no-op. The amber shared machines: the blood pre-tint rides the LIT side only (:448-452) — "the backlit night side (``lns``, unmodified) is untouched" — and the body-frame constancy (the color is a function of surface direction + the epoch's fixed ``S`` only, one static shade map, only the projection rotates) is what the retired dusk's cheap spin stood on. The classifier is ``docs/_graphs/sky-events.dot``; the camera basis that picks WHICH pixels exist is ``geometry-chain.dot``.

**The shipped blend** — both sides, one write, no test:

.. graphviz:: ../_graphs/dark-side-branch.dot
   :align: center
   :alt: The blue three-display-sites box over litv, darkv and the smoothstep t box, all three flowing into the blue out box whose return skips is_night.
   :caption: The blend (:479-493), line by line: ``litv = TONE_LUT[idx] + es(d)`` (:485); ``darkv = moon_backlit_night(A, lns)`` (:486); ``t`` = smoothstep over :math:`\lns ∈ ±\texttt{MOON\_TERM\_SOFT}` — 0.03 ≈ 6 px each side at the 400 px disk (:193) — "NASA adds a gaussian blur at the boundary"; ``out = darkv + (litv − darkv)·t`` (:487-493). All three display bake sites (``moon_ui.c``:1249 the hourly strip, :1455 the boot bake, :2035 the bake-reveal strip) pass ``dark_zero=1, dk_sup=0`` and return here.

**The hard night test** — the gate's path:

.. graphviz:: ../_graphs/dark-side-night.dot
   :align: center
   :alt: The gate and the dashed retired dusk boxes feeding the is_night node, which fans to the dashed backlit write, the earthshine branch and the lit side; the blue ISOLATION note hangs under earthshine and the dashed dk_sup margin note feeds the backlit write.
   :caption: The test, verbatim (:496): ``int is_night = c->dark_zero ? (d < c->dk_sup) || (lns <= 0.0f) : (lns <= 0.0f);``. The three outcomes: night & ``dark_zero`` (:501) is the backlit write — only ever used with ``dk_sup > 0``, so dead since the dusk retired (whose bound was ``dk_sup = √(1−(E·S)²)``, the comment's own endpoint formula :377 — the pole spin never changes view latitude); night & ``!dark_zero`` (:505) is the validated earthshine side ``TONE_LUT[0] + es(d)``, which still reads albedo ``A`` and never uses it (the header's own "one dead load", :50-53); the lit side (:517) ``TONE_LUT[idx] + es(d)`` is the tone LUT's domain, :doc:`the lighting page's <lighting>` endpoint. The caller knobs are the flag contract (``moon.h``:82-102, signature :109): the gate runs ``dark_zero = 0, dk_sup = 0`` (``host_sim/main.c:168``; ``g_darkzero`` default 0 :50 — ``--darkzero`` opts in to the product). The inert dashed pair is dusk-era machinery kept so the shade-map model stays one code path: the erosion at :743 writes the backlit side for ``d < dk_sup + MOON_DK_MARGIN`` (0.010, :208) because the re-project samples NEAREST-NEIGHBOR and a dark pixel's cell can be up to ``(pi/map_w + pi/(2*map_h)) ≈ 0.0073`` away in ``d`` (:197-199) — the margin makes the wrong cell UNREACHABLE, not merely unlikely.

**The backlit color** — the three-factor product:

.. graphviz:: ../_graphs/dark-side-backlit.dot
   :align: center
   :alt: Four source boxes — the sign-flipped l, the sliver kill, the amber body-frame note, the why-not-the-LUT note — stacked left and flowing into the blue out = A · l · K box.
   :caption: ``moon_backlit_night`` (:220-227): :math:`l = −\lns` clipped to 0..1 (:221-224) — the sign flip IS the model: the more the Sun rejects a patch, the more directly it faces the anti-solar lamp at :math:`-S`; ``out = A · l · K``, ``K = MOON_NIGHT_BACKLIT_K = 0.20f`` (:226, :185) — peak ``255·K ≈ 51``; LINEAR on purpose: the tone LUT's shadow lift (bin 1 → 27) would over-brighten a faint side and break the peak=255·K contract. The free sliver kill: a SUN-LIT pixel (:math:`\lns > 0`) drawn backlit clips to exactly 0 — pure black; fight no lighting, re-classify the pixel as night and black falls out.

Three claims the picture is drawn to make. First, the device's night
side never reaches the hard test while ``dk_sup`` is 0: the three
display bake sites (``moon_ui.c:1339`` the hourly strip, ``:1596`` the
boot bake, ``:2176`` the bake-reveal strip) all pass ``dark_zero=1,
dk_sup=0`` and return from the blend. Second, the gate's isolation is
structural, not stylistic — ``dark_zero=0`` is *byte-identical* to the
old earthshine bake for every :math:`d` (``moon.h:101-102``), so the
image-diff numbers certify lit-side physics however the product's dark
side is tuned (the earthshine branch still loads the albedo and never
uses it — the header's own "one dead load", ``moon_bake.c:50-53``).
Third, everything dashed is inert: the ``dk_sup`` bound, its
``MOON_DK_MARGIN`` erosion, and the backlit night write are dusk-era
machinery kept so the shade-map model stays one code path; the
blood-moon pre-tint rides the *lit* side only (the classifier is
``docs/_graphs/sky-events.dot``), and the body-frame constancy — the
camera selects which pixels exist, never their color — is what the
retired dusk's cheap spin stood on.

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
clamps to the display range. One product of three factors — no table
read.

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
non-zero caller, retired 2026-11-13). What that leaves depends on the
path. The **gate** (``dark_zero = 0``) does run this test, and on that
path it reduces to the plain :math:`lns \le 0` and stays byte-identical
to the pre-backlit bake (``moon.h:101-102``). The **display** never
reaches this line at all: with ``dk_sup = 0`` the shipped blend branch
(the "branch in one picture" above) returns first, so the backlit night
write below the test is dusk-era machinery, inert — exactly the dashed
nodes in the figure.

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
    * |grad(r . S)| = 1 on the unit sphere, so that is (pi/map_w + pi/(2*map_h))
    * ~= 0.0073 for the 864x432 map. Write the backlit night side for
    * d < dk_sup + MOON_DK_MARGIN so every cell a dark pixel can round to
    * is backlit (pure black when the cell is sun-lit, lns > 0). */
   #define MOON_DK_MARGIN 0.010f

(The quote's own formula, ``pi/map_w + pi/(2*map_h)``, is the exact
half-theta-cell + half-lat-cell worst case this section derived above,
and evaluates to 0.00727 at 864x432 — the 0.010 margin is that bound
plus a hair.)

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
