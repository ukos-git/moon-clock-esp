Observer view: the moon as the timezone's city sees it
======================================================

.. brief:
   Reader: a firmware developer working on the moon display or the timezone settings.
   They already know: C, basic spherical astronomy (hour angle, declination), the bake and the gray→RGB565 gather.
   After this page they can: explain how the active timezone tilts the displayed moon, and where the tilt is computed and applied.
   Page type: explanation.

The clock tilts the displayed moon the way it looks from the reference city of the active timezone.
A north-up disk is a view from no particular place: the crescent hangs by its horns at every hour, in every season, on every continent.
Seen from a city, the same moon is **tilted**.
The crescent lies down at dusk (the "wet signature") and stands up at midnight, and the tilt depends on the site's latitude and on the Moon's position in that city's sky.
The observer view makes the tilt real: the reference city is an input to the viewport, and changing the zone re-tilts the resting frame in place.

The zone list therefore has no generic UTC±hh:mm entries.
A bare offset has no city, no latitude or longitude and no DST rule, so there is nothing to compute a view from.
The zone table holds 67 curated named IANA zones (:cfile:`components/clock/clock_tz.c`, :doc:`the timezone screen <../ui/settings-tz>`), and every entry carries its reference ``lat_x10/lon_x10`` beside the POSIX rule.

The math chain
--------------

:cfunc:`moon_viewport_rot_deg` (:cfile:`components/moon/core/moon_viewport.c`) answers one question.
At this UTC instant, what is the Moon's parallactic angle for an observer at (lat, lon)?
That is the angle at the Moon between the zenith and the celestial north pole, measured east of north:

.. math::

   q = \operatorname{atan2}\bigl(\sin H,\; \tan\varphi \cos\delta - \sin\delta \cos H\bigr)

Here ``H`` is the Moon's local hour angle, ``φ`` the site latitude and ``δ`` the Moon's **topocentric** declination.
Three choices make the answer right, and a test checks each one.

Earth Rotation Angle, not GMST
   The hour angle needs a sidereal reference.
   The vendored ephemeris (``eraMoon98``, the same cheap TT-based one the geometry uses) returns **ICRF/GCRS** right ascensions.
   Classic GMST counts from the *mean equinox of the date*.
   Pairing it with an ICRS right ascension silently adds the accumulated general precession in RA: ~0.34° in 2026, growing ~0.014°/yr, an order of magnitude above what the display needs.
   The core therefore uses the IAU-2000 ERA polynomial, ``θ = 360·(0.7790572732640 + 1.00273781191135448·du)``, the reference that pairs with ICRS.
   The omitted equation of the equinoxes (GAST−ERA, < 0.005°) sits far below what the Q15.15 gather quantizes.

Topocentric, not geocentric
   The core subtracts the observer's geocentric vector from the geocentric Moon direction.
   It models a spherical Earth (R = 6371 km, geodetic latitude); the flattening's ~0.003° effect on ``q`` is invisible on the panel.
   This is the physical "as seen standing in that city" term.
   Lunar parallax moves the disk against the stars by up to ~1°, and the geocentric reference lacks exactly that.

UT1 ≈ UTC
   The hour angle is read at ``jd_ut`` directly.
   The ≤ 0.9 s UT1−UTC band is 0.00001° of hour angle, which is noise.

The sign follows the astronomical convention (east of north, −180..180).
Whether the *image* rotates by +q or −q is a question of display handedness.
The core stays display-agnostic; the UI-side option ``UI_MOON_OBSERVER_SIGN`` (below) owns the answer.

The golden test :cfile:`host_sim/viewport_test.c` compares the core with astropy 8.
The reference is the position angle of the physical zenith as seen from the Moon's geocentric apparent position.
Its 16-entry golden table spans both hemispheres, both seasons and high declinations.
The result is **PASS 16/16, max \|Δ\| 0.29°**, inside the tolerance of astropy's own analytical-zenith reference.
It runs with the rest of the host suite (the sibling checks of :doc:`the image gate <../verification/image-gate>`).

The tilt is applied in the gather, not in a second bake
-------------------------------------------------------

The bake stays exactly what the gate validated: north-up and independent of the zone.
The tilt rides the **gray→RGB565 gather**, the one leg every displayed frame already passes through.
In :cfile:`components/ui/moon_ui.c` that leg is a single internal helper, :cfunc:`moon_fill_rgb_from_frame` (called with an epoch), and it is the only way ``s_rgb`` is built:

- Inside the observer's dead band, with the option off, or with no usable zone, it runs the exact pass: the module-wide ``moon_gray565`` LUT (look-up table) loop, byte for byte.
- Otherwise it turns the same LUT into a rotation table and gathers ``s_frame`` through it with the Q15.15 **inverse gather** of :cfunc:`moon_rotate` (:doc:`rotation and the LUT <rotate-lut>`). The inverse map means every destination pixel samples exactly one source pixel, and reads outside the frame land on black. Because the gather uses the module's LUT, it keeps the event tint: a comet-red moon tilts red.

