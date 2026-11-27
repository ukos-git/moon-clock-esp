Geometry and ephemeris
======================

.. brief:
   Reader: a developer or technical reader who has read How the clock works.
   They already know: vectors, the dot and cross product, basic trigonometry.
   After this page they can: trace how one UTC instant becomes the Sun, Earth and camera vectors in the Moon's body frame, and say why the cheap ephemeris is enough.
   Page type: explanation.

This page explains how the clock turns a time into directions.
:doc:`How the clock works <../how-it-works>` said that the clock evaluates published formulas for the Sun, Earth and Moon, and keeps only two directions seen from the Moon.
Here are those formulas, the frame they are expressed in, and the C struct they end up in.

For any UTC instant the moon renderer needs three things in the Moon's *body frame*, the frame fixed to the rotating lunar surface:

- :math:`\mathbf{S}`: Moon→Sun (the sub-solar direction),
- :math:`\mathbf{E}`: Moon→geocentre (the sub-Earth direction, the point of the disk the camera looks at),
- the *camera basis*: :math:`\mathbf{right}` (image +x) and :math:`\mathbf{up}` (image +y), which with :math:`\mathbf{E}` define how the visible hemisphere is projected onto the screen.

With these right, the terminator falls where the Moon's surface says it should.
The libration (the slow wobble that lets us see around the sides of the Moon) and the position angle (the tilt of the lunar north pole on the sky) come out right as well.
With them wrong, the Moon looks like *a* moon, but not the Moon of that date.

The short version
-----------------

Before any pixel can be shaded, the renderer has to know where everything is and how the Moon is turned.
The job has four steps:

1. Ask where the bodies are.
   A compact astronomical formula gives the positions of the Sun, Earth and Moon for the requested instant.
   The device needs no network and no NASA lookup tables.
2. Keep only the directions.
   The renderer never asks how far, only which way: one arrow from the Moon to the Sun (:math:`\mathbf{S}`), one to the Earth (:math:`\mathbf{E}`).
3. Know how the Moon is turned.
   The Moon turns like a wheel on an axle.
   Two numbers fix its orientation at any instant: where the axle (the spin axis) points, and how far the wheel has turned since a known starting reading.
4. Use the Moon's own frame.
   The surface maps are stored in the frame that rotates with the ground.
   Re-expressing the Sun, Earth and camera arrows in that frame turns "which way?" into "which point on the surface?", which is what a texture lookup needs.

:doc:`The lighting model <lighting>` takes these arrows and does the shading.

Vocabulary
----------

Twelve terms carry the page.
The :doc:`glossary <../glossary>` has the full definitions.

- **Frame (reference frame)**: three numbered arrows used to state directions, like the axes on a graph.
  "The Sun lies at direction (x, y, z)" means nothing until you say which frame.
  Converting between frames re-expresses the same arrow in the other frame's arrows.
- **Unit vector**: an arrow of length 1, so it carries direction only.
- **ICRF**: the International Celestial Reference Frame.
  It is the near-non-rotating backdrop anchored to distant quasars, the standard "fixed stars" frame in which astronomy quotes positions.
- **Body frame**: the frame fixed to the lunar surface, rotating with it.
  Its +z is the lunar north pole, and latitudes and longitudes are coordinates in it.
- **Pole**: the direction of the Moon's spin axis, stated in the ICRF.
- **Prime meridian**: the reference longitude (the lunar "Greenwich").
  The angle :math:`W` says how far the Moon has rotated since the reference epoch.
- **Julian Date (JD)**: astronomy's continuous day counter.
  The reference epoch J2000 is JD 2451545.0.
- **Ephemeris**: any formula or table that says where the solar-system bodies are at a given time.
- **Dot product**: ``A · B`` of two unit arrows is the cosine of the angle between them (``1`` same way, ``0`` at right angles, negative pointing apart).
  It answers "how aligned are these two directions?" with three multiplies and two adds.
- **Cross product**: ``A × B`` is the arrow perpendicular to both inputs (right-hand rule), the standard way to build a perpendicular direction.
- **Libration**: the Moon's slow apparent wobble, which over time lets us see a little past each limb and each pole.
- **Position angle**: how the lunar north pole is tilted on the sky, measured eastward from celestial north.

The chain in one picture
------------------------

One ``int64`` epoch goes in and one ``moon_geom_t`` comes out.
In between are five stations: a time conversion, two vendored ERFA calls, a two-number rotation, a camera basis, and a set of scalars (plus the eclipse-shadow triple the event classifier reads).
The first figure shows the chain closed; the next three open its boxes one at a time.

.. graphviz:: ../_graphs/geometry-chain.dot
   :alt: The moon_geometry chain as four boxes: epoch in, time and ERFA, calibrated body frame, camera basis and scalars, moon_geom_t out, fanning out to the bake, the gate and the classifier.
   :align: center
   :caption: ``moon_geometry`` as a black box. Blue is the path a value takes to the struct; grey dashed edges are reads of values already built.

