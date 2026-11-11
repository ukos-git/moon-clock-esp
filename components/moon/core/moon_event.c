/* Sky-event classification (moon_event.h): harvest / blue / super /
 * micro / blood, from the cheap ephemeris + the civil calendar.
 *
 * Pure C + the same eraEpv00 + eraMoon98 pair moon_geometry uses --
 * no network, no libm beyond math.h, so the device and the host
 * simulator run the exact same code. The civil date comes from an
 * integer-only epoch->YMD conversion (Hinnant's days_from_civil)
 * because the newlib device libc does not guarantee timegm.
 */
#include "moon_event.h"
#include "erfa.h" /* eraEpv00, eraMoon98 (vendored, public domain) */
#include "moon.h"
#include "moon_consts.h"

#include <string.h>

#ifdef CONFIG_BOARD_KNOB
/* The knob classifies on its producer tasks directly (sweep start + the
 * swap task's day probe); the clocks offload the scan to a worker
 * (215176b). The scan needs those tasks to yield or IDLE starves past
 * the task WDT (bench: TG1WDT reset 5 s into the boot sweep). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/* TT - UTC in the 2017..2035 range (no leap second in that window) --
 * the same pair moon_astro.c uses. */
#define TT_OFF_S 69.184

/* Full-moon day: |t - F| <= FULL_WIN_S, where F = the exact opposition
 * (the argmax of S . E, the minimum phase angle). A literal "S . E >
 * 0.999" (phase angle < 2.56 deg) NEVER fires when the full moon's
 * ecliptic latitude exceeds 2.56 deg -- at opposition the phase angle
 * IS the Moon's ecliptic latitude (up to 5.15 deg, S . E down to
 * 0.996) -- so a dot-product threshold would silently lose roughly half
 * of all full moons (and their harvest / blue / super colors). Across
 * the 18 h window the disk is at least ~98.5% illuminated (phase angle
 * up to 5.15 deg + 0.52 deg/h * 18 h), i.e. it reads full to the eye. */
#define FULL_WIN_S (18 * 3600)
/* Super-/micromoon distance bounds (km, geocentric center-to-center,
 * measured at F -- the full-moon instant itself). The 60-year census
 * (2000..2060, all 742 full moons) envelopes the full-moon distance
 * at 356,431 .. 406,528 km (closest at the 2052-12-06 full moon,
 * farthest at 2014-01-16), so the naive perigee/apogee bounds
 * (356,500 / 406,700) would fire at most ONCE in 60 years -- dead
 * code. These bounds sit ~1,000 km inside the observed extremes,
 * capturing the most extreme ~1-in-12 full moons (60 super / 61 micro
 * in 60 yr, roughly one per year): the super color for the closest
 * full moons, the micro for the farthest. */
#define SUPER_KM 357500.0
#define MICRO_KM 405500.0
/* Harvest window: the civil dates Sep 22 .. Oct 23 (the full moon
 * closest to the autumn equinox, the deterministic civil rule). */
/* Peak search resolution: 6 h coarse grid (S . E has one smooth maximum
 * per lunation, peak >= 0.9959 = cos(5.15 deg); the coarse sample is
 * within 3 h of the true peak), refined to 10 min over +-6 h. */
#define COARSE_S (6 * 3600)
#define FINE_S 600
#define FINE_WIN_S (6 * 3600)

/* UTC epoch seconds -> civil (year, month, day), UTC. Howard Hinnant's
 * days_from_civil, integer-only (no TZ, no timegm). */
static void civil_from_epoch(int64_t epoch, int *y, int *m, int *d) {
    int64_t z = epoch / 86400 + 719468; /* days since 1970-01-01 -> days since 0000-03-01 */
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                      /* [0, 146097) */
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 400) */
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               /* [0, 365] */
    int64_t mp = (5 * doy + 2) / 153;                                    /* [0, 12) */
    int64_t dd = doy - (153 * mp + 2) / 5 + 1;                           /* [1, 31] */
    int64_t mm = mp + (mp < 10 ? 3 : -9);                                /* [1, 12] */
    /* Jan and Feb (mp 0/1) are the 11th/12th months of the YEAR that
     * started on March 1, so the civil year is one less than the era
     * year for those two months (Hinnant's `+ (m <= 2)`). */
    *y = (int)(yoe + era * 400 + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}

