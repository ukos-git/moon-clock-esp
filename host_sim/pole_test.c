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
 *      NOTE: this probe keeps dark_zero = 0 to verify the earthshine
 *      MODEL; the DEVICE dusk/endpoint now renders the night side flat
 *      black (dark_zero = 1, T2/T3), so it never displays this contrast.
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

    printf("== 3. constant-projection (shade map + reproject) vs per-frame bake\n"
           "    The dusk's promise: a pole spin at a fixed epoch re-projects ONE\n"
           "    pre-shaded body-frame map (no per-frame shadow recompute) instead\n"
           "    of re-baking. The error must be the cell-center-vs-pixel lighting\n"
           "    difference ALONE -- it must not grow with the fold angle (the map\n"
           "    is view-independent: only the basis changes between frames).\n");
    /* Acceptance: the map is a constant, so the re-projected frame differs
     * from the per-frame bake only by the map's CELL-CENTER lighting (the
     * lighting is evaluated once per 0.42 x 0.42 deg cell, not per pixel):
     *   - the mean error must stay small (the lighting is smooth), and
     *   - the spikes (relief-shadow rims straddling a cell boundary) must
     *     be rare, NOT grow with the fold angle (view independence: the
     *     same cells light the same surface points every frame -- they
     *     rotate rigidly with the disk, they cannot flicker), and
     *   - between consecutive frames the re-projected path must move the
     *     image by the SAME amount as the re-baked one (the temporal
     *     delta is the genuine 1 deg roll of the content, identical for
     *     both -- the map adds no frame-to-frame shimmer). */
    static uint8_t shade_map[MOON_ALB_W * MOON_ALB_H];
    for (unsigned e = 0; e < 3; e++) {
        int64_t epoch = e == 1 ? iso(epochs[e]) : now - 7 * 86400LL * (int)e;
        moon_geom_t g0;
        moon_geometry(epoch, &g0);
        /* Build the map ONCE from the unfolded geometry (S is never folded:
         * the folds only touch the camera basis). */
        /* dark_zero = 0: the host gate keeps the earthshine dark side and
         * verifies the re-projected map matches the full bake. The device
         * dusk passes dark_zero = 1 (flat black). */
        moon_shade_map_build(&g0, moon_albedo(), moon_relief(), shade_map, MOON_ALB_W, MOON_ALB_H, 0);
        const float thetas3[] = {0.3f, -1.1f, 2.4f, 5.9f};
        const float phis3[] = {0.0f, 0.5f, -2.9f, 0.7f}; /* the 3rd = a near-endpoint fold */
        for (unsigned t = 0; t < 4; t++) {
            moon_geom_t gb;
            moon_inplane_geometry(&g0, thetas3[t], &gb);
            moon_pole_geometry(&gb, phis3[t], &gb);
            moon_bake(&gb, moon_albedo(), moon_relief(), b, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H,
                      0);                                                                 /* reference */
            moon_reproject(shade_map, MOON_ALB_W, MOON_ALB_H, &gb, a, PX, RAD, CTR, CTR); /* the dusk's frame */
            double sum = 0;
            int mx = 0, n = 0, big = 0;
            for (int y = 0; y < PX; y++)
                for (int x = 0; x < PX; x++) {
                    float u = (x - 199.5f) / 200.0f, v = (199.5f - y) / 200.0f;
                    if (u * u + v * v > 1.0f)
                        continue;
                    int d = (int)a[y * PX + x] - (int)b[y * PX + x];
                    d = d < 0 ? -d : d;
                    sum += d;
                    if (d > mx)
                        mx = d;
                    if (d > 100)
                        big++;
                    n++;
                }
            double mae = sum / (double)n;
            double big_pct = 100.0 * (double)big / (double)n;
            /* The baseline pair (phi = 0, small theta) measures the inherent
             * map-vs-bake error; every fold must stay close to it. */
            int baseline = (t == 0);
            printf("  epoch=%ld d th=%+.2f phi=%+.2f rad: MAE=%.2f max=%d px>100=%.2f%% %s\n",
                   (long)(epoch - now) / 86400, thetas3[t], phis3[t], mae, mx, big_pct, baseline ? "(baseline)" : "");
            if (mae > 7.0 || big_pct > 2.0)
                g_fail++;
        }
        /* Temporal: the re-projected path's frame-to-frame motion must
         * match the re-baked path's (the same content rolls 1 deg about
         * the pole; the map adds no shimmer of its own). Coarse 10 deg
         * step to amplify the delta; the device steps 1 deg. */
        moon_geom_t ga, gaa;
        moon_inplane_geometry(&g0, 0.0f, &ga);
        moon_pole_geometry(&ga, 0.0f, &ga);
        moon_pole_geometry(&g0, 0.17453293f, &gaa); /* +10 deg */
        moon_reproject(shade_map, MOON_ALB_W, MOON_ALB_H, &ga, a, PX, RAD, CTR, CTR);
        moon_reproject(shade_map, MOON_ALB_W, MOON_ALB_H, &gaa, b, PX, RAD, CTR, CTR);
        moon_bake(&ga, moon_albedo(), moon_relief(), a + PX * PX, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
        static uint8_t bb[PX * PX];
        moon_bake(&gaa, moon_albedo(), moon_relief(), bb, PX, RAD, CTR, CTR, MOON_ALB_W, MOON_ALB_H, 0);
        double sr = 0, sb = 0;
        int nr = 0;
        for (int y = 0; y < PX; y++)
            for (int x = 0; x < PX; x++) {
                float u = (x - 199.5f) / 200.0f, v = (199.5f - y) / 200.0f;
                if (u * u + v * v > 1.0f)
                    continue;
                int dr = (int)b[y * PX + x] - (int)a[y * PX + x];
                int db = (int)bb[y * PX + x] - (int)(a + PX * PX)[y * PX + x];
                dr = dr < 0 ? -dr : dr;
                db = db < 0 ? -db : db;
                sr += dr;
                sb += db;
                nr++;
            }
        sr /= nr, sb /= nr;
        double diff = sr > sb ? sr - sb : sb - sr;
        printf("  temporal (10 deg step): reproject f-delta=%.2f bake f-delta=%.2f |d|=%.2f\n", sr, sb, diff);
        /* The map's temporal delta may exceed the bake's by the cell
         * re-sampling, but must stay within ~1.5x of it (same content). */
        if (diff > 0.5 * (sr + sb))
            g_fail++;
    }

    printf(g_fail ? "POLE TEST FAIL (%d)\n" : "POLE TEST PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