The grey dashed edges matter: the gate fields and the classifier re-project what the main path produced, they do not compute anything new.

.. graphviz:: ../_graphs/geometry-chain-time.dot
   :alt: The time station opened: JD(UT), the TT offset, the ERFA split, eraEpv00 and eraMoon98, and the three unit directions.
   :align: center
   :caption: The first two stations: the TT conversion and the two vendored ERFA calls.

The TT conversion adds a 69.184 s offset; there is no leap second in the 2017–2035 window.
A nonzero ``eraEpv00`` return aborts the whole geometry.
The distances are thrown away when the vectors are squeezed to unit length, which is what makes the cheap ephemeris precisely adequate.
The same day counter also turns the body wheel.

.. graphviz:: ../_graphs/geometry-chain-body.dot
   :alt: The body frame opened: the calibrated pole, the W rotation angle, the Rodrigues rot_axis, and the dot products with the columns into the body frame.
   :align: center
   :caption: The two-number rotation: the calibrated pole is the axle, W is how far the wheel has turned.

The pole pair is the project's calibrated one, not the IAU nominal values.
:math:`W` turns once in about 27.3 days.
Rodrigues' three terms swing both equatorial arrows about the pole.
The pole never moves, only :math:`W` turns, and that one rotation is the spin.
The ICRF→body dot product asks the same arrow the same question three times.
Because it is a pure rotation, it maps any vector without distortion.

.. graphviz:: ../_graphs/geometry-chain-view.dot
   :alt: The camera basis, scalars and eclipse shadow opened: north onto the sky plane, the east-left flip, lon/lat, posangle, phase, and the umbra axis triple.
   :align: center
   :caption: The view and the gate fields. The two amber items are the east-left flip and the shadow axis.

Celestial north is flattened onto the sky plane.
Projecting it against the *non-unit* line of sight would tilt the disk; a guard comment in the code says so.
East carries the east-left screen-right flip, one of the two things that are easy to get backwards.
It has its own sign constant, and the 180° texture offset is the other one.
The shadow axis is what the blood-moon classifier compares against the cone constants.
The scalars are gate fields, not pixels: the longitude/latitude, position angle and phase feed the NASA cross-check at tolerances 0.6°, 0.6° and 2.0°.

The distance is discarded at ``unit3`` and only recovered for the display field ``distance_km``.
That is why the cheap ephemeris is not merely adequate but precisely adequate (see `The ephemeris`_).

The pipeline around this chain (who calls ``moon_geometry``, where the finished frame goes, and the watchdogs around the panel write) is ``docs/_graphs/moon-core-flow.dot`` in :doc:`the C core <../implementation/core-c>`.
What the bake does with the four arrows is :doc:`the lighting model <lighting>`.
How much detail the textures behind these lookups can resolve is :ref:`the resolution funnel <sharpness-funnel-perf>`.

Two frames, one rotation
------------------------

The Moon's orientation is a wheel on an axle.
You need two facts: which way the axle points (the *pole*) and how far the wheel has turned since a known starting reading (the prime meridian :math:`W`).
Those two facts fix the whole surface-fixed frame, and every texture lookup follows from it.

The ephemeris (the Sun, Moon and Earth positions over time) is computed in the ICRF, a quasi-inertial frame fixed to the distant quasars.
The surface features (the maria, the craters) are fixed to the Moon's body frame, which rotates with the Moon.
One rigid rotation relates the two, described by two numbers:

- the pole: the direction of the body-frame +z axis in the ICRF,
- the prime meridian :math:`W`: the body-frame longitude that faces a reference direction, advancing at the Moon's rotation rate.

This is the standard IAU "IAU_Moon" description.
The IAU publishes nominal values, but the project calibrates its own constants against the NASA SVS 5415 frames (:doc:`the image gate <../verification/image-gate>` documents the residual).
A fraction of a degree of pole error shows up as a visible libration drift across the year.
The calibrated constants live in ``moon_consts.h``, generated from the Python ``constants.py``, the one source shared by the C core and the Python prototype:

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

Read it as a clock face.
:math:`W_0` is the reading at the reference epoch, and ``RATE × (elapsed days)`` is speed times time since then.
The rate is about 13.2° per day, so the wheel comes round in 360 / 13.176 ≈ 27.3 days, the Moon's rotation period relative to the stars.

