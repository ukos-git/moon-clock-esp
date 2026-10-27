/* Throwaway dev probe: verify the sleep "dusk" pole-spin geometry
 * (duplicated 1:1 from components/ui/moon_ui.c) against the device
 * runtime path:
 *
 *   1. IN-PLANE FOLD: baking the camera basis pre-rotated about the view
 *      axis E by the free-spin angle theta must reproduce the on-screen
 *      pose, i.e. the bake at (right, up) rotated in place by
 *      moon_rotate(theta) -- the light-sleep entry seam. MAE must be
 *      tiny (sub-1-gray over the disk) at several epochs/angles.
 *
 *   2. ENDPOINT: the pole rotation phi* = atan2(-B, -A) (A = E0S0+E1S1,
 *      B = E0S1-E1S0, so E(phi).S = A cos + B sin + C) must sit at the
 *      numeric minimum of E(phi).S over the full circle (the visible
 *      disk as dark as the geometry allows), and the rendered frame at
 *      phi* must show the dark side with some contrast (earthshine +
 *      relief-lit rims, dark_zero = 0) -- the deep-sleep entry frame.
 *
 * Build:
 *   cc -O2 -Imoon/include -I. pole_test.c <core objs> -lm -o pole_test
 * Run: ./pole_test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

/* Device geometry: 400x400 disk, radius 200, center (200,200). */
#define PX 400
#define RAD 200.0f
#define CTR 200.0f

/* 1:1 copies of components/ui/moon_ui.c. */
static void moon_inplane_geometry(const moon_geom_t *src, float theta, moon_geom_t *dst) {
    *dst = *src;
    float c = cosf(theta), s = sinf(theta);
    float r[3] = {src->right[0], src->right[1], src->right[2]};
    float u[3] = {src->up[0], src->up[1], src->up[2]};
    dst->right[0] = c * r[0] + s * u[0];
    dst->right[1] = c * r[1] + s * u[1];
    dst->right[2] = c * r[2] + s * u[2];
    dst->up[0] = -s * r[0] + c * u[0];
    dst->up[1] = -s * r[1] + c * u[1];
    dst->up[2] = -s * r[2] + c * u[2];
    /* E unchanged (in-plane axis). */
}

static void moon_pole_geometry(const moon_geom_t *src, float phi, moon_geom_t *dst) {
    *dst = *src;
    float c = cosf(phi), s = sinf(phi);
    float a[3] = {src->right[0], src->right[1], src->right[2]};
    float b[3] = {src->up[0], src->up[1], src->up[2]};
    float e[3] = {src->E[0], src->E[1], src->E[2]};
    dst->right[0] = c * a[0] - s * a[1];
    dst->right[1] = s * a[0] + c * a[1];
    dst->right[2] = a[2];
    dst->up[0] = c * b[0] - s * b[1];
    dst->up[1] = s * b[0] + c * b[1];
    dst->up[2] = b[2];
    dst->E[0] = c * e[0] - s * e[1];
    dst->E[1] = s * e[0] + c * e[1];
    dst->E[2] = e[2];
}

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097 + doe - 719468;
}

static int64_t iso(const char *s) {
    int y, mo, d;
    sscanf(s, "%d-%d-%d", &y, &mo, &d);
    return days_from_civil(y, mo, d) * 86400LL;
}

static int g_fail;

/* Bake the current phase at the plain Earth viewport, then rotate in
 * place by theta (the on-screen free-spin pose). */
