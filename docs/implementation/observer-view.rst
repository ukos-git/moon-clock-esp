Observer view — the moon as the timezone's city sees it
========================================================

The user's brief (2026-11-17): *"the selectable zones should include
all typical time zones and synchronize the moon projection / viewport
to these zones. Generic time zones (UTC+02) should be dropped. We need
this to calculate the viewport of the moon."*

Until then the timezone was pure clock arithmetic: it moved the
digits, never the moon. A north-up disk is a view from no particular
place — the crescent hangs by its horns at every hour, in every
season, on every continent. Standing in a city, the same moon is
**tilted**: the crescent lies down at dusk (the "wet signature"),
stands up at midnight, and the tilt is a function of the site's
latitude and the Moon's position over *that city's* sky. The observer
view makes that tilt real: the active timezone's reference city is an
input to the viewport, and changing the zone re-tilts the resting
frame in place.

It is also why the generic UTC±hh:mm entries had to go: a bare offset
has no city, no lat/lon and no DST rule — nothing to compute a view
from. The zone table is now 67 curated named IANA zones
(:cfile:`components/clock/clock_tz.c`, :doc:`the timezone screen
<../ui/settings-tz>`); every entry carries its reference
``lat_x10/lon_x10`` beside the POSIX rule.

The math chain
--------------

:cfunc:`moon_viewport_rot_deg` (:cfile:`components/moon/core/moon_viewport.c`)
answers one question: *at this UTC instant, what is the Moon's
parallactic angle for an observer at (lat, lon)?* — the angle at the
Moon between the zenith and the celestial north pole, east of north:

.. math::

   q = \operatorname{atan2}\bigl(\sin H,\; \tan\varphi \cos\delta - \sin\delta \cos H\bigr)

with ``H`` the Moon's local hour angle, ``φ`` the site latitude and
``δ`` the Moon's **topocentric** declination. Three choices make the
answer right, and each is witnessed:

- **Earth Rotation Angle, not GMST.** The hour angle needs a sidereal
  reference, and the vendored ephemeris (``eraMoon98``, the same
  cheap TT-based one the geometry uses) returns **ICRF/GCRS** right
  ascensions. Classic GMST is reckoned from the *mean equinox of the
  date*; pairing it with an ICRS right ascension silently adds the
  accumulated general precession in RA — ~0.34° in 2026, growing
  ~0.014°/yr, an order of magnitude above the display's need. The
  core therefore uses the IAU-2000 ERA polynomial,
  ``θ = 360·(0.7790572732640 + 1.00273781191135448·du)``, the
  reference conjugate to ICRS. The omitted equation-of-equinoxes
  (GAST−ERA, < 0.005°) sits far below what the Q15.15 gather
  quantizes.
- **Topocentric, not geocentric.** The geocentric Moon direction gets
  the observer's geocentric vector subtracted from it — a spherical
  Earth (R = 6371 km, geodetic latitude), the ~0.003° flattening
  effect on ``q`` invisible at the panel. This is the physical
  "as seen *standing in that city*" term: lunar parallax moves the
  disk against the stars by up to ~1°, and it is exactly what the
  geocentric reference lacks.
- **UT1 ≈ UTC.** The hour angle is read at ``jd_ut`` directly; the
  ≤ 0.9 s UT1−UTC band is 0.00001° of hour angle — noise.

The sign is the astronomical convention (east of north, −180..180);
whether the *image* rotates by +q or −q is a display handedness
question, and the core stays display-agnostic about it — the UI-side
knob below owns the answer.

Golden test: :cfile:`host_sim/viewport_test.c` diffs the core against
astropy 8's machinery (the position angle of the physical zenith as
seen from the Moon's geocentric apparent position) over a 16-entry
golden table spanning both hemispheres, both seasons and high
declinations: **PASS 16/16, max \|Δ\| 0.29°** — inside astropy's own
analytical-zenith reference tolerance. Run it with the rest of the
host suite (:doc:`the image gate <../verification/image-gate>`'s
sibling checks).

One build leg, not a second bake
--------------------------------

The bake stays exactly what the gate validated: north-up, zone-free.
The tilt rides the **gray→RGB565 gather** — the one leg every
displayed frame already passes through. In
:cfile:`components/ui/moon_ui.c` that leg is a single internal
helper, ``moon_fill_rgb_from_frame(epoch)``, and it is now the *only*
way ``s_rgb`` is ever built:

- under the observer's dead band (or with the option off, or with no
  usable zone), it is byte-for-byte the old exact pass — the
  module-wide ``moon_gray565`` LUT loop;