The body-frame basis is three orthonormal ICRF vectors, the x, y and z columns.
The code fixes the 0° meridian direction in the ICRF x–z plane, perpendicular to the pole, and then rotates the whole basis about the pole by :math:`W`.
The Python ``moon.astro.body_basis`` does this with a Rodrigues rotation:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (body_basis, abridged)

   def body_basis(P, W_deg):
       """Orthonormal body-frame basis (x, y, z=P) expressed in ICRF."""
       x0 = np.array([P[2], 0.0, -P[0]])      # in ICRF x-z plane, perp to P
       x0 = _unit(x0)
       y0 = _unit(np.cross(P, x0))
       th = math.radians(W_deg)
       return _rot_axis(x0, P, th), _rot_axis(y0, P, th), P

What the lines do:

- ``x0 = [P[2], 0.0, -P[0]]`` is an arrow in the ICRF x–z plane, perpendicular to the pole.
  Dotting it with :math:`P` gives ``P.x*P.z + 0 - P.z*P.x = 0``.
  It is the 0°-meridian direction at the moment :math:`W = 0`.
- ``y0 = cross(P, x0)`` builds the third arrow so that x, y, z form a right-handed set.
- The two ``_rot_axis`` calls swing both equatorial arrows together about the pole by :math:`W`.
  That one rotation is the Moon's spin.
  ``_rot_axis`` is Rodrigues' formula in three terms: the part of the arrow along the axis stays put, and the part across it sweeps a circle.
- The pole never moves and only the spin angle :math:`W` changes, so the frame rotates rigidly.
  That is what "fixed to the surface" means.

With the basis built, any ICRF unit vector :math:`\mathbf{v}` converts to body-frame coordinates by dotting with the three basis columns:

.. math::

   \mathbf{v}_{body} = (\mathbf{v}\cdot\mathbf{b}_x,\; \mathbf{v}\cdot\mathbf{b}_y,\; \mathbf{v}\cdot\mathbf{b}_z)

Each dot product asks the arrow "how much of you points along this axis?".
The three answers are its coordinates in the body frame.

The ephemeris
-------------

The ephemeris answers one question: where are the Sun, Earth and Moon at this instant?
Professional planetary ephemerides give sub-arcsecond positions, a level nobody could see on a disk a few hundred pixels across.
The project uses a cheap alternative instead: public-domain formulas small and fast enough to run on the microcontroller, good to a few tenths of a degree.

These are the same low-order models that astropy's ``builtin`` (PyERFA) backend uses internally, rather than a full planetary ephemeris (DE432).
The C core vendors the public-domain ERFA routines ``eraEpv00`` (the Earth–Moon barycentre orbit) and ``eraMoon98`` (the Moon around the barycentre).
The microcontroller never needs a network ephemeris.

On the host, the Python prototype uses astropy's ``get_body_barycentric`` for the Sun, Moon and Earth, then forms the two directions the renderer needs:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (the two directions)

   sun   = get_body_barycentric("Sun",   t, ephemeris=eph).xyz.to_value(u.au)
   moon  = get_body_barycentric("Moon",  t, ephemeris=eph).xyz.to_value(u.au)
   earth = get_body_barycentric("Earth", t, ephemeris=eph).xyz.to_value(u.au)

   S = _unit(sun - moon)    # Moon -> Sun  (ICRF)
   E = _unit(earth - moon)  # Moon -> geocenter (ICRF)

Each of the last two lines subtracts two positions and squeezes the result to length 1.
The subtraction turns positions into a direction seen from the Moon.
``_unit`` then throws the distance away.
Only the direction ever reaches the pixel shader, which is why the cheap ephemeris is precisely adequate.

Both vectors are then rotated into the body frame with the same dot-product conversion.
The gate (:doc:`the image gate <../verification/image-gate>`) measures a maximum subsolar-point error of about 0.43° against NASA's published geometry over the 32-frame set.
That is well inside what a 400–466 px disk needs, and far below what a visible terminator position demands.
A full DE432 ephemeris would give sub-arcsecond positions, which make no difference at this resolution.

The camera basis
----------------

The camera looks along the Earth–Moon line.
To draw the disk you need two arrows lying flat in the plane perpendicular to that line: one for *up* (screen +y) and one for *right* (screen +x).
"Up" is celestial north flattened onto the sky.
"Right" is the *west* side, so the picture matches what a person on Earth sees, with lunar east on the left.