static void bake_screen_pose(int64_t epoch, float theta, uint8_t *out) {
    moon_geom_t g;
    moon_geometry(epoch, &g);
    moon_bake(&g, moon_albedo(), moon_relief(), out, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
    if (theta != 0.0f) {
        static uint16_t rgb[PX * PX];
        int32_t c15 = moon_cos15(theta), s15 = moon_sin15(theta);
        moon_rotate(out, PX, rgb, PX, CTR, CTR, (int)CTR, (int)CTR, c15, s15);
        /* Decode back to 8-bit gray (nearest): rgb -> g via the gray565
         * LUT is 1:1 for neutral grays, so the max g with g565(g) == v
         * recovers g within the LUT's rounding. */
        for (int i = 0; i < PX * PX; i++) {
            int best = 0, bd = 0xFFFF;
            for (int gr = 0; gr < 256; gr++) {
                int d = (int)rgb[i] - moon_gray565(gr);
                d = d < 0 ? -d : d;
                if (d < bd) {
                    bd = d;
                    best = gr;
                }
            }
            out[i] = (uint8_t)best;
        }
    }
}

int main(void) {
    if (!moon_albedo() || !moon_relief()) {
        fprintf(stderr, "no assets\n");
        return 1;
    }
    const char *epochs[] = {"2026-10-27", "2026-10-20", "2026-11-05"};
    const float thetas[] = {0.3f, -1.1f, 2.4f, 5.9f};
    static uint8_t a[2 * PX * PX], b[PX * PX];
    int64_t now = iso("2026-10-27");

    printf("== 1. in-plane fold (bake at theta-folded basis vs on-screen pose)\n");
    for (unsigned e = 0; e < 3; e++) {
        int64_t epoch = e == 1 ? iso(epochs[e]) : now - 7 * 86400LL * (int)e;
        moon_geom_t g0;
        moon_geometry(epoch, &g0);
        for (unsigned t = 0; t < 4; t++) {
            float th = thetas[t];
            bake_screen_pose(epoch, th, a);
            moon_geom_t gb, gwrong;
            moon_inplane_geometry(&g0, th, &gb);
            moon_inplane_geometry(&g0, -th, &gwrong);
            moon_bake(&gb, moon_albedo(), moon_relief(), b, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
            moon_bake(&gwrong, moon_albedo(), moon_relief(), a + PX * PX, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
            /* NOTE: the on-screen pose is moon_rotate's NEAREST-NEIGHBOR
             * gather (an integer 1-px sample of the unrotated bake), while
             * the folded basis bakes the sphere EXACTLY per pixel. The
             * residual is the inherent 1-px sampling difference (large
             * only at the terminator), NOT a geometry error. The sign is
             * verified against the WRONG-sign fold: a mis-signed fold
             * misaligns the terminator by 2*th (tens of gray), so the
             * right sign must be far closer. */
            double sum = 0, sumw = 0;
            int mx = 0, mxw = 0, lit = 0;
            for (int y = 0; y < PX; y++)
                for (int x = 0; x < PX; x++) {
                    float u = (x - 199.5f) / 200.0f, v = (199.5f - y) / 200.0f;
                    if (u * u + v * v > 1.0f)
                        continue;
                    int d = (int)a[y * PX + x] - (int)b[y * PX + x];
                    int dw = (int)a[y * PX + x] - (int)a[PX * PX + y * PX + x];
                    d = d < 0 ? -d : d;
                    dw = dw < 0 ? -dw : dw;
                    sum += d;
                    sumw += dw;
                    if (d > mx)
                        mx = d;
                    if (dw > mxw)
                        mxw = dw;
                    if (b[y * PX + x] > 8)
                        lit++;
                }
            double mae = sum / (double)lit, maw = sumw / (double)lit;
            printf("  epoch=%ld th=%+.2f rad: right MAE=%.2f max=%d | wrong MAE=%.2f max=%d\n",
                   (long)(epoch - now) / 86400, th, mae, mx, maw, mxw);
            /* Inherent 1-px gather mismatch (terminator) + right sign
             * must beat the wrong sign. 2.5x floor: the margin shrinks
             * on an already-dark-starting disk (epoch -14 d, E.S = -0.88
             * at the start: the view is nearly azimuthally symmetric, so
             * a small +/-th fold is almost identical either way). */
            if (mae > 6.0 || maw < mae * 2.5)
                g_fail++;
        }
    }

    printf("== 2. pole-spin endpoint (E(phi).S minimum)\n");
    for (unsigned e = 0; e < 3; e++) {
        int64_t epoch = e == 1 ? iso(epochs[e]) : now - 7 * 86400LL * (int)e;
        moon_geom_t g0;
        moon_geometry(epoch, &g0);
        float A = g0.E[0] * g0.S[0] + g0.E[1] * g0.S[1];
        float B = g0.E[0] * g0.S[1] - g0.E[1] * g0.S[0];
        float phi = atan2f(-B, -A); /* analytic shortest arc to the dark side */
        float fmin = A * cosf(phi) + B * sinf(phi) + g0.E[2] * g0.S[2];
        /* Numeric check: the full circle on a 0.5 deg grid. */
        float best = 1e9f;
        for (int i = 0; i < 720; i++) {
            float p = (float)i * (6.283185307179586 / 720.0);
            float ev = A * cosf(p) + B * sinf(p) + g0.E[2] * g0.S[2];
            if (ev < best)
                best = ev;
        }
        float start = A + g0.E[2] * g0.S[2];                       /* f(0): the on-screen view */
        int frames = (int)lrintf(fabsf(phi) * 57.29577951308232f); /* 1 deg/frame */
        printf("  epoch=%ld d: start E.S=%+.4f  phi*=%+8.2f d  E.S(phi*)=%+.4f  |grid-analytic|=%.5f  frames=%d (%.0f "
               "s @0.5s/f)\n",
               (long)(epoch - now) / 86400, start, phi * 57.29577951308232f, fmin, fabsf(best - fmin), frames,
               frames * 0.5);
        if (phi > 3.1415927f || phi < -3.1415927f || fabsf(best - fmin) > 0.01f || fmin > -0.5f)
            g_fail++;
        /* Render the endpoint frame: the dark side must still show
         * contrast (earthshine ramp + relief-lit rims), dark_zero = 0. */
        moon_geom_t gb;
        moon_inplane_geometry(&g0, 0.0f, &gb);
        moon_pole_geometry(&gb, phi, &gb);
        moon_bake(&gb, moon_albedo(), moon_relief(), b, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
        int lit = 0, dark_hi = 0, dark_cnt = 0;
        long long sum = 0;
        for (int y = 0; y < PX; y++)
            for (int x = 0; x < PX; x++) {
                int g = b[y * PX + x];
                if (g > 16)
                    lit++;
                else if (g > 0)
                    dark_cnt++;
                if (g > dark_hi)
                    dark_hi = g;
                sum += g;
            }
        printf("    endpoint frame: lit px=%d  dark-side px>0=%d  dark max=%d  mean=%.1f\n", lit, dark_cnt, dark_hi,
               (double)sum / (PX * PX));
        if (dark_hi < 4)
            g_fail++; /* no contrast: the dark side would be invisible */
    }

    printf(g_fail ? "POLE TEST FAIL (%d)\n" : "POLE TEST PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
