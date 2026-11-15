Geometry and ephemeris
======================

The moon renderer needs, for any UTC instant, three unit vectors in the
Moon's *body frame* — the frame that is fixed to the rotating lunar
surface:

- :math:`\mathbf{S}` — Moon→Sun (the sub-solar direction),
- :math:`\mathbf{E}` — Moon→geocenter (the sub-Earth direction, the
  point of the disk the camera looks at),
- and the *camera basis* — :math:`\mathbf{right}` (image +x) and
  :math:`\mathbf{up}` (image +y) — which together with :math:`\mathbf{E}`
  define how the visible hemisphere is projected onto the screen.

Get these right and the terminator falls exactly where the Moon's
surface says it should, with exactly the right libration (the slow
wobble that lets us peek around the sides of the Moon) and position
angle (the tilt of the lunar north pole on the sky). Get them wrong and
the Moon looks like *a* moon — just not *this* moon, on *this* date.

The short version
-----------------

This page is the first half of "compute the Moon": before any pixel can
be shaded, the renderer has to know where everything *is* and how the
Moon is *aimed*. The whole job in plain language:

1. **Ask where the bodies are.** A compact astronomical formula gives
   the positions of the Sun, Earth, and Moon for the requested instant
   — no network, no NASA lookup tables on the device.
2. **Keep only the directions.** The renderer never asks *how far* —
   only *which way*: two arrows from the Moon, one pointing at the Sun
   (:math:`\mathbf{S}`), one pointing at the Earth (:math:`\mathbf{E}`).
3. **Know how the Moon is aimed.** The Moon turns like a wheel on an
   axle: its orientation at any instant is fixed by just two numbers —
   where the axle (spin axis) points, and how far the wheel has turned
   about it since a known starting reading.
4. **Speak the Moon's language.** The surface maps are stored in the
   frame that rotates with the ground. Re-expressing the Sun, Earth and
   camera arrows in that frame turns "which way?" into "which point on
   the surface?" — which is what a texture lookup needs.

The next page (:doc:`the lighting model <lighting>`) takes the arrows
this page builds and does the shading. Here every arrow is derived,
number by number.

Vocabulary
----------

Twelve terms carry the page (full definitions in the :doc:`glossary
<../glossary>`):

- **Frame (reference frame)** — a set of three numbered arrows used to
  state directions, like the axes on a graph. "The Sun lies at
  direction (x, y, z)" is meaningless until you say *which frame*;
  converting between frames re-expresses the same arrow in the other
  frame's arrows.
- **Unit vector** — an arrow trimmed to length exactly 1, so it carries
  direction only.
- **ICRF** — the International Celestial Reference Frame: the
  near-non-rotating backdrop anchored to distant quasars, the standard
  "fixed stars" frame all astronomy positions are quoted in.
- **Body frame** — the frame glued to the lunar surface, rotating with
  it: its +z is the lunar north pole, and latitudes and longitudes are
  just coordinates in it.
- **Pole** — the direction of the Moon's spin axis, stated in the ICRF.
- **Prime meridian** — the reference longitude (the lunar "Greenwich");
  the angle :math:`W` says how far the Moon has rotated since the
  reference epoch.
- **Julian Date (JD)** — astronomy's continuous day counter; the
  reference epoch J2000 is JD 2451545.0.
- **Ephemeris** — any formula or table that says where the solar-system
  bodies are at a given time.
- **Dot product** — the vector multiply ``A · B`` between two unit
  arrows: the cosine of the angle between them (``1`` same way, ``0``
  at right angles, negative pointing apart). It is how "how aligned are
  these two directions?" gets computed with three multiplies and two
  adds.
- **Cross product** — ``A × B``: the arrow perpendicular to both
  inputs (right-hand rule); the standard way to manufacture a
  perpendicular direction.
- **Libration** — the Moon's slow apparent wobble, which over time lets
  us peek a little past each limb and each pole.
- **Position angle** — how the lunar north pole is tilted on the sky:
  the angle from celestial north to the pole, measured eastward.

The chain in one picture
------------------------

**The short version.** One ``int64`` epoch goes in, one
``moon_geom_t`` comes out, through five stations: a time conversion,
two vendored ERFA calls, a two-number rotation, a camera basis, and a
bag of scalars (plus the eclipse-shadow triple the event classifier
consumes). The page below walks each station; this is the machine they
describe — the chain sealed first, then the stations opened.