/* S . E at the epoch (the peak search's objective). */
static double se_dot(int64_t utc_epoch) {
    moon_geom_t g;
    moon_geometry(utc_epoch, &g);
    return (double)g.S[0] * g.E[0] + (double)g.S[1] * g.E[1] + (double)g.S[2] * g.E[2];
}

/* The argmax of S . E around t0 (the fine grid over +-6 h). The coarse
 * caller is within 3 h of the true peak, so the window cannot miss it. */
static int64_t se_argmax(int64_t t0) {
    int64_t best_t = t0;
    double best = -2.0;
    for (int64_t dt = -FINE_WIN_S; dt <= FINE_WIN_S; dt += FINE_S) {
        const double v = se_dot(t0 + dt);
        if (v > best) {
            best = v;
            best_t = t0 + dt;
        }
    }
    return best_t;
}

/* The exact opposition (F) closest to t and the full moon before it
 * (F_prev; -1 if unknown) -- the blue-moon test's "previous full moon".
 *
 * S . E has one smooth maximum per lunation (29.53 d, peak >= 0.9959),
 * so the 6 h coarse grid over the 60-day window [t - 45 d, t + 15 d]
 * (which always holds >= 2 full moons: 60 d > 2 * 30 d) never aliases a
 * peak, and each coarse local maximum refines to a 10 min resolution.
 * F = the refined peak nearest t; F_prev = the refined peak before it.
 * (F_prev, not "the full moon before the instant t": t is within 18 h
 * of F, and a month holds at most two full moons, so the 2nd-of-the-month
 * test is exact on F_prev.) */
static void full_moments(int64_t t, int64_t *f, int64_t *f_prev) {
    int64_t peaks[4];
    int n = 0;
    const int64_t lo = t - 45LL * 86400;
    const int64_t hi = t + 15LL * 86400;
    double v0 = -2.0;
    int64_t t0 = lo - COARSE_S;
    double v1 = -2.0;
    int64_t t1 = lo - COARSE_S;
#ifdef CONFIG_BOARD_KNOB
    int yielded = 0;
#endif
    for (int64_t tt = lo; tt <= hi && n < 4; tt += COARSE_S) {
        const double v = se_dot(tt);
#ifdef CONFIG_BOARD_KNOB
        /* ~900 ephemeris evaluations run back-to-back on the CALLER's
         * task; at the knob's size-optimized build one costs ~8 ms, so
         * the bare loop starves IDLE past the 5 s task WDT. Yield every
         * 32 evaluations (worst case ~1/4 s unserved) -- the clocks
         * never compile this (their scan runs on an unregistered
         * worker, 215176b). */
        if ((++yielded & 31) == 0) {
            vTaskDelay(1);
        }
#endif
        if (v1 > v0 && v1 >= v) { /* a strict grid peak at t1 */
            const int64_t F = se_argmax(t1);
            /* The window's first sample, a few hours before a NEW moon
             * (S . E near -1), can register as a strict grid peak; a
             * full moon always has S . E >= 0.996, so require > 0.9. */
            if (se_dot(F) > 0.9)
                peaks[n++] = F;
        }
        v0 = v1;
        t0 = t1;
        v1 = v;
        t1 = tt;
    }
    (void)t0;
    int bi = 0;
    double bd = 1e18;
    for (int i = 0; i < n; i++) {
        const double d = peaks[i] > t ? (double)(peaks[i] - t) : (double)(t - peaks[i]);
        if (d < bd) {
            bd = d;
            bi = i;
        }
    }
    *f = peaks[bi];
    *f_prev = (bi > 0) ? peaks[bi - 1] : -1;
}

/* The full_moments scan (up to ~600 ephemeris evaluations) is the only
 * expensive step of the classify; its RESULT is stable within a
 * calendar day (the 18 h boundary crossing -- the only signature change
 * a day can see -- uses the same cached F). The device probes the
 * classify once per minute (moon_ui_day_tick, LVGL task) and the sweep /
 * orbit / dusk tasks classify their own epochs from a second core, so
 * the scan runs once per day and the rest of the day reuses it. (A torn
 * 64-bit read of s_f_* between the two cores can only make one probe see
 * a wrong full-moon instant for one minute -- the next probe re-reads;
 * the worst case is one extra re-bake, which self-corrects.) */
