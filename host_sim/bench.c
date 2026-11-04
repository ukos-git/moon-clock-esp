/* Performance bench for the portable moon core: times the device path's
 * individual steps (geometry, relief build, bake, rotate, shade-map
 * build, re-project) on the host. The numbers in the book's Part IV
 * (chapter 19) come from this file + the Python/JS comparators:
 *
 *   host_sim/build/moon_bench --utc 2026-04-15T12:00 --sizes 200 240 320 400 466 618 650
 *
 * Build: the moon_sim target's source set + bench.c (see bench CMake or
 * the book's commands). All times are wall clock, average of --iters
 * bakes (the relief build is reported once, first run only).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

/* ISO "YYYY-MM-DDTHH:MM" -> epoch seconds (same civil math as main.c). */
static int parse_iso(const char *s, int64_t *epoch) {
    int y, m, d, h, mi;
    if (sscanf(s, "%d-%d-%dT%d:%d", &y, &m, &d, &h, &mi) < 3)
        return 0;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    *epoch = ((long long)era * 146097 + doe - 719468) * 86400LL + h * 3600LL + mi * 60LL;
    return 1;
}

int main(int argc, char **argv) {
    const char *iso = "2026-04-15T12:00";
    int iters = 5;
    int sizes[16], nsizes = 0;
    int no_relief = 0, darkzero = 0, neutral = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--utc"))
            iso = argv[++i];
        else if (!strcmp(argv[i], "--iters"))
            iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sizes")) {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (nsizes < 16)
                    sizes[nsizes++] = atoi(argv[++i]);
            }
        } else if (!strcmp(argv[i], "--no-relief"))
            no_relief = 1;
        else if (!strcmp(argv[i], "--darkzero"))
            darkzero = 1;
        else if (!strcmp(argv[i], "--neutral"))
            neutral = 1;
    }
    if (nsizes == 0)
        sizes[nsizes++] = 400;
    if (neutral)
        moon_gray565_set_warm(0);

    int64_t epoch;
    if (!parse_iso(iso, &epoch)) {
        fprintf(stderr, "bad time '%s'\n", iso);
        return 1;
    }

    /* geometry: average of iters calls (the ERFA series; per frame on
     * the device too -- one per bake). */
    moon_geom_t g;
    double t0 = now_ms();
    for (int k = 0; k < iters; k++)
        moon_geometry(epoch, &g);
    double geo_ms = (now_ms() - t0) / iters;
    printf("utc=%s  geo=%.1f ms/call  relief=%s  darkzero=%d\n", iso, geo_ms, no_relief ? "off" : "on", darkzero);

    const uint8_t *alb = moon_albedo();
    /* relief build: first run only (the device builds it once at boot). */
    const moon_grel_t *grel = NULL;
    double relief_ms = -1;
    if (!no_relief) {
        t0 = now_ms();
        grel = moon_relief();
        relief_ms = now_ms() - t0;
    } else {
        moon_relief(); /* still build it so the no-relief A/B is the bake's */
        grel = NULL;
    }
    if (!alb || (grel == NULL && !no_relief)) {
        fprintf(stderr, "no albedo/relief\n");
        return 1;
    }

    for (int s = 0; s < nsizes; s++) {
        int size = sizes[s];
        int radius = size / 2, center = size / 2;
        uint8_t *frame = malloc((size_t)size * size);
        uint16_t *rgb = malloc((size_t)size * size * 2);

        /* bake: the per-pixel model (bake is the expensive step). */
        t0 = now_ms();
        for (int k = 0; k < iters; k++)
            moon_bake(&g, alb, grel, frame, size, (float)radius, (float)center, (float)center, MOON_ALB_W, MOON_ALB_H,
                      darkzero, 0.0f, NULL); /* NULL event = the pre-event baseline path (byte-identical) */
        double bake_ms = (now_ms() - t0) / iters;

        /* rotate: the spin's per-frame gather (Q15.15 + fused LUT). */
        float th = 0.7;
        t0 = now_ms();
        for (int k = 0; k < iters; k++)
            moon_rotate(frame, size, rgb, size, center, center, center, center, moon_cos15(th), moon_sin15(th));
        double rot_ms = (now_ms() - t0) / iters;

        /* re-project: the dusk's per-frame map gather (one map load/px). */
        uint8_t *map = malloc((size_t)MOON_ALB_W * MOON_ALB_H);
        t0 = now_ms();
        moon_shade_map_build(&g, alb, grel, map, NULL, MOON_ALB_W, MOON_ALB_H, 1, 0.1f, NULL);
        double map_ms = now_ms() - t0;
        t0 = now_ms();
        for (int k = 0; k < iters; k++)
            moon_reproject(map, MOON_ALB_W, MOON_ALB_H, &g, frame, size, (float)radius, (float)center, (float)center);
        double reproj_ms = (now_ms() - t0) / iters;

        printf("size=%4d  bake=%8.2f ms  rotate=%6.2f ms  mapbuild=%8.2f ms  reproj=%6.2f ms\n", size, bake_ms, rot_ms,
               map_ms, reproj_ms);
        free(frame);
        free(rgb);
        free(map);
    }
    if (relief_ms >= 0)
        printf("relief build (once): %.1f ms (%.0f px DEM)\n", relief_ms, (double)MOON_DEM_W * MOON_DEM_H);
    return 0;
}
