/* Throwaway: print C bake geometry + frame bytes at named pixels to settle
 * the dark-side speckle diagnosis. Not part of the gate.
 *
 * Build:
 *   cc -O2 -Imoon/include -I. dark_test.c <core objs> -o dark_test
 * Run: ./dark_test "2026-10-05T12:00" 466 241,414 250,420 233,33
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

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

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: dark_test ISO SIZE x,y ...\n");
        return 2;
    }
    int64_t epoch = parse_iso(argv[1]);
    int size = atoi(argv[2]);
    int radius = (int)MOON_DISK_RADIUS_PX;
    int center = size / 2;

    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = moon_relief();
    moon_geom_t g;
    moon_geometry(epoch, &g);
    fprintf(stderr, "epoch=%s  subsolar=(%.2f,%.2f)  illum=%.3f\n", argv[1], g.subsolar_lon, g.subsolar_lat, g.illum);
    fprintf(stderr, "S=(%.4f,%.4f,%.4f) E=(%.4f,%.4f,%.4f)\n", g.S[0], g.S[1], g.S[2], g.E[0], g.E[1], g.E[2]);
    fprintf(stderr, "right=(%.4f,%.4f,%.4f) up=(%.4f,%.4f,%.4f)\n", g.right[0], g.right[1], g.right[2], g.up[0],
            g.up[1], g.up[2]);

    int sz = size;
    static uint8_t *frame0, *frame1;
    frame0 = malloc((size_t)sz * sz);
    frame1 = malloc((size_t)sz * sz);
    moon_bake(&g, alb, grel, frame0, sz, (float)radius, (float)center, (float)center, MOON_ALB_W, MOON_ALB_H, 0, 0.0f,
              NULL);
    moon_bake(&g, alb, grel, frame1, sz, (float)radius, (float)center, (float)center, MOON_ALB_W, MOON_ALB_H, 1, 0.0f,
              NULL);

    for (int i = 3; i < argc; i++) {
        int x, y;
        if (sscanf(argv[i], "%d,%d", &x, &y) != 2)
            continue;
        float u = ((float)x - (float)center) / (float)radius;
        float v = ((float)center - (float)y) / (float)radius;
        float r2 = u * u + v * v;
        float w = sqrtf(fmaxf(0.0f, 1.0f - r2));
        float rx = u * g.right[0] + v * g.up[0] + w * g.E[0];
        float ry = u * g.right[1] + v * g.up[1] + w * g.E[1];
        float rz = u * g.right[2] + v * g.up[2] + w * g.E[2];
        float d = rx * g.S[0] + ry * g.S[1] + rz * g.S[2];
        /* dark_zero = 1 now writes the COMPUTED backlit night side (not
         * the flat floor), so a d < 0 pixel is non-zero BY DESIGN -- no
         * BUG marker. */
        fprintf(stderr, "px(%d,%d) u=%.4f v=%.4f d=%.5f  gray[dz=0]=%u  gray[dz=1]=%u\n", x, y, u, v, d,
                frame0[y * sz + x], frame1[y * sz + x]);
    }
    return 0;
}
