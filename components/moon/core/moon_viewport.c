/* Observer-view disk tilt: the parallactic angle of the Moon for one
 * UTC instant at one reference city.
 *
 * The baked frame is north-up (moon_bake, gate-validated). The UI's
 * in-plane rotate gather (moon_rotate) turns it into the view an
 * observer at (lat, lon) sees: the disk rotates so celestial north
 * sits at the parallactic angle q of the Moon's position — the angle
 * at the Moon between the zenith and the celestial north pole,
 * measured eastward from north:
 *
 *     tan q = sin H / (tan φ cos δ − sin δ cos H)
 *
 * with H the Moon's local hour angle, φ the observer latitude, δ the
 * Moon's TOPOCENTRIC declination. This is what makes the crescent
 * "lie down" at dusk and stand up at midnight, and why the tilt
 * follows the local time of night — so the selectable timezone's
 * reference city (clock_tz) is an input to the viewport.
 *
 * Pure C + the same vendored public-domain ERFA cheap ephemeris as
 * moon_geometry (eraMoon98, TT-based). The sidereal time is the
 * IAU-2000 Earth Rotation Angle, NOT the classic GMST polynomial:
 * eraMoon98 returns ICRF/GCRS right ascensions, and GMST is reckoned
 * from the mean equinox — feeding GMST to an ICRS right ascension
 * silently adds the accumulated general precession in RA (~0.34 deg
 * in 2026, growing ~0.014 deg/yr). The missing equation-of-equinoxes
 * (GAST-ERA, < 0.005 deg) is far below what the gather quantizes.
 * The topocentric correction subtracts the observer's geocentric
 * vector (spherical Earth, geodetic lat) from the geocentric Moon
 * direction, which carries the lunar parallax into q — the physical
 * "as seen standing in that city" term the geocentric reference lacks.
 *
 * host_sim/viewport_test.c diffs this against astropy 8's machinery
 * (position angle of the physical zenith as seen from the Moon's
 * geocentric apparent position) over a golden table.
 *
 * Sign convention: q in degrees, −180..180, EAST of north (the
 * astronomical convention). Whether the UI rotates the image by +q or
 * −q is the UI-side sign knob (Kconfig UI_MOON_OBSERVER_SIGN) — the
 * core stays display-agnostic.
 */
#include "moon.h"
#include "moon_consts.h" /* MOON_J2000_JD (same TT/JD anchor as moon_astro.c) */

#include <math.h>

#include "erfa.h" /* eraMoon98 (vendored, public domain) */

#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
/* TT - UTC in the 2017..2035 range (no leap second in that window);
 * same constant as moon_astro.c. */
#define TT_OFF_S 69.184
/* au -> km */
#define AU_KM 149597870.7
/* mean Earth radius, km (the observer's geocentric vector; the
 * ~0.003 deg flattening effect on q is far below the display
 * resolution) */
#define EARTH_R_KM 6371.0

float moon_viewport_rot_deg(int64_t utc_epoch, float lat_deg, float lon_deg) {
    double jd_ut = utc_epoch / 86400.0 + 2440587.5; /* JD(UT), UT1 ~= UTC */
    double t_ut = jd_ut - 2451545.0;

    /* Earth Rotation Angle (IAU 2000, deg): theta = 360*(0.7790572732640
     * + 1.00273781191135448 * du). The sidereal reference conjugate to
     * ICRF right ascension (eraMoon98's frame) — see the file header
     * for why not GMST. */
    double theta = 360.0 * (0.7790572732640 + 1.00273781191135448 * t_ut);
    double gmst = fmod(theta, 360.0);
    if (gmst < 0.0)
        gmst += 360.0;

    /* Geocentric Moon (ICRF, AU), same TT argument as moon_geometry. */
    double d = (jd_ut + TT_OFF_S / 86400.0) - MOON_J2000_JD;
    double d1 = MOON_J2000_JD, d2 = d;
    double mpv[2][3];
    eraMoon98(d1, d2, mpv);
    double mg_km[3] = {mpv[0][0] * AU_KM, mpv[0][1] * AU_KM, mpv[0][2] * AU_KM};

    /* Observer's geocentric vector in ICRF: fixed in the EARTH frame
     * (lat φ, east longitude), so in the sky frame its direction is
     * (GAST + lon). Spherical Earth. */
    double lha = (gmst + (double)lon_deg) * DEG2RAD; /* local direction's RA */
    double phi = (double)lat_deg * DEG2RAD;
    double cphi = cos(phi);
    double obs[3] = {EARTH_R_KM * cphi * cos(lha), EARTH_R_KM * cphi * sin(lha), EARTH_R_KM * sin(phi)};

    /* Topocentric Moon direction. */
    double tc[3] = {mg_km[0] - obs[0], mg_km[1] - obs[1], mg_km[2] - obs[2]};
    double n = sqrt(tc[0] * tc[0] + tc[1] * tc[1] + tc[2] * tc[2]);
    double ra = atan2(tc[1], tc[0]);
    double dec = asin(tc[2] / n);

    /* Local hour angle and the parallactic angle. */
    double h = (gmst + (double)lon_deg) * DEG2RAD - ra;
    double q = atan2(sin(h), tan(phi) * cos(dec) - sin(dec) * cos(h));
    return (float)(q * RAD2DEG);
}