.. graphviz:: ../_graphs/geometry-chain.dot
   :alt: The moon_geometry chain sealed — epoch in, time and ERFA box, calibrated body frame box, camera basis and scalars box, moon_geom_t out, fan-out to bake, gate, classifier
   :align: center
   :caption: ``moon_geometry`` as a black box: the four boxes below are opened one figure each. Blue is the path a value takes to the struct; gray dashed edges are *reads* of already-built values — the gate fields and the classifier re-project what the main path produced.

.. graphviz:: ../_graphs/geometry-chain-time.dot
   :alt: The time station opened — JD(UT), TT offset, the ERFA split, eraEpv00 and eraMoon98, the three unit directions
   :align: center
   :caption: The first two stations: the TT conversion (the 69.184 s offset, no leap second in the 2017–2035 window) and the two vendored ERFA calls — a nonzero ``eraEpv00`` return ABORTS the whole geometry. Distances are thrown away at the unit squeeze, which is what makes the cheap ephemeris precisely adequate; the same day counter also spins the body wheel.

.. graphviz:: ../_graphs/geometry-chain-body.dot
   :alt: The body frame opened — calibrated pole, W rotation angle, Rodrigues rot_axis, dot-with-the-columns into the body frame
   :align: center
   :caption: The two-number rotation: the CALIBRATED pole pair is the axle (not the IAU nominal values), W is how far the wheel has turned (≈ 27.3 days per turn), and Rodrigues' three terms swing both equatorial arrows about the pole — the pole never moves, only W turns; that one rotation IS the spin. The ICRF→body dot is the same arrow asked the same question three times, and being a pure rotation it maps ANY vector without distortion.

.. graphviz:: ../_graphs/geometry-chain-view.dot
   :alt: The camera basis, scalars and eclipse shadow opened — north onto the sky plane, east-left flip, lon/lat, posangle, phase, the umbra axis triple
   :align: center
   :caption: The view and the gate fields: celestial north flattened onto the sky plane (project against the NON-unit line of sight and the disk tilts — the guard comment), east with the EAST-LEFT screen-right flip — one of the two things easy to get backwards, with its sign knob and the 180° texture offset — and the shadow axis (the other amber one) that the blood-moon classifier reads against the cone constants. The scalars are gate fields, not pixels: the lon/lat, posangle and phase feed the NASA cross-check at tolerances 0.6/0.6/2.0°.

The distance is discarded at ``unit3`` and only resurrected for the
display ``distance_km`` — the fact that makes the cheap ephemeris not
merely adequate but *precisely* adequate (see below).

The pipeline *around* this chain — who calls ``moon_geometry``, where
the finished frame goes, and the watchdogs around the panel write — is
``docs/_graphs/moon-core-flow.dot`` in :doc:`the C core
<../implementation/core-c>`; what the bake does with the four arrows
once they exist is :doc:`the lighting model <lighting>`; how finely the
textures these lookups hit can actually speak is :ref:`the resolution
funnel <sharpness-funnel-perf>`.

Two frames, one rotation
------------------------

**The short version.** The Moon's orientation is a wheel on an axle:
you need to know exactly two things — which way the axle points (the
*pole*) and how far the wheel has turned since a known starting reading
(the prime meridian :math:`W`). Those two facts fix the entire
surface-glued frame, and every texture lookup follows from it.

The ephemeris (the Sun, Moon, and Earth positions in time) is computed
in the **ICRF** — the International Celestial Reference Frame, a
quasi-inertial frame fixed to the distant quasars. But the surface
features (the maria, the craters) are fixed to the **Moon's body
frame**, which rotates with the Moon. The relationship between the two
is a single rigid rotation, fully described by two numbers:

- the **pole** — the direction of the body-frame +z axis in the ICRF,
- the **prime meridian** :math:`W` — the body-frame longitude that
  currently faces a reference direction, advancing at the Moon's
  rotation rate.

This is the standard IAU "IAU_Moon" description. The IAU publishes
nominal values, but the project *calibrates* its own constants against
the NASA SVS 5415 ground truth (:doc:`the image gate <../verification/image-gate>` documents the residual),
because a fraction of a degree of pole error shows up as a visible
libration drift across the year. The calibrated constants live in
``moon_consts.h`` (generated from the Python ``constants.py`` — a single
source of truth shared by the C core and the Python prototype):

