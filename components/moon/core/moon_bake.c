/* Bake one lit 8-bit grayscale frame from the equirect albedo + relief DEM.
 *
 * Ports moon/assets.py::render_from_assets + relief_lns +
 * renderer.earthshine/tonemap 1:1 (the validated device model):
 *
 *   r_body = u*right + v*up + w*E        (base unit direction, body frame)
 *   d      = r_body . S                  (base-sphere dot; drives earthshine)
 *   L      = clip(lns, 0, 1), lns = (d - G.S)/sqrt(1 + |G|^2) UNCLIPPED
 *            -- the displaced-normal Lambert applied over the WHOLE lit
 *            disk (mountain shadows at the terminator: the ragged edge +
 *            the fully dark shadow side of crater ridges the NASA frames
 *            carry). The old thin-band blend of a CLIPPED relief term
 *            (|d| < MOON_TERM_BAND_RAD, lns floored at 0) left the
 *            terminator 3-7x too soft; unclipped full-disk relief at a
 *            2048x1024 DEM matches the NASA terminator contrast.
 *            dem == NULL: relief-free L = clip(d, 0, 1).
 *   raw    = albedo(lon_tex, lat_body) * L     (equirect, nearest-neighbor)
 *   es     = A*x + B*x^2 + C*x^3, x = max(0, -d)   (phase-independent)
 *   out    = clamp(TONE_LUT[round(raw)] + es)     (8-bit units)
 *
 * Outside the disk: 0.
 *
 * Relief normal (body frame). For a unit base direction r=(rx,ry,rz), the
 * two tangent unit vectors are east = (-ry, rx, 0)/rho and north =
 * (-rz*rx, -rz*ry, rho)/rho with rho = sqrt(rx^2+ry^2) = cos(lat) (built
 * from r itself -- no lon/lat transcendentals). The DEM's 4-neighbor
 * central-difference gradient, in height-per-unit-arc (dlon, dlat =
 * radians per DEM cell), gives the slope vector; since east/north are
 * tangent to the unit sphere the displaced normal is
 *     N = normalize(r - G)   =>   N . S = (d - G . S) / sqrt(1 + |G|^2)
 * with G = glon*east + glat*north and G . S = glon*east_s + glat*north_s.
 * This is the small-slope form of the exact radial-displacement normal
 * (see assets.py::relief_lns for the derivation); at the slopes present
 * (<< 0.3 rad) the truncation error is < 0.5%.
 *
 * Per-pixel cost: a few FPU ops + ONE albedo read + (when the DEM is
 * present) a fixed 4-neighbor DEM gather for the relief normal --
 * DEM-resolution-independent, so the 2048-wide DEM costs the same as the
 * 512-wide one (the animation is display-rate-limited, not bake-limited).
 * The Xtensa FPU has no atan/asin, so the texture long/lat come from two
 * small internal-RAM tables (worst-case error << 0.05 texture row /
 * 0.001 deg) instead of the libm transcendentals, and the albedo is
 * sampled nearest-neighbor: the 864-wide texture is ~1.05 texture px per
 * screen px on the disk, so NN is sub-pixel-accurate. (The validated gate
 * re-checks the NN/bilinear and table/atan2 differences against the
 * Python model after any change here.)
 */
#include "moon.h"
#include "moon_consts.h"

#include <math.h>
#include <stddef.h>

#define MOON_TBL_N 257 /* 256 intervals; i = t*256 for t in [0,1] */

/* Internal RAM: 2 x 257 floats = 2 KB. Built once at first bake. Both are
 * size-independent: the row table stores the normalized row fraction
 * [0,1] (row 0 = north) so the same table serves any albedo/DEM height. */
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

/* Relief-normal sun dot lns = (d - G.S)/sqrt(1 + |G|^2) -- UNCLIPPED --
 * from the 4-neighbor DEM gradient (body frame; see file header).
 * dem_w/dem_h are the DEM dimensions (MOON_DEM_W/H). rx,ry,rz is the unit
 * base direction, d = r . S. Goes negative on slopes facing away from the
 * Sun (a ridge's shadow side); the caller clips at 0 so the dark side gets
 * no direct light. The old max(lns, 0) floor is what kept the terminator
 * shadows from going fully dark -- and left the edge 3-7x too soft vs the
 * NASA frames. */
