/* UTC epoch -> lunar geometry in the calibrated IAU_Moon body frame.
 *
 * Ports moon/astro.py 1:1. Ephemeris is the cheap vendored ERFA pair
 * (eraEpv00 Earth + eraMoon98 Moon), the same models the validated
 * Python pipeline uses internally (astropy 'builtin'). Accuracy measured
 * against NASA SVS 5415 over the 32-frame gate: max 0.43 deg subsolar,
 * 0.51 deg subearth, 0.44 deg posangle.
 *
 * The body-frame rotation (pole + prime meridian) is the calibrated
 * constant set in moon_consts.h.
 */
#include "erfa.h" /* eraEpv00, eraMoon98 (vendored, public domain) */
#include "moon.h"
#include "moon_consts.h"

#include <math.h>

/* TT - UTC in the 2017..2035 range (no leap second in that window). */
#define TT_OFF_S 69.184
/* au -> km */
#define AU_KM 149597870.7

#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232

static void rot_axis(const float v[3], const float axis[3], double th, float out[3]) {
    double c = cos(th), s = sin(th);
    float k[3] = {axis[0], axis[1], axis[2]};
    float w[3];
    /* w = axis x v */
    w[0] = axis[1] * v[2] - axis[2] * v[1];
    w[1] = axis[2] * v[0] - axis[0] * v[2];
    w[2] = axis[0] * v[1] - axis[1] * v[0];
    double dot = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
    out[0] = (float)(v[0] * c + w[0] * s + k[0] * dot * (1.0 - c));
    out[1] = (float)(v[1] * c + w[1] * s + k[1] * dot * (1.0 - c));
    out[2] = (float)(v[2] * c + w[2] * s + k[2] * dot * (1.0 - c));
}

static void unit3(const double v[3], float out[3]) {
    double n = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    out[0] = (float)(v[0] / n);
    out[1] = (float)(v[1] / n);
    out[2] = (float)(v[2] / n);
}

/* Calibrated body-frame basis (P, x0, y0) at W=0 (pole + 0-deg meridian). */
static void body_ref_basis(float P[3], float x0[3], float y0[3]) {
    double ra = MOON_POLE_RA_ICRF * DEG2RAD;
    double dec = MOON_POLE_DEC_ICRF * DEG2RAD;
    P[0] = (float)(cos(dec) * cos(ra));
    P[1] = (float)(cos(dec) * sin(ra));
    P[2] = (float)(sin(dec));
    double a[3] = {P[2], 0.0, -P[0]}; /* in ICRF x-z plane, perp to P */
    unit3(a, x0);
    /* y0 = unit(P x x0) */
    double cr[3] = {P[1] * x0[2] - P[2] * x0[1], P[2] * x0[0] - P[0] * x0[2], P[0] * x0[1] - P[1] * x0[0]};
    unit3(cr, y0);
}

/* Body-frame (lon, lat) in deg of a unit ICRF vector, given W. */
static void body_lonlat(const double v[3], const float P[3], const float x0b[3], const float y0b[3], double W,
                        double *lon, double *lat) {
    float xb[3], yb[3], zb[3];
    double th = W * DEG2RAD;
    rot_axis(x0b, P, th, xb);
    rot_axis(y0b, P, th, yb);
    zb[0] = P[0];
    zb[1] = P[1];
    zb[2] = P[2];
    *lon = atan2(v[1] * yb[1] + v[0] * yb[0] + v[2] * yb[2], v[1] * xb[1] + v[0] * xb[0] + v[2] * xb[2]) * RAD2DEG;
    double zdot = v[0] * P[0] + v[1] * P[1] + v[2] * P[2];
    if (zdot > 1.0)
        zdot = 1.0;
    else if (zdot < -1.0)
        zdot = -1.0;
    *lat = asin(zdot) * RAD2DEG;
}