.. code-block:: c
   :caption: components/moon/generated/<board>/moon_consts.h (the calibrated body frame)

   // --- Calibrated IAU_Moon body frame (pole + prime meridian) ---
   #define MOON_POLE_RA_ICRF   269.839977
   #define MOON_POLE_DEC_ICRF  68.110423
   #define MOON_PRIME_MER_W0   40.017251
   #define MOON_PRIME_MER_RATE 13.17617251 // deg / day
   #define MOON_J2000_JD       2451545.0

The prime meridian at any epoch is a linear function of Julian Date:

.. math::

   W(\text{JD}) = W_0 + \text{RATE} \times (\text{JD} - \text{JD}_{2000})

Read that as a clock face: :math:`W_0` is the reading at the reference
epoch, and ``RATE × (elapsed days)`` is speed times time-since-then —
about 13.2° per day, so the wheel comes back around in 360 / 13.176 ≈
27.3 days (the Moon's rotation period relative to the stars).

The body-frame basis (three orthonormal ICRF vectors, the x, y, and z
columns) is built by fixing the 0° meridian direction in the ICRF x–z
plane perpendicular to the pole, then rotating the whole basis about the
pole by :math:`W`. The Python ``moon.astro.body_basis`` does exactly
this with a Rodrigues rotation:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (body_basis, abridged)

   def body_basis(P, W_deg):
       """Orthonormal body-frame basis (x, y, z=P) expressed in ICRF."""
       x0 = np.array([P[2], 0.0, -P[0]])      # in ICRF x-z plane, perp to P
       x0 = _unit(x0)
       y0 = _unit(np.cross(P, x0))
       th = math.radians(W_deg)
       return _rot_axis(x0, P, th), _rot_axis(y0, P, th), P

What those lines do, one by one:

- ``x0 = [P[2], 0.0, -P[0]]`` is an arrow in the ICRF x–z plane that is
  perpendicular to the pole (check: dotting it with :math:`P` gives
  ``P.x*P.z + 0 - P.z*P.x = 0``). It serves as the 0°-meridian
  direction at the moment :math:`W = 0`.
- ``y0 = cross(P, x0)`` manufactures the third arrow so that x, y, z
  form a right-handed set.
- The two ``_rot_axis`` calls swing *both* equatorial arrows together
  about the pole by :math:`W` — that one rotation *is* the Moon's spin.
  (``_rot_axis`` is Rodrigues' formula in three terms: the part of the
  arrow along the axis stays put, the part across it sweeps a circle.)
- Because the pole never moves and only the spin angle :math:`W`
  changes, the frame rotates rigidly — exactly what "glued to the
  surface" means.

Once the basis is built, any ICRF unit vector :math:`\mathbf{v}`
converts to body-frame coordinates by dotting with the three basis
columns:

.. math::

   \mathbf{v}_{body} = (\mathbf{v}\cdot\mathbf{b}_x,\; \mathbf{v}\cdot\mathbf{b}_y,\; \mathbf{v}\cdot\mathbf{b}_z)

Dotting with the three columns asks the same arrow the same question
three times — *how much of you points along this axis?* — and the three
answers are its coordinates in the body frame.

The ephemeris
-------------

**The short version.** The ephemeris answers one question: *where are
the Sun, Earth, and Moon right now?* Professional planetary ephemerides
pinpoint them to sub-arcsecond accuracy — a level nobody could notice
on a disk a few hundred pixels across. This project deliberately uses
the cheap alternative: public-domain formulas small and fast enough to
run on the microcontroller itself, good to a few tenths of a degree —
the gate number below makes "plenty" precise.

The project uses a *cheap* ephemeris — the same low-order models
astropy's ``builtin`` (PyERFA) backend uses internally — rather than a
full planetary ephemeris (DE432). The C core vendors public-domain
**ERFA** ephemeris routines (``eraEpv00`` for the Earth-Moon barycenter
orbit, ``eraMoon98`` for the Moon around the barycenter) so the
microcontroller never needs a network ephemeris.

On the host, the Python prototype uses astropy's
``get_body_barycentric`` for the Sun, Moon, and Earth, then forms the
two directions the renderer needs:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (the two directions)

   sun   = get_body_barycentric("Sun",   t, ephemeris=eph).xyz.to_value(u.au)
   moon  = get_body_barycentric("Moon",  t, ephemeris=eph).xyz.to_value(u.au)
   earth = get_body_barycentric("Earth", t, ephemeris=eph).xyz.to_value(u.au)

   S = _unit(sun - moon)    # Moon -> Sun  (ICRF)
   E = _unit(earth - moon)  # Moon -> geocenter (ICRF)

Each of the last two lines is "subtract two positions, then squeeze the
result to length 1". The subtraction turns *positions* into a
*direction seen from the Moon*; ``_unit`` then throws the distance away
— which is exactly why the cheap ephemeris is not merely adequate but
precisely adequate: only the direction ever reaches the pixel shader.

Both are then rotated into the body frame with the same dot-product
conversion as above. The cheap ephemeris is *sufficient* here: the gate
(:doc:`the image gate <../verification/image-gate>`) measures a maximum subsolar-point error of about 0.43°
against NASA's own published geometry over the 32-frame set — well inside
the tolerance a 400–466 px disk needs, and far below what a visible
terminator position demands. (A full DE432 ephemeris would buy a
sub-arcsecond position, which is utterly irrelevant at this resolution.)

The camera basis
----------------

**The short version.** The camera looks down the Earth–Moon line. To
draw the disk you need two arrows lying flat in the plane perpendicular
to that line: one to call *up* (screen +y) and one to call *right*
(screen +x). "Up" is celestial north flattened onto the sky; "right"
is the *west* side — the picture as a person standing on Earth sees
it, with lunar east appearing on the left.

The last piece is the *view*. The camera is on Earth, looking at the
Moon, so the line of sight is :math:`-\mathbf{E}` (the Moon→geocenter
direction points at us). The screen convention is **celestial-north-up,
east-left** — the standard orientation in which a northern-hemisphere
observer sees the Moon, with the lunar east (the side that waxes first)
on the left.

In the ICRF, the celestial north on the sky is the component of the
ICRF +z axis perpendicular to the line of sight, and the sky east is the
cross product with the line of sight:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (camera basis)

   N_sky = np.array([0.0, 0.0, 1.0])
   L     = _unit(moon - earth)                       # geocenter -> Moon (line of sight)
   north = _unit(N_sky - float(N_sky @ L) * L)       # celestial north on the sky
   east  = _unit(np.cross(north, L))                 # east on the sky
   right = -east                                     # screen +x is WEST (east is left)
   right_body = _to_body(right, basis)
   up_body    = _to_body(north, basis)

Line by line: ``N_sky`` is the celestial north-pole direction in the
ICRF, and ``L`` is the line of sight *from us toward the Moon*. The
``north`` line is the workhorse: ``N_sky @ L`` (a dot product) measures
how much of north points *along* the line of sight, and subtracting
``(N_sky @ L) * L`` knocks that part off — what remains is the piece of
north lying flat on the sky plane, the camera's up. ``east`` is then
the perpendicular the cross product manufactures, and ``right`` simply
flips it. The last two lines re-express both arrows in the body frame
with the same dot-with-the-columns conversion as before.

The ``right = -east`` line is the "east-left" convention, and it is one
of the two things that is easy to get backwards (the other is the 180°
texture offset, below). The C core carries the same sign as a calibration
knob, ``MOON_POSANGLE_SIGN``.

The position angle
------------------

**The short version.** The position angle is the one number an amateur
astronomer quotes for "how is the Moon tilted on the sky tonight?"
Nothing drawn on the panel depends on it — the renderer carries it as a
self-check, and the image gate compares it against NASA's published
value to catch aiming mistakes independently of the pixels.

The *position angle* is the angle on the sky, measured east of
celestial north, of the lunar north pole. It is a useful independent
cross-check on the geometry: the gate compares the computed position
angle against NASA's published value (tolerance 2°). It is derived from
the same body-frame pole vector, projected onto the sky:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (position angle)

   pole_icrf = _unit(basis[2])                          # body (lon=0, lat=90) in ICRF
   Np = _unit(pole_icrf - float(pole_icrf @ L) * L)
   pa = C.POSANGLE_SIGN * math.degrees(math.atan2(float(Np @ east), float(Np @ north)))

The phase and illumination follow directly from the Sun-Earth angle at
the Moon:

.. math::

   \cos i = \mathbf{S}\cdot\mathbf{E}, \qquad
   \text{illum} = \frac{1 + \cos i}{2}

where :math:`i` is the Sun-Moon-Earth (phase) angle. :math:`i=0` is a
full moon (we see the fully-lit face), :math:`i=180°` is a new moon.
Read it as a conversation: ``S · E`` is the dot product asking *how
wide is the Sun–Moon–Earth angle?*, and ``(1 + cos i) / 2`` straightens
the answer into the illuminated fraction a calendar would print — full
moon (:math:`i = 0°`) gives ``1``, half moon (:math:`i = 90°`) gives
``0.5``, new moon (:math:`i = 180°`) gives ``0``.

The 180° texture offset
-----------------------

**The short version.** The map and the math count longitude from
opposite sides of the Moon — two conventions, neither one wrong, and
the equirectangular image simply starts its columns 180° away from
where body-frame longitude starts. One added constant reconciles them
at every lookup.

The equirect albedo texture is stored with column 0 at the prime
meridian, but the body-frame longitude convention the ephemeris uses is
offset by 180° from the texture column. Every texture lookup therefore
adds the constant offset:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (texture column)

   float colf = (theta + MOON_TEX_LON_OFFSET) * c->per_deg;   // MOON_TEX_LON_OFFSET = 180.0

Get this wrong and the features land mirrored — a moon that looks like a
moon but with the wrong craters on the wrong side. The image gate
catches it instantly, because a mirrored terminator is a large per-pixel
error.

Libration, for free
-------------------

**The short version.** Nobody computes the wobble. The model honestly
tracks just two ingredients — a steady spin and a real orbit — and the
famous wobble is what honestly tracking them *looks like* from Earth.

Libration is not computed as a separate term. It emerges from the body
frame: as the Moon's rotation (the prime meridian) and its orbit (the
directions :math:`\mathbf{S}` and :math:`\mathbf{E}`) evolve in time,
the sub-Earth and sub-Solar points wander across the surface, and the
camera basis precesses with the line of sight. That wandering *is* the
libration in longitude and latitude — including the nodal wobble that
lets us see a little past each pole. The calibrated
pole and prime-meridian constants are what make that wander trace the
real 18.6-year nodal cycle.

