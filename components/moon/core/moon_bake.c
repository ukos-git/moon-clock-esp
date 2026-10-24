/* Bake one lit 8-bit grayscale frame from the equirect albedo.
 *
 * Ports moon/assets.py::render_from_assets + renderer.earthshine/tonemap 1:1
 * (the relief-free validated device model):
 *
 *   r_body = u*right + v*up + w*E        (base unit direction, body frame)
 *   d      = r_body . S                  (base-sphere dot, terminator + es)
 *   L      = clip(d, 0, 1)               (full Lambert; relief dropped by
 *                                          measurement -- see assets.py)
 *   raw    = albedo(lon_tex, lat_body) * L     (equirect, nearest-neighbor)
 *   es     = A*x + B*x^2 + C*x^3, x = max(0, -d)   (phase-independent)
 *   out    = clamp(TONE_LUT[round(raw)] + es)     (8-bit units)
 *
 * Outside the disk: 0.
 *
 * Per-pixel cost is a few FPU ops + ONE albedo read. The Xtensa FPU has no
 * atan/asin, so the texture long/lat come from two small internal-RAM
 * tables (worst-case error << 0.05 texture row / 0.001 deg) instead of the
 * libm transcendentals, and the albedo is sampled nearest-neighbor: the
 * 1024-wide texture is ~1.1 texture px per screen px on the disk, so NN is
 * sub-pixel-accurate. (The validated gate re-checks the NN/bilinear and
 * table/atan2 differences against the Python model after any change here.)
 */
#include "moon.h"
#include "moon_consts.h"

#include <math.h>

#define MOON_TBL_N 257 /* 256 intervals; i = t*256 for t in [0,1] */

/* Internal RAM: 2 x 257 floats = 2 KB. Built once at first bake. Both are
 * size-independent: the row table stores the normalized row fraction
 * [0,1] (row 0 = north) so the same table serves any albedo height. */
static float s_atan_deg[MOON_TBL_N]; /* atan(i/256) in degrees, t in [0,1] */
static float s_row_frac[MOON_TBL_N]; /* row frac [0,1] for rz = i/128 - 1 */
static int s_tbl_init = 0;

static void tables_build(void) {
    for (int i = 0; i < MOON_TBL_N; i++) {
        s_atan_deg[i] = atanf((float)i / 256.0f) * 57.29577951308232f;
        float rz = (float)i / 128.0f - 1.0f; /* [-1, 1] */
        float lat = asinf(rz) * 57.29577951308232f;
        s_row_frac[i] = (90.0f - lat) / 180.0f; /* [0,1] */
    }
    s_tbl_init = 1;
}

static float atan_interp(float t) /* t in [0,1] -> atan(t) deg */
{
    float f = t * 256.0f;
    int i = (int)f;
    if (i >= MOON_TBL_N - 1)
        i = MOON_TBL_N - 2;
    float fr = f - (float)i;
    return s_atan_deg[i] + (s_atan_deg[i + 1] - s_atan_deg[i]) * fr;
}

static float row_frac_interp(float rz) /* rz in [-1,1] -> row frac [0,1] */
{
    float f = (rz + 1.0f) * 128.0f;
    int i = (int)f;
    if (i < 0)
        i = 0;
    else if (i > MOON_TBL_N - 2)
        i = MOON_TBL_N - 2;
    float fr = f - (float)i;
    return s_row_frac[i] + (s_row_frac[i + 1] - s_row_frac[i]) * fr;
}

/* atan2(y, x) in degrees, [0, 360) -- matches math.atan2 + wrap. */
static float atan2_deg(float x, float y) {
    if (x == 0.0f && y == 0.0f)
        return 0.0f; /* poles: atan2(0,0) = 0 in both C and Python */
    float ax = x < 0.0f ? -x : x;
    float ay = y < 0.0f ? -y : y;
    float a;
    if (ay > ax) { /* keep the ratio in [0,1] */
        a = 90.0f - atan_interp(ax / ay);
        if (x < 0.0f)
            a += 180.0f;
        return y >= 0.0f ? a : 360.0f - a;
    }
    a = atan_interp(ay / ax);
    if (x < 0.0f)
        a += 180.0f;
    return y >= 0.0f ? a : 360.0f - a;
}

/* Phase-independent earthshine, 8-bit units (constants.MOON_ES_*). */
static float earthshine(float d) {
    float x = -d > 0.0f ? -d : 0.0f;
    return MOON_ES_A * x + MOON_ES_B * x * x + MOON_ES_C * x * x * x;
}

void moon_bake(const moon_geom_t *g, const uint8_t *albedo, uint8_t *frame, int size, float radius, float cx, float cy,
               int alb_w, int alb_h) {
    if (!s_tbl_init)
        tables_build();
    const float per_deg = (float)alb_w / 360.0f;
    for (int y = 0; y < size; y++) {
        float v = (cy - (float)y) / radius; /* v up: row 0 is top */
        uint8_t *row = &frame[y * size];
        for (int x = 0; x < size; x++) {
            float u = ((float)x - cx) / radius;
            float r2 = u * u + v * v;
            if (r2 > 1.0f) {
                row[x] = 0;
                continue;
            }
            float w = sqrtf(1.0f - r2);
            /* Base direction, body frame (pole = +z). */
            float rx = u * g->right[0] + v * g->up[0] + w * g->E[0];
            float ry = u * g->right[1] + v * g->up[1] + w * g->E[1];
            float rz = u * g->right[2] + v * g->up[2] + w * g->E[2];
            float d = rx * g->S[0] + ry * g->S[1] + rz * g->S[2];

            /* Texture coordinates, table-based (see file header). */
            float theta = atan2_deg(rx, ry); /* lon, deg, [0, 360) */
            float colf = (theta + MOON_TEX_LON_OFFSET) * per_deg;
            if (colf >= (float)alb_w) /* theta + 180 < 540: one wrap max */
                colf -= (float)alb_w;
            int c = (int)(colf + 0.5f);
            if (c >= alb_w)
                c = alb_w - 1;
            int r = (int)(row_frac_interp(rz) * (float)alb_h + 0.5f);
            if (r < 0)
                r = 0;
            else if (r >= alb_h)
                r = alb_h - 1;
            float A = (float)albedo[r * alb_w + c];

            float L = d > 1.0f ? 1.0f : (d < 0.0f ? 0.0f : d);
            float raw = A * L;
            int idx = (int)(raw + 0.5f);
            if (idx < 0)
                idx = 0;
            else if (idx > 255)
                idx = 255;
            float out = MOON_TONE_LUT[idx] + earthshine(d);
            row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
        }
    }
}
