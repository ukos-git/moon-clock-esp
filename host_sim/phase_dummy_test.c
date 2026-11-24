/* Throwaway dev probe: verify moon_bake_phase_dummy() (the computation
 * placeholder disc, components/moon/core/moon_bake.c:592) against its own
 * contract with synthetic orthonormal geoms, phase by phase:
 *
 *   out-of-disk  = 0 (pure black, never a stray tint)
 *   new moon     (S = -E)  -> the WHOLE visible disk is TONE_LUT[0] = 0
 *   full moon    (S = +E)  -> center = TONE_LUT[255], limb darker
 *                             (Lambert falloff: color decreases with r)
 *   quarter      (S = right) -> lit fraction of the disk ~= illum (~0.5),
 *                             lit half on the S-facing (u > 0) side
 *
 * No ephemeris, no real epoch: the basis (right, up, E) is a fixed
 * right-handed orthonormal frame and S is aimed at the desired phase, so
 * the projection's lighting is known analytically. illum is set to
 * (1 + S . E) / 2, the geom's own definition, so the quarter lit-fraction
 * check compares the baked disc to the geometry it was given.
 *
 * Build (from components/moon, the generated consts carry MOON_TONE_LUT):
 *   cc -O2 -std=gnu11 -Iinclude -Icore -Icore/erfa -Igenerated/s3 \
 *       ../../host_sim/phase_dummy_test.c \
 *       core/moon_bake.c core/moon_astro.c core/moon_viewport.c \
 *       core/moon_render.c core/moon_fixp.c core/moon_event.c \
 *       core/erfa/*.c -lm -o /tmp/phase_dummy_test
 * Run: /tmp/phase_dummy_test            (exits 0 iff every assertion holds)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "moon.h"
#include "moon_consts.h"

#define SIZE 201
#define RADIUS 100.0f
#define CX 100.0f
#define CY 100.0f

static int fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

/* Fill a geom with a fixed right-handed orthonormal basis and a chosen
 * sun direction S. E = +z (visible pole), right = +x, up = +y, so right x
 * up = E and the projection's lighting is analytic. */
static void make_geom(moon_geom_t *g, const float *S) {
    memset(g, 0, sizeof *g);
    g->E[0] = 0.0f;
    g->E[1] = 0.0f;
    g->E[2] = 1.0f;
    g->right[0] = 1.0f;
    g->right[1] = 0.0f;
    g->right[2] = 0.0f;
    g->up[0] = 0.0f;
    g->up[1] = 1.0f;
    g->up[2] = 0.0f;
    g->S[0] = S[0];
    g->S[1] = S[1];
    g->S[2] = S[2];
    g->illum = (1.0f + (g->S[0] * g->E[0] + g->S[1] * g->E[1] + g->S[2] * g->E[2])) / 2.0f;
}

/* A disk sample at image offset (du, dv) pixels from center. */
static unsigned char px(const uint8_t *f, int dx, int dy) {
    int x = (int)CX + dx, y = (int)CY + dy;
    return f[(size_t)y * SIZE + x];
}