Where the numbers go
--------------------

**The short version.** Everything above lands in one C struct: four
body-frame unit arrows for the per-pixel work (:math:`\mathbf{S}`,
:math:`\mathbf{E}`, and the camera pair) plus a bag of scalars — the
sub-solar and sub-Earth points, position angle, phase angle, illuminated
fraction, distance — that exist for the image gate and for debugging,
and the eclipse-shadow triple the ``moon_event`` classifier turns into
the umbra test (the blood moon).

All of this lands in the ``moon_geom_t`` struct the bake consumes
(``components/moon/include/moon.h``):

.. code-block:: c
   :caption: components/moon/include/moon.h (the geometry struct, verbatim)

   typedef struct {
       float S[3];         /* Moon->Sun, unit, body frame (pole = +z) */
       float E[3];         /* Moon->geocenter, unit (the visible pole) */
       float right[3];     /* image +x (east left), body frame         */
       float up[3];        /* image +y (celestial north up), body frame */
       float subsolar_lon; /* deg (gate/debug only) */
       float subsolar_lat;
       float subearth_lon;
       float subearth_lat;
       float posangle;     /* deg, east of celestial north */
       float phase_angle_deg;
       float illum;        /* (1 + cos i) / 2 */
       float distance_km;
       /* Eclipse shadow axis (body frame, meters): shadow_dir = the
        * geocentric ANTI-solar unit direction (the umbra cone's axis
        * points away from the Sun); shadow_off = the closest point of
        * that axis to the Moon's center (it is perp to shadow_dir);
        * off_ax = the point's coordinate along the axis measured FROM
        * THE EARTH (a = (p + mg) . shadow_dir for a Moon-frame point p
        * -- see moon_event.h). The moon_event.c classifier turns these
        * into the per-pixel umbra/penumbra occlusion (the blood moon). */
       float shadow_dir[3];
       float shadow_off[3];
       float off_ax;
   } moon_geom_t;

   /* UTC epoch seconds -> body-frame geometry. Cheap ephemeris (eraEpv00 +
    * eraMoon98), validated against NASA SVS 5415: max 0.43 deg subsolar /
    * 0.51 deg subearth / 0.44 deg posangle over the 32-frame gate set. */
   void moon_geometry(int64_t utc_epoch, moon_geom_t *g);

The next page takes those four vectors and turns them into light.