Every producer publishes through the helper, at the epoch the displayed frame was baked for:

- the hourly refresh at the hour centre;
- the boot replay's middle frames at their own hour centres (``hours[i]·3600 + 1800``), so a replayed 21:00 frame tilts as the 21:00 sky had it, not as the sky is now;
- the replay's final leg at the current instant;
- the orbit bake and the heal, restore and convert legs at ``s_orbit_epoch``.

The frame is whole or nothing.
The orbit task bakes its rows, then builds the tilt in one locked pass, so the gather never samples a row that is not baked yet.
A cancelled bake leaves ``s_rgb`` holding the old frame, never a half-rotated composite.

Keeping the bake north-up has two useful results.
The **TF (microSD) hour cache does not depend on the zone**: one card cache serves every timezone on Earth, and a zone change never invalidates it.
And the **image gate is untouched**: the gate's 32 frames are north-up physics frames, and the observer view is a display pass above them (the same class of decision as :doc:`the art pass <../science/art-pass>`).

The tilt and compute progress
-----------------------------

No frame is displayed strip by strip (:doc:`the compute progress <compute-progress>`), so nothing needs to be suppressed during a bake.
The compute bake runs in strip chunks with the rows-completed watermark, whatever the tilt.
The watermark sits on the north-up strip loop, so the seconds arc's progress is honest either way.
The gather runs at publish time, on the finished frame.

Two Kconfig options
-------------------

``UI_MOON_OBSERVER_SIGN`` (+1 / −1, default +1)
   The screen's handedness relative to the astronomical east-of-north convention is not fixed in code; ``UI_IMU_ANGLE_SIGN`` is the precedent.
   If the crescent tilts mirrored (lying down at dawn instead of dusk), flip it.

``UI_MOON_OBSERVER_DEADBAND_DEG`` (default 2, 0–30)
   Below this ``q``, the plain exact LUT pass runs instead of the gather.
   The Q15.15 quantization is ~0.01°, so this is a choice for crispness, not precision: a nearest-neighbour resample at 1° buys nothing the eye can see.
   Because the dead band uses the exact pass, the hours near the equator cost nothing extra.

A third option, ``UI_MOON_OBSERVER_ENABLE``, switches the observer view off (see `When there is no view`_).

Changing the zone re-gathers, it does not re-bake
--------------------------------------------------

The phase is a function of the instant, not of the zone; the zone only selects the view.
:cfunc:`moon_ui_tz_changed` is the last call in the commit chain of the TIMEZONE screen (:doc:`the timezone screen <../ui/settings-tz>`).
It does not mark the frame stale and does not re-bake.
It re-runs :cfunc:`moon_fill_rgb_from_frame` at ``s_orbit_epoch`` under ``s_rgb_mtx`` and presents the same gray frame through the new angle.

The atomic-frame rule holds.
The function flushes only when the moon is on screen and the panel is free (no hold, no fade in flight, no busy producer).
Otherwise it leaves the frame rebuilt but not flushed, and the next screen switch's own atomic flush presents it.
When it does flush, the drain witness runs as everywhere else.

On the **knob** (:doc:`the knob display <knob-display>`) the same seam is a deliberate no-op.
The knob's viewport stays fixed north-up; the zone still applies and persists through the clock component.

When there is no view
---------------------

The observer view falls back to the north-up view, silently and leg by leg, whenever an input is missing:

- a non-positive epoch (no trustworthy time; the frame must not be tilted for a clock defaulting to 2026);
- an unknown zone name (a custom raw-POSIX setting has no city, so ``clock_tz_observer`` reports false);
- the option off (``UI_MOON_OBSERVER_ENABLE=n`` gives the fixed north-up viewport exactly).

No leg ever guesses a city.

What each board does
--------------------

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Board
     - Observer view
   * - S3 AMOLED 466
     - Full: tilt in every build leg (at publish), re-gather on a zone change.
   * - P4 4C 720
     - Identical (one ``moon_ui.c``, scaled by ``UI_PX``).
   * - knob 360
     - ``moon_ui_tz_changed`` is the no-op seam; the dial keeps the
       fixed north-up viewport (:doc:`the knob display <knob-display>`).

Where to go next
----------------

* :doc:`Rotation and the LUT <rotate-lut>`: the Q15.15 inverse gather the tilt uses.
* :doc:`The timezone screen <../ui/settings-tz>`: how the zone is picked and committed.
* :doc:`The art pass <../science/art-pass>`: the other display-side pass above the gate-checked physics.