The camera is on Earth looking at the Moon, so the line of sight is :math:`-\mathbf{E}` (the Moon→geocentre direction points at us).
The screen convention is celestial-north-up, east-left.
That is the orientation in which a northern-hemisphere observer sees the Moon, with the lunar east (the side that waxes first) on the left.
(:doc:`The observer view <../implementation/observer-view>` later turns this disk to match a real observer's horizon.)

In the ICRF, celestial north on the sky is the part of the ICRF +z axis perpendicular to the line of sight.
Sky east is its cross product with the line of sight:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (camera basis)

   N_sky = np.array([0.0, 0.0, 1.0])
   L     = _unit(moon - earth)                       # geocenter -> Moon (line of sight)
   north = _unit(N_sky - float(N_sky @ L) * L)       # celestial north on the sky
   east  = _unit(np.cross(north, L))                 # east on the sky
   right = -east                                     # screen +x is WEST (east is left)
   right_body = _to_body(right, basis)
   up_body    = _to_body(north, basis)

Line by line:

- ``N_sky`` is the celestial north-pole direction in the ICRF, and ``L`` is the line of sight from us toward the Moon.
- ``north`` does the main work.
  ``N_sky @ L`` (a dot product) measures how much of north points along the line of sight.
  Subtracting ``(N_sky @ L) * L`` removes that part, and what remains is the piece of north lying flat on the sky plane: the camera's up.
- ``east`` is the perpendicular the cross product builds, and ``right`` flips it.
- The last two lines re-express both arrows in the body frame with the same dot-with-the-columns conversion.

The ``right = -east`` line is the east-left convention, one of the two things that are easy to get backwards (the other is `The 180° texture offset`_).
The C core carries the same sign as a calibration constant, ``MOON_POSANGLE_SIGN``.

The position angle
------------------

The position angle is the number an amateur astronomer quotes for "how is the Moon tilted on the sky tonight?".
It is the angle on the sky, measured east of celestial north, of the lunar north pole.
Nothing drawn on the panel depends on it.
The renderer carries it as a self-check: the gate compares it with NASA's published value (tolerance 2°), which catches aiming mistakes independently of the pixels.
It comes from the same body-frame pole vector, projected onto the sky:

.. code-block:: python
   :caption: assets/images/moon/moon/astro.py (position angle)

   pole_icrf = _unit(basis[2])                          # body (lon=0, lat=90) in ICRF
   Np = _unit(pole_icrf - float(pole_icrf @ L) * L)
   pa = C.POSANGLE_SIGN * math.degrees(math.atan2(float(Np @ east), float(Np @ north)))

The phase and illumination follow from the Sun–Earth angle at the Moon:

.. math::

   \cos i = \mathbf{S}\cdot\mathbf{E}, \qquad
   \text{illum} = \frac{1 + \cos i}{2}

Here :math:`i` is the Sun–Moon–Earth (phase) angle.
``S · E`` is the dot product that measures how wide that angle is.
``(1 + cos i) / 2`` turns the answer into the illuminated fraction a calendar prints:
full moon (:math:`i = 0°`, we see the fully lit face) gives ``1``, half moon (:math:`i = 90°`) gives ``0.5``, and new moon (:math:`i = 180°`) gives ``0``.

The 180° texture offset
-----------------------

The map and the maths count longitude from opposite sides of the Moon.
Neither convention is wrong: the Kit's equirectangular maps are georeferenced 180° of longitude away from the IAU_Moon body frame.
Column 0 of the texture is the map's own longitude 0, which is body longitude 180°.
Every texture lookup therefore adds a constant offset:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (texture column)

   float colf = (theta + MOON_TEX_LON_OFFSET) * c->per_deg;   // MOON_TEX_LON_OFFSET = 180.0

Without it, every lookup lands on the opposite side of the map, and the disk shows the wrong craters.
The image gate catches that at once, because the wrong features give a large per-pixel error.
:doc:`The reference data <reference-images>` describes how the offset was confirmed.

Libration without a libration term
----------------------------------

Nobody computes the wobble.
The model tracks two things, a steady spin and a real orbit, and the wobble is what tracking them looks like from Earth.

As the Moon's rotation (the prime meridian) and its orbit (the directions :math:`\mathbf{S}` and :math:`\mathbf{E}`) change over time, the sub-Earth and sub-solar points wander across the surface.
The camera basis precesses with the line of sight.
That wandering is the libration in longitude and latitude, including the nodal wobble that lets us see a little past each pole.
The calibrated pole and prime-meridian constants make the wander follow the real 18.6-year nodal cycle.

Where the numbers go
--------------------

Everything above lands in one C struct, ``moon_geom_t``, in ``components/moon/include/moon.h``.
It holds four body-frame unit arrows for the per-pixel work (:math:`\mathbf{S}`, :math:`\mathbf{E}` and the camera pair).
It also holds scalars for the image gate and for debugging: the sub-solar and sub-Earth points, position angle, phase angle, illuminated fraction and distance.
The last three fields are the eclipse-shadow triple that the ``moon_event`` classifier turns into the umbra test (the blood moon).

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

Where to go next
----------------

- :doc:`The lighting model <lighting>`: how the bake turns these four vectors into brightness, pixel by pixel.
- :doc:`The observer view <../implementation/observer-view>`: how the north-up disk is turned to match a real observer's horizon.
- :doc:`The image gate <../verification/image-gate>`: the geometry cross-check against NASA's published values.