static int64_t s_f_day;  /* the utc_epoch / 86400 of the last scan */
static int64_t s_f_curr; /* that scan's F */
static int64_t s_f_prev; /* that scan's F_prev (-1 if unknown) */

static void full_moments_cached(int64_t t, int64_t *f, int64_t *f_prev) {
    const int64_t day = t / 86400;
    if (day != s_f_day) {
        full_moments(t, &s_f_curr, &s_f_prev);
        s_f_day = day;
    }
    *f = s_f_curr;
    *f_prev = s_f_prev;
}

void moon_event_classify(int64_t utc_epoch, moon_event_t *ev) {
    memset(ev, 0, sizeof *ev);
    moon_geom_t g;
    moon_geometry(utc_epoch, &g);
    int64_t f, f_prev;
    full_moments_cached(utc_epoch, &f, &f_prev);
    const bool is_full = (utc_epoch > f - FULL_WIN_S) && (utc_epoch < f + FULL_WIN_S);
    if (!is_full) {
        /* Only the blood test is meaningful off a full moon (an eclipse
         * can start hours before the full-moon window opens); keep it. */
        ev->harvest = ev->blue = ev->super_moon = ev->micro_moon = false;
    } else {
        int y, mo, d;
        civil_from_epoch(f, &y, &mo, &d); /* the full moon's own civil date (stable
                                             across the whole 18 h window) */
        ev->harvest = (mo == 9 && d >= 22) || (mo == 10 && d <= 23);
        ev->blue = false;
        if (f_prev >= 0) {
            int py, pm, pd;
            civil_from_epoch(f_prev, &py, &pm, &pd);
            ev->blue = (py == y) && (pm == mo); /* the 2nd full moon of the month */
        }
        /* Super/micro at the full-moon instant F itself (the 18 h
         * window's distance can drift a few hundred km; the thresholds
         * are meant at opposition). One extra ephemeris evaluation. */
        moon_geom_t gf;
        moon_geometry(f, &gf);
        ev->super_moon = (gf.distance_km < SUPER_KM);
        ev->micro_moon = (gf.distance_km > MICRO_KM);
    }

    /* Blood: is the Moon's center inside the Earth's shadow cone at the
     * epoch? The axis geometry (moon_astro fills these from the exact
     * geocentric Sun/Moon positions, not the Moon-centric -S): the
     * umbra cone's axis passes through g.shadow_off (the closest point
     * of the axis, body frame, meters from the Moon's center) in the
     * unit direction g.shadow_dir; the cone radii at the axis
     * coordinate a (m from the Earth) are linear (see moon_event.h). */
    /* The Moon center's axial coordinate a (m from the Earth) = mg . s,
     * filled by moon_astro from the exact geocentric positions. (The
     * tempting shadow_off . shadow_dir is ~0 BY CONSTRUCTION -- shadow_off
     * is the axis's closest point to the Moon center, hence perpendicular
     * to the axis -- so it is not the axis coordinate.) */
    const double a = (double)g.off_ax;
    const double off2 = (double)g.shadow_off[0] * g.shadow_off[0] + (double)g.shadow_off[1] * g.shadow_off[1] +
                        (double)g.shadow_off[2] * g.shadow_off[2];
    const double off = sqrt(off2);
    const double tip = (double)MOON_EV_UMBRA_TIP_M;
    double ru = MOON_EV_EARTH_R_M * (1.0 - a / tip);
    if (ru < 0.0)
        ru = 0.0;
    const double rp = MOON_EV_EARTH_R_M + a * MOON_EV_PENUMBRA_SLOPE;
    ev->blood = off < rp; /* the center in the penumbra = any eclipse phase */
    if (ev->blood) {
        ev->shadow_dir[0] = g.shadow_dir[0];
        ev->shadow_dir[1] = g.shadow_dir[1];
        ev->shadow_dir[2] = g.shadow_dir[2];
        ev->shadow_off[0] = g.shadow_off[0];
        ev->shadow_off[1] = g.shadow_off[1];
        ev->shadow_off[2] = g.shadow_off[2];
        ev->off_ax = (float)a;
    }
}