- otherwise it turns the same LUT into a rotation table and gathers
  ``s_frame`` through it with the Q15.15 **inverse gather** of
  :cfunc:`moon_rotate` (:doc:`rotation and the LUT <rotate-lut>`) —
  the inverse map means every destination pixel samples exactly one
  source pixel, out-of-frame reads land on black, and because the
  gather uses the module's LUT the *event tint is kept* — a comet-red
  moon tilts red.

Every producer publishes through it, at the epoch **the displayed
frame was baked for**: the hourly refresh publishes at the hour
centre, the boot replay's mid frames at their own hour centres
(``hours[i]·3600 + 1800`` — a replayed 21:00 frame tilts as the 21:00
sky had it, not as now does), the replay's final "now" leg at the
current instant, the orbit bake and the heal/restore/convert legs at
``s_orbit_epoch``. The frame is whole or nothing: the orbit task
bakes its rows, then builds the tilt in one locked pass, so the
gather never samples an unbaked row (a cancelled bake leaves ``s_rgb``
holding the *old* frame, never a half-rotated composite).

Two properties fall out of keeping the bake north-up. The **TF hour
cache stays zone-agnostic** — one card cache serves every timezone on
earth, and a zone change never invalidates it. And the **image gate
is untouched** — the gate's 32 frames are north-up physics frames;
the observer view is a display pass above them
(:doc:`the art pass <../science/art-pass>`'s class of decision).

The reveal rule
---------------

The bake reveal (:doc:`the bake reveal <bake-reveal>`) drips
north-up strip bands while a frame computes. Showing north-up strips
that are *about to be* tilted would animate a view nobody sees, so
the rule is: **wherever a tilt applies, the reveal is suppressed and
the frame bakes monolithically** — the hourly cold-bake and the boot
replay's final leg check the same ``moon_observer_theta`` gate the
publish uses and fall back to the plain ``moon_bake``. The atomicity
contract is unchanged either way (every displayed state is one
atomic band or one atomic whole-panel flush under ``s_rgb_mtx``);
what changes is only *which* frames are allowed to drip.

Two Kconfig knobs
-----------------

- ``UI_MOON_OBSERVER_SIGN`` (+1 / −1, default +1) — the screen
  handedness vs the astronomical east-of-north convention is not
  fixed at code level; the precedent is ``UI_IMU_ANGLE_SIGN``. If the
  crescent tilts *mirrored* (lying down at dawn instead of dusk),
  flip it.
- ``UI_MOON_OBSERVER_DEADBAND_DEG`` (default 2, 0–30) — below this
  ``q`` the plain exact LUT pass runs instead of the gather. The Q15.15
  quantization is ~0.01°, so this is a crispness choice (a
  nearest-neighbor resample buys nothing the eye can see at 1°), not
  a precision one. The dead-band's exactness also means the
  equator-side hours cost literally nothing over the old path.

Changing the zone re-gathers, it does not re-bake
--------------------------------------------------

The phase is a function of the instant, not of the zone — the zone
only selects the view. So :cfunc:`moon_ui_tz_changed` (the commit
chain's last call on the TIMEZONE screen,
:doc:`the timezone screen <../ui/settings-tz>`) does **not** mark the
frame stale and does not re-bake: it re-runs
``moon_fill_rgb_from_frame(s_orbit_epoch)`` under ``s_rgb_mtx`` and
re-presents the same gray frame through the new angle. The atomic
discipline holds — it flushes only when the moon is actually on
screen *and* the panel is free (no hold, no fade in flight, no busy
producer); otherwise it leaves the frame healed-but-not-flushed and
the next screen switch's own atomic flush presents it. When it does
flush, the drain witness runs as everywhere else.

The same seam on the **knob** (:doc:`the knob display <knob-display>`)
is a deliberate no-op: the knob's viewport stays fixed north-up, the
zone still applies and persists through the clock component.

When there is no view
---------------------

The observer view degrades to today's north-up behavior — silently,
leg by leg — whenever any input is missing: a non-positive epoch (no
trustworthy time: the frame must not be tilted for a default-2026
clock), an unknown zone name (a custom raw-POSIX setting has no city,
so ``clock_tz_observer`` reports false), or the option off
(``UI_MOON_OBSERVER_ENABLE=n`` restores the pre-2026-11-17 fixed
viewport exactly). No leg ever guesses a city.

What the board matrix says
--------------------------

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Board
     - Observer view
   * - S3 AMOLED 466
     - Full: tilt in every build leg, reveal suppression, tz re-gather.
   * - P4 4C 720
     - Identical (one ``moon_ui.c``, ``UI_PX``-scaled).
   * - knob 360
     - ``moon_ui_tz_changed`` is the no-op seam; the dial keeps the
       fixed north-up viewport (:doc:`the knob display <knob-display>`).