static float relief_lns(const int8_t *dem, int dem_w, int dem_h, float rx, float ry, float rz, float d, const float *S,
                        float theta, float row_frac, float mps) {
    /* DEM texel (same equirect convention as the albedo). */
    float colf = (theta + MOON_TEX_LON_OFFSET) * ((float)dem_w / 360.0f);
    if (colf >= (float)dem_w)
        colf -= (float)dem_w;
    int c = (int)(colf + 0.5f);
    if (c < 0)
        c = 0;
    else if (c >= dem_w)
        c = dem_w - 1;
    int r = (int)(row_frac * (float)dem_h + 0.5f);
    if (r < 0)
        r = 0;
    else if (r >= dem_h)
        r = dem_h - 1;
    int c_e = c + 1 < dem_w ? c + 1 : 0;
    int c_w = c > 0 ? c - 1 : dem_w - 1;
    int r_n = r > 0 ? r - 1 : 0;
    int r_s = r + 1 < dem_h ? r + 1 : dem_h - 1;
    float h_e = (float)dem[r * dem_w + c_e];
    float h_w = (float)dem[r * dem_w + c_w];
    float h_n = (float)dem[r_n * dem_w + c];
    float h_s = (float)dem[r_s * dem_w + c];

    /* Arc length per DEM cell (radians); height in meters via mps. */
    float dlon = (2.0f * 3.14159265f) / (float)dem_w;
    float dlat = 3.14159265f / (float)dem_h;
    float rho = sqrtf(rx * rx + ry * ry); /* = cos(lat) */
    if (rho < 1e-4f)
        rho = 1e-4f;
    float rho_c = rho > 0.05f ? rho : 0.05f; /* longitude arc floor (polar cap) */
    float glon = (h_e - h_w) * mps / (2.0f * MOON_RADIUS_M * rho_c * dlon);
    float glat = (h_n - h_s) * mps / (2.0f * MOON_RADIUS_M * dlat);

    /* Tangent basis dotted with S (east/north from r itself). */
    float inv = 1.0f / rho;
    float east_s = (-ry * S[0] + rx * S[1]) * inv;
    float north_s = (-rz * rx * S[0] - rz * ry * S[1] + rho * S[2]) * inv;
    float gs = glon * east_s + glat * north_s;
    float lns = (d - gs) / sqrtf(1.0f + glon * glon + glat * glat);
    return lns > 0.0f ? lns : 0.0f;
}

/* Bake one 8-bit grayscale row range [y0, y1) (see file header for the
 * per-pixel model). moon_bake() = the full range; the device boot
 * animation bakes row-by-row so it can feed the progressive strip display
 * (components/ui/moon_ui.c) while the rest of the frame is still being
 * computed. */
void moon_bake_rows(const moon_geom_t *g, const uint8_t *albedo, const int8_t *dem, uint8_t *frame, int size,
                    float radius, float cx, float cy, int alb_w, int alb_h, int y0, int y1) {
    if (!s_tbl_init)
        tables_build();
    const float per_deg = (float)alb_w / 360.0f;
    const float mps = (MOON_DEM_MAX_M - MOON_DEM_MIN_M) / (float)MOON_DEM_STEPS; /* m per int8 step */
    for (int y = y0; y < y1; y++) {
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
            float row_frac = row_frac_interp(rz);
            int r = (int)(row_frac * (float)alb_h + 0.5f);
            if (r < 0)
                r = 0;
            else if (r >= alb_h)
                r = alb_h - 1;
            float A = (float)albedo[r * alb_w + c];

            /* Lighting: full-disk displaced-normal Lambert (see header).
             * lns is unclipped; clip at 0 so the dark side (d < 0 and
             * shadowed slopes) gets no direct light -- earthshine below
             * carries the dark zone. */
            float L;
            if (dem == NULL) {
                L = d > 1.0f ? 1.0f : (d < 0.0f ? 0.0f : d);
            } else {
                L = relief_lns(dem, MOON_DEM_W, MOON_DEM_H, rx, ry, rz, d, g->S, theta, row_frac, mps);
                if (L < 0.0f)
                    L = 0.0f;
                else if (L > 1.0f)
                    L = 1.0f;
            }
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

void moon_bake(const moon_geom_t *g, const uint8_t *albedo, const int8_t *dem, uint8_t *frame, int size, float radius,
               float cx, float cy, int alb_w, int alb_h) {
    moon_bake_rows(g, albedo, dem, frame, size, radius, cx, cy, alb_w, alb_h, 0, size);
}
