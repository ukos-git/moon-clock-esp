/* Throwaway dev probe: verify the new_moon_epoch() coarse+fine scan
 * (duplicated 1:1 from components/ui/moon_ui.c) against the cheap
 * ephemeris: it must return the darkest epoch of the last ~29.53 d,
 * with illumination near 0 and a sane span.
 *
 * Build:
 *   cc -O2 -Imoon/include -I. newmoon_test.c <core objs> -lm -o newmoon_test
 * Run: ./newmoon_test "2026-10-27T12:00"
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

/* 1:1 copy of components/ui/moon_ui.c::new_moon_epoch. */
static int64_t new_moon_epoch(int64_t utc_epoch) {
    moon_geom_t g;
    int64_t best = utc_epoch - (int64_t)(29.53 * 86400);
    float best_illum = 1.0f;
    for (int64_t e = utc_epoch - (int64_t)(6 * 3600); e >= utc_epoch - (int64_t)(29.53 * 86400); e -= 6 * 3600) {
        g.illum = 1.0f;
        moon_geometry(e, &g);
        if (g.illum < best_illum - 1e-6f) {
            best_illum = g.illum;
            best = e;
        }
    }
    int64_t fine_best = best;
    float fine_illum = best_illum;
    for (int64_t e = best + 6 * 3600; e >= best - 6 * 3600; e -= 20 * 60) {
        g.illum = 1.0f;
        moon_geometry(e, &g);
        if (g.illum < fine_illum - 1e-6f) {
            fine_illum = g.illum;
            fine_best = e;
        }
    }
    return fine_best;
}

static void fmt(int64_t e, char *out, size_t n) {
    time_t t = (time_t)e;
    struct tm *lt = gmtime(&t);
    strftime(out, n, "%Y-%m-%d %H:%M UTC", lt);
}

int main(int argc, char **argv) {
    int64_t now = argc > 1 ? parse_iso(argv[1]) : 1761600000;
    char ts[64];
    moon_geom_t g;
    moon_geometry(now, &g);
    fmt(now, ts, sizeof ts);
    printf("now            = %s  illum=%.4f\n", ts, g.illum);
    int64_t nm = new_moon_epoch(now);
    moon_geom_t gn;
    moon_geometry(nm, &gn);
    fmt(nm, ts, sizeof ts);
    double span_d = (double)(now - nm) / 86400.0;
    printf("new moon       = %s  illum=%.4f  span=%.2f d\n", ts, gn.illum, span_d);
    /* Brute-force check on a fine grid over the WHOLE 29.53 d window
     * (now -> now-29.53 d): the scan's result must be within 1 fine step
     * (20 min) of the true minimum. */
    moon_geom_t g0;
    float min_illum = 1.0f;
    int64_t min_e = now;
    for (int64_t e = now; e >= now - (int64_t)(29.53 * 86400); e -= 20 * 60) {
        g0.illum = 1.0f;
        moon_geometry(e, &g0);
        if (g0.illum < min_illum) {
            min_illum = g0.illum;
            min_e = e;
        }
    }
    int64_t off = (nm > min_e) ? nm - min_e : min_e - nm;
    printf("brute-force min = illum=%.4f  |scan-min| offset = %lld s (%.0f min)\n", min_illum, (long long)off,
           off / 60.0);
    return (gn.illum < 0.05f && span_d > 0.5 && span_d <= 29.53 && off < 40 * 60) ? 0 : 1;
}