void moon_geometry(int64_t utc_epoch, moon_geom_t *g) {
    /* Days from J2000 in TT. */
    double d_ut = utc_epoch / 86400.0 + 2440587.5; /* JD(UT) */
    double d = (d_ut + TT_OFF_S / 86400.0) - MOON_J2000_JD;
    double d1 = MOON_J2000_JD, d2 = d;

    double pvh[2][3], pvb[2][3];
    if (eraEpv00(d1, d2, pvh, pvb) != 0)
        return;
    double eh[3] = {pvh[0][0], pvh[0][1], pvh[0][2]}; /* Earth helio p */
    double sun[3] = {-eh[0], -eh[1], -eh[2]};         /* Sun geocentric = -Earth helio */

    double mpv[2][3];
    eraMoon98(d1, d2, mpv); /* geocentric */
    double mg[3] = {mpv[0][0], mpv[0][1], mpv[0][2]};

    double S[3] = {sun[0] - mg[0], sun[1] - mg[1], sun[2] - mg[2]};
    double E[3] = {-mg[0], -mg[1], -mg[2]}; /* Moon->geocenter */
    double L[3] = {mg[0], mg[1], mg[2]};    /* geocenter->Moon */
    float Su[3], Eu[3], Lu[3];
    unit3(S, Su);
    unit3(E, Eu);
    unit3(L, Lu);

    double W = MOON_PRIME_MER_W0 + MOON_PRIME_MER_RATE * d;

    float P[3], x0b[3], y0b[3];
    body_ref_basis(P, x0b, y0b);
    double th = W * DEG2RAD;
    float xb[3], yb[3], zb[3];
    rot_axis(x0b, P, th, xb);
    rot_axis(y0b, P, th, yb);
    zb[0] = P[0];
    zb[1] = P[1];
    zb[2] = P[2];

    /* ICRF -> body: dot with the basis columns. */
    g->S[0] = Su[0] * xb[0] + Su[1] * xb[1] + Su[2] * xb[2];
    g->S[1] = Su[0] * yb[0] + Su[1] * yb[1] + Su[2] * yb[2];
    g->S[2] = Su[0] * P[0] + Su[1] * P[1] + Su[2] * P[2];
    g->E[0] = Eu[0] * xb[0] + Eu[1] * xb[1] + Eu[2] * xb[2];
    g->E[1] = Eu[0] * yb[0] + Eu[1] * yb[1] + Eu[2] * yb[2];
    g->E[2] = Eu[0] * P[0] + Eu[1] * P[1] + Eu[2] * P[2];

    /* Body-frame lon/lat of S and E (gate/debug fields). */
    double ss_lon, ss_lat, se_lon, se_lat;
    body_lonlat(S, P, x0b, y0b, W, &ss_lon, &ss_lat);
    body_lonlat(E, P, x0b, y0b, W, &se_lon, &se_lat);
    g->subsolar_lon = (float)ss_lon;
    g->subsolar_lat = (float)ss_lat;
    g->subearth_lon = (float)se_lon;
    g->subearth_lat = (float)se_lat;

    /* Camera basis: celestial-north-up, east-left (ICRF then body frame).
     * north = unit(Ns - (Ns.L) L) with L the UNIT line of sight (Lu);
     * projecting against a non-unit L leaves a component along the line of
     * sight, tilting the disk. */
    double Ns[3] = {0.0, 0.0, 1.0};
    double ndot = Ns[0] * Lu[0] + Ns[1] * Lu[1] + Ns[2] * Lu[2];
    double north[3] = {Ns[0] - ndot * Lu[0], Ns[1] - ndot * Lu[1], Ns[2] - ndot * Lu[2]};
    float nu[3];
    unit3(north, nu);
    /* east = unit(north x L) */
    double east[3] = {nu[1] * Lu[2] - nu[2] * Lu[1], nu[2] * Lu[0] - nu[0] * Lu[2], nu[0] * Lu[1] - nu[1] * Lu[0]};
    float eu[3];
    unit3(east, eu);
    /* right = -east */
    double right[3] = {-eu[0], -eu[1], -eu[2]};
    g->right[0] = right[0] * xb[0] + right[1] * xb[1] + right[2] * xb[2];
    g->right[1] = right[0] * yb[0] + right[1] * yb[1] + right[2] * yb[2];
    g->right[2] = right[0] * P[0] + right[1] * P[1] + right[2] * P[2];
    g->up[0] = nu[0] * xb[0] + nu[1] * xb[1] + nu[2] * xb[2];
    g->up[1] = nu[0] * yb[0] + nu[1] * yb[1] + nu[2] * yb[2];
    g->up[2] = nu[0] * P[0] + nu[1] * P[1] + nu[2] * P[2];

    /* Position angle (independent cross-check; gate field). */
    double pdot = P[0] * Lu[0] + P[1] * Lu[1] + P[2] * Lu[2];
    double Np[3] = {P[0] - pdot * Lu[0], P[1] - pdot * Lu[1], P[2] - pdot * Lu[2]};
    float Npu[3];
    unit3(Np, Npu);
    double na = Npu[0] * nu[0] + Npu[1] * nu[1] + Npu[2] * nu[2];
    double ea = Npu[0] * eu[0] + Npu[1] * eu[1] + Npu[2] * eu[2];
    double pa = MOON_POSANGLE_SIGN * atan2(ea, na) * RAD2DEG;
    pa = fmod(pa, 360.0);
    if (pa < 0)
        pa += 360.0;
    g->posangle = (float)pa;

    /* Phase angle + illumination + distance. */
    double cos_i = Su[0] * Eu[0] + Su[1] * Eu[1] + Su[2] * Eu[2];
    if (cos_i > 1.0)
        cos_i = 1.0;
    else if (cos_i < -1.0)
        cos_i = -1.0;
    g->phase_angle_deg = (float)(acos(cos_i) * RAD2DEG);
    g->illum = (float)((1.0 + cos_i) / 2.0);
    g->distance_km = (float)(sqrt(mg[0] * mg[0] + mg[1] * mg[1] + mg[2] * mg[2]) * AU_KM);
}
