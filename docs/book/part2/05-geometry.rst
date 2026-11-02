Chapter 5 — Geometry and ephemeris
==================================

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

Two frames, one rotation
------------------------

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
the NASA SVS 5415 ground truth (Chapter 17 documents the residual),
because a fraction of a degree of pole error shows up as a visible
libration drift across the year. The calibrated constants live in
``moon_consts.h`` (generated from the Python ``constants.py`` — a single
source of truth shared by the C core and the Python prototype):

.. code-block:: c
   :caption: components/moon/include/moon_consts.h (the calibrated body frame)

   // --- Calibrated IAU_Moon body frame (pole + prime meridian) ---
   #define MOON_POLE_RA_ICRF   269.839977
   #define MOON_POLE_DEC_ICRF  68.110423
   #define MOON_PRIME_MER_W0   40.017251
   #define MOON_PRIME_MER_RATE 13.17617251 // deg / day
   #define MOON_J2000_JD       2451545.0

The prime meridian at any epoch is a linear function of Julian Date:

.. math::

   W(\text{JD}) = W_0 + \text{RATE} \times (\text{JD} - \text{JD}_{2000})

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

Once the basis is built, any ICRF unit vector :math:`\mathbf{v}`
converts to body-frame coordinates by dotting with the three basis
columns:

.. math::

   \mathbf{v}_{body} = (\mathbf{v}\cdot\mathbf{b}_x,\; \mathbf{v}\cdot\mathbf{b}_y,\; \mathbf{v}\cdot\mathbf{b}_z)

The ephemeris
-------------

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

Both are then rotated into the body frame with the same dot-product
conversion as above. The cheap ephemeris is *sufficient* here: the gate
(Chapter 17) measures a maximum subsolar-point error of about 0.43°
against NASA's own published geometry over the 32-frame set — well inside
the tolerance a 400–466 px disk needs, and far below what a visible
terminator position demands. (A full DE432 ephemeris would buy a
sub-arcsecond position, which is utterly irrelevant at this resolution.)

The camera basis
----------------

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

The ``right = -east`` line is the "east-left" convention, and it is one
of the two things that is easy to get backwards (the other is the 180°
texture offset, below). The C core carries the same sign as a calibration
knob, ``MOON_POSANGLE_SIGN``.

The position angle
------------------

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

The 180° texture offset
-----------------------

The equirect albedo texture is stored with column 0 at the prime
meridian, but the body-frame longitude convention the ephemeris uses is
offset by 180° from the texture column. Every texture lookup therefore
adds the constant offset:

.. code-block:: c
   :caption: components/moon/core/moon_bake.c (texture column)

   float colf = (theta + MOON_TEX_LON_OFFSET) * c->per_deg;   // MOON_TEX_LON_OFFSET = 180.0

Get this wrong and the features land mirrored — a moon that looks like a
moon but with the wrong craters on the wrong side. Chapter 17's gate
catches it instantly, because a mirrored terminator is a large per-pixel
error.

Libration, for free
-------------------

Libration is not computed as a separate term. It emerges from the body
frame: as the Moon's rotation (the prime meridian) and its orbit (the
directions :math:`\mathbf{S}` and :math:`\mathbf{E}`) evolve in time,
the sub-Earth and sub-Solar points wander across the surface, and the
camera basis precesses with the line of sight. That wandering *is* the
libration in longitude, latitude, and the libration in libration (the
nodal wobble that lets us see a little past each pole). The calibrated
pole and prime-meridian constants are what make that wander trace the
real 18.6-year nodal cycle.

Where the numbers go
--------------------

All of this lands in the ``moon_geom_t`` struct the bake consumes
(``components/moon/include/moon.h``):

.. code-block:: c
   :caption: components/moon/include/moon.h (the geometry struct)

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
   } moon_geom_t;

   /* UTC epoch seconds -> body-frame geometry. Cheap ephemeris
    * (eraEpv00 + eraMoon98), validated against NASA SVS 5415. */
   void moon_geometry(int64_t utc_epoch, moon_geom_t *g);

The next chapter takes those four vectors and turns them into light.