int main(void) {
    uint8_t *f = calloc((size_t)SIZE * SIZE, 1);
    if (!f)
        return 2;
    moon_geom_t g;
    int bad = 0;

    /* --- full moon (S = +E): center TONE_LUT[255], Lambert limb falloff --- */
    const float S_full[3] = {0, 0, 1};
    make_geom(&g, S_full);
    moon_bake_phase_dummy(&g, f, SIZE, RADIUS, CX, CY);

    /* out-of-disk stays black: the frame corners and points just past the
     * limb (r2 = 75^2 + 75^2 / 100^2 = 1.125 > 1), all inside the frame. */
    if (f[0] != 0 || f[SIZE * SIZE - 1] != 0 || px(f, 75, 75) != 0 || px(f, -75, 75) != 0)
        bad |= fail("full: out-of-disk pixel not black");
    /* center is the fully-lit bin. */
    if (px(f, 0, 0) != MOON_TONE_LUT[255])
        bad |= fail("full: center != TONE_LUT[255]");
    /* Lambert: the CONTINUOUS light d = w = sqrt(1 - u^2) must strictly
     * decrease with radius (the tone LUT's upper bins are 5/6/5-quantized
     * and jitter +-a few levels there, so the rendered bytes are only
     * asserted non-increasing, with a hard falloff by the limb). */
    float u10 = 0.1f, u40 = 0.4f, u70 = 0.7f, u95 = 0.95f;
    float w10 = sqrtf(1 - u10 * u10), w40 = sqrtf(1 - u40 * u40), w70 = sqrtf(1 - u70 * u70),
          w95 = sqrtf(1 - u95 * u95);
    if (!(w10 > w40 && w40 > w70 && w70 > w95))
        bad |= fail("full: Lambert light not strictly decreasing (geometry wrong)");
    unsigned char c10 = px(f, 10, 0), c40 = px(f, 40, 0), c70 = px(f, 70, 0), c95 = px(f, 95, 0);
    if (!(c10 >= c40 && c40 >= c70 && c70 >= c95))
        bad |= fail("full: rendered limb not non-increasing");
    if (c70 - (int)c95 < 50)
        bad |= fail("full: no hard limb falloff by u = 0.95");
    /* the whole interior is lit (only the exact limb may read dark). */
    {
        int lit = 0, total = 0;
        for (int dy = -RADIUS; dy <= RADIUS; dy++)
            for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                float u = dx / RADIUS, v = dy / RADIUS;
                if (u * u + v * v <= 1.0f) {
                    total++;
                    if (px(f, dx, dy) > 0)
                        lit++;
                }
            }
        if ((float)lit / total < 0.99f)
            bad |= fail("full: interior not (almost) fully lit");
        (void)total;
    }

    /* --- new moon (S = -E): the entire visible disk is TONE_LUT[0] = 0 --- */
    const float S_new[3] = {0, 0, -1};
    make_geom(&g, S_new);
    moon_bake_phase_dummy(&g, f, SIZE, RADIUS, CX, CY);
    {
        int dark = 0, total = 0;
        for (int dy = -RADIUS; dy <= RADIUS; dy++)
            for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                float u = dx / RADIUS, v = dy / RADIUS;
                if (u * u + v * v <= 1.0f) {
                    total++;
                    if (px(f, dx, dy) == 0)
                        dark++;
                }
            }
        if (dark != total)
            bad |= fail("new: visible disk not fully dark (no earthshine)");
    }

    /* --- quarter (S = right): lit fraction ~= illum ~0.5, on u>0 side --- */
    const float S_q[3] = {1, 0, 0};
    make_geom(&g, S_q);
    moon_bake_phase_dummy(&g, f, SIZE, RADIUS, CX, CY);
    {
        int lit = 0, dark_side_lit = 0, total = 0;
        for (int dy = -RADIUS; dy <= RADIUS; dy++)
            for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                float u = dx / RADIUS, v = dy / RADIUS;
                if (u * u + v * v <= 1.0f) {
                    total++;
                    unsigned char c = px(f, dx, dy);
                    if (c > 0) {
                        lit++;
                        if (dx < 0)
                            dark_side_lit++; /* the u < 0 half must stay dark */
                    }
                }
            }
        float frac = (float)lit / total;
        if (fabsf(frac - 0.5f) > 0.03f)
            bad |= fail("quarter: lit fraction far from 0.5");
        if (dark_side_lit > total / 200)
            bad |= fail("quarter: lit pixels leaked onto the dark (u < 0) half");
        if (fabsf(frac - g.illum) > 0.05f)
            bad |= fail("quarter: lit fraction != g.illum");
        printf("quarter: lit fraction %.3f vs illum %.3f (disc lit=%d/%d px)\n", frac, g.illum, lit, total);
    }

    free(f);
    if (bad) {
        fprintf(stderr, "phase_dummy_test: %d assertion(s) failed\n", 1);
        return 1;
    }
    printf("phase_dummy_test: OK (out-of-disk black, new=black, full=Lambert, quarter~illum)\n");
    return 0;
}
