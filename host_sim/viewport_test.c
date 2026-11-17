/* Golden test for moon_viewport_rot_deg (the observer-view disk tilt):
 * the parallactic angle of the Moon for a reference city + UTC instant,
 * diffed against astropy's own machinery (position angle of the
 * observer's zenith as seen from the Moon, built on astropy's GCRS
 * transform chain with the builtin ephemeris).
 *
 * The reference table below was generated ONCE by (astropy 8 in
 * host_sim/.venv; astropy removed AltAz.parallactic_angle, but
 * PA-of-zenith-from-Moon is the identical quantity by definition):
 *   moon   = get_body("moon", t)                  # geocentric apparent
 *   era    = math.degrees(erfa.era00(t.jd1, t.jd2))
 *   zenith = SkyCoord(ra=((era + lon) % 360)*u.deg, dec=lat*u.deg)
 *   q = moon.position_angle(zenith).deg            # [0,360)
 * Two deliberate choices in the reference:
 *   - get_body WITHOUT a location: astropy's builtin (erfa) ephemeris
 *     silently IGNORES the observer location, so the reference moon is
 *     geocentric. The core adds the spherical-Earth topocentric term
 *     (lunar parallax, up to ~0.95 deg of position) — the physical
 *     "standing in that city" term the reference lacks; worst case ~1
 *     deg of q, hence the tolerance (measured: <= 0.2 deg on this set).
 *   - the zenith built ANALYTICALLY (ellipsoidal normal: dec = geodetic
 *     lat, RA = ERA + lon), NOT via AltAz(alt=90): astropy 8's
 *     AltAz->ICRS transform carries an unphysical constant +0.148 deg
 *     dec pole offset (near-zenith moons amplify any position error by
 *     1/sin(z) — the Quito sample pins this down).
 * Re-generate if the model ever changes; the comparison is wrap-around
 * in [-180,180), so the [0,360) spelling above is fine.
 *
 * Build:
 *   cc -O2 -I../components/moon/include -I../components/moon/generated/s3 \
 *      -I../components/moon/core/erfa viewport_test.c \
 *      ../components/moon/core/moon_viewport.c \
 *      ../components/moon/core/erfa/{moon98,s2pv,pfw06,obl06,epv00,rx,rz,cp,ir,rxp,rxpv}.c \
 *      -lm -o viewport_test
 * Run: ./viewport_test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "moon.h"

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097 + doe - 719468;
}

static int64_t parse_iso(const char *s) {
    int y, mo, d, h = 0, mi = 0;
    sscanf(s, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi);
    return days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL;
}

typedef struct {
    const char *city;
    float lat, lon;
    const char *iso; /* "YYYY-MM-DD HH:MM" UTC */
    float ref_deg;   /* astropy parallactic angle, deg */
} sample_t;

/* Generated 2026-11-17 (astropy 8.0.2). "moon alt" comments: the Moon's
 * altitude at that instant (negative = below the horizon — the angle is
 * still the frame's, the UI just never shows it then). */
static const sample_t TABLE[] = {
    {"Berlin", 52.52f, 13.40f, "2026-11-17 22:00", +34.4789f},  /* moon alt   +3.23 deg */
    {"Berlin", 52.52f, 13.40f, "2026-11-24 01:30", +38.8657f},  /* moon alt  +42.57 deg */
    {"Berlin", 52.52f, 13.40f, "2026-12-09 18:15", +42.5563f},  /* moon alt  -28.62 deg */
    {"Tokyo", 35.68f, 139.69f, "2026-12-05 12:00", +349.6984f}, /* moon alt  -71.51 deg */
    {"Sydney", -33.87f, 151.21f, "2027-01-15 10:00", +142.0226f}, /* moon alt  +30.06 deg */
    {"Nairobi", -1.29f, 36.82f, "2026-11-18 21:00", +88.9344f},   /* moon alt  +22.51 deg */
    {"Quito", -0.18f, -78.47f, "2026-06-21 23:30", +57.4524f},    /* moon alt  +85.17 deg */
    {"Reykjavik", 64.15f, -21.94f, "2026-12-21 12:00", +352.0932f}, /* moon alt   -2.08 deg */
    {"Santiago", -33.45f, -70.67f, "2026-07-04 02:00", +237.4181f}, /* moon alt   +7.17 deg */
    {"Honolulu", 21.31f, -157.86f, "2026-10-31 05:00", +326.7483f}, /* moon alt  -36.16 deg */
    {"NewYork", 40.71f, -74.01f, "2027-03-02 03:45", +301.5288f}, /* moon alt  -48.36 deg */
    {"Singapore", 1.35f, 103.82f, "2026-09-23 14:20", +344.3847f}, /* moon alt  +73.43 deg */
    {"CapeTown", -33.92f, 18.42f, "2026-08-08 20:10", +243.2379f}, /* moon alt  -75.16 deg */
    {"Moscow", 55.76f, 37.62f, "2027-02-14 21:55", +36.9665f},  /* moon alt  +19.86 deg */
    {"Kiritimati", 1.87f, -157.40f, "2026-12-31 11:05", +272.6005f}, /* moon alt   +2.67 deg */
    {"Delhi", 28.61f, 77.21f, "2026-05-01 17:40", +342.1021f},  /* moon alt  +38.75 deg */
};

#define TOL_DEG 1.0f

/* Wrap any degree value into [-180, 180). */
static float wrap180(float a) {
    a = fmodf(a, 360.0f);
    if (a >= 180.0f)
        a -= 360.0f;
    else if (a < -180.0f)
        a += 360.0f;
    return a;
}

int main(void) {
    int fails = 0;
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        const sample_t *s = &TABLE[i];
        char iso[20];
        strncpy(iso, s->iso, sizeof(iso) - 1);
        iso[sizeof(iso) - 1] = 0;
        iso[10] = 'T'; /* parse_iso wants 'T' between date and time */
        int64_t e = parse_iso(iso);
        float q = moon_viewport_rot_deg(e, s->lat, s->lon);
        float diff = wrap180(q - s->ref_deg);
        int ok = fabsf(diff) < TOL_DEG;
        if (!ok)
            fails++;
        printf("%-11s %-17s q=%+8.3f ref=%+8.3f d=%+7.3f %s\n", s->city, s->iso, q, wrap180(s->ref_deg), diff,
               ok ? "ok" : "FAIL");
    }
    printf("%s: %d/%d samples within %.1f deg of astropy\n", fails ? "FAIL" : "PASS",
           (int)(sizeof(TABLE) / sizeof(TABLE[0])) - fails, (int)(sizeof(TABLE) / sizeof(TABLE[0])), (double)TOL_DEG);
    return fails ? 1 : 0;
}
