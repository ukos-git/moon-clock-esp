/* Bake one lit 8-bit grayscale frame from the equirect albedo + relief.
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
 *            relief == NULL: relief-free L = clip(d, 0, 1).
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
 * The slope (glon, glat) is a pure function of the DEM cell -- the
 * 4-neighbor height differences -- so it is PRECOMPUTED ONCE per cell by
 * moon_relief_build() into two int8 planes (quantized to MOON_GREL_INV,
 * clamped at 127 = 0.381 rad) instead of being re-gathered per pixel.
 * The bake then reads two cache bytes per pixel (1 + 2 PSRAM loads total)
 * instead of a 4-neighbor DEM scatter, and the only per-pixel relief
 * math left is dequantize + the tangent-basis dots. The quantization
 * (+-MOON_GREL_INV/2 slope -> < 0.3% of L) and the per-cell rho_c (vs
 * per-pixel; differs only in the polar-cap rows, where lns ~ (d-G.S)/|G|
 * -> 0 anyway) are re-checked by the image-diff gate after any change.
 *
 * Per-pixel cost: a few FPU ops + PSRAM byte loads -- a LIT pixel takes
 * 3 (1 albedo + 2 relief gradient, independent, so the 4-pixel unroll
 * below overlaps their latency with the FPU work); a DARK pixel takes 2,
 * because L <= 0 is provable from d + the gradient cache alone
 * (d - G.S <= 0, the sqrt denominator is always positive) and its output
 * is TONE_LUT[0] + earthshine(d), albedo-independent (A*0 rounds to LUT
 * entry 0) -- so the albedo load is dead work there and is skipped
 * (bit-identical; see bake_px). Measured ~1.3 s/frame at half phase on
 * the S3 (down from ~3.3 s/frame with the old per-pixel 4-neighbor
 * gather); the sweep (components/ui) paces off this bake time, not the
 * ~150 ms display.
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

/* always_inline on the per-pixel helpers: see the note on bake_px (the
 * inner loop's PSRAM loads can only be scheduled across pixels when the
 * whole pixel body is visible to the compiler at once). */
static __attribute__((always_inline)) inline float atan_interp(float t) /* t in [0,1] -> atan(t) deg */
{
    float f = t * 256.0f;
    int i = (int)f;
    if (i >= MOON_TBL_N - 1)
        i = MOON_TBL_N - 2;
    float fr = f - (float)i;
    return s_atan_deg[i] + (s_atan_deg[i + 1] - s_atan_deg[i]) * fr;
}

static __attribute__((always_inline)) inline float row_frac_interp(float rz) /* rz in [-1,1] -> row frac [0,1] */
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
static __attribute__((always_inline)) inline float atan2_deg(float x, float y) {
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
static __attribute__((always_inline)) inline float earthshine(float d) {
    float x = -d > 0.0f ? -d : 0.0f;
    return MOON_ES_A * x + MOON_ES_B * x * x + MOON_ES_C * x * x * x;
}

/* Relief-gradient quantization: slopes stored as int8 in units of
 * MOON_GREL_INV rad (height per unit arc). 127 * 0.003 = 0.381 rad covers
 * the DEM's steepest 2-cell slopes (<< 0.3 rad everywhere on the smooth
 * LROC-LDEM downsample); the +-127 clamp only bites on synthetic
 * checkerboard input. The per-pixel error is at most MOON_GREL_INV/2 in
 * each slope -> < 0.3% of L (see file header). */
#define MOON_GREL_INV 0.003f

/* Quantize a slope to int8 units of MOON_GREL_INV, clamped to +-127. */
static __attribute__((always_inline)) inline int8_t slope_q(float slope) {
    float q = roundf(slope / MOON_GREL_INV);
    if (q > 127.0f)
        q = 127.0f;
    else if (q < -127.0f)
        q = -127.0f;
    return (int8_t)q;
}

/* One interior cell of a relief row (c in [1, dem_w-2], so c-1/c+1 are
 * in bounds and the longitude-wrap branches stay out of the hot loop).
 * always_inline so the 4-wide unroll's PSRAM loads can be issued together
 * and overlap (the same lesson as moon_bake_rows' 4-pixel unroll: a
 * sequential per-cell loop leaves ~12 M PSRAM byte accesses stalling on
 * their own latency -- ~4 s for the whole build on the S3). */
static __attribute__((always_inline)) inline void relief_cell(const int8_t *row_n, const int8_t *row_m,
                                                              const int8_t *row_s, int c, float mps, float inv_glon,
                                                              float inv_glat, int8_t *gln, int8_t *glt) {
    float dhlon = ((float)row_m[c + 1] - (float)row_m[c - 1]) * mps;
    float dhlat = ((float)row_n[c] - (float)row_s[c]) * mps;
    gln[c] = slope_q(dhlon * inv_glon);
    glt[c] = slope_q(dhlat * inv_glat);
}

/* One output row of the per-cell relief-gradient cache (see file
 * header) from the three DEM window rows (row_n/row_m/row_s, each
 * dem_w int8): the central-difference slopes quantized to
 * MOON_GREL_INV, written to gln/glt (dem_w cells each).
 *
 * Bit-level notes: the numerators (h_e - h_w)*mps / (h_n - h_s)*mps are
 * computed with the exact same float ops as the old per-pixel gather (the
 * int8 difference is exact in float, one rounding at the mps multiply),
 * and mps/dlon/dlat/R are the same constants; the only deliberate deltas
 * are the quantization, the per-cell rho_c (the cell's own latitude, same
 * 0.05 floor) in place of the old per-pixel rho -- which differs only
 * inside the polar cap, where lns -> 0 regardless (see header) -- and the
 * denominator reciprocal (slope * (1/den) instead of slope / den; a float
 * reciprocal rounds, so the product can differ by <= 1 ulp, which flips
 * at most rounding-boundary cells by one quantization step = 0.003 slope
 * = 0.3% of L; the image-diff gate re-checks it). */
void moon_relief_row(const int8_t *row_n, const int8_t *row_m, const int8_t *row_s, int dem_w, int dem_h, int r,
                     int8_t *gln, int8_t *glt) {
    const int W = dem_w;
    const float mps = (MOON_DEM_MAX_M - MOON_DEM_MIN_M) / (float)MOON_DEM_STEPS; /* m per int8 step */
    const float dlon = (2.0f * 3.14159265f) / (float)dem_w;
    const float dlat = 3.14159265f / (float)dem_h;
    const float two_r = 2.0f * (float)MOON_RADIUS_M;
    /* Cell center latitude (row 0 = north = +90 deg); rho = cos(lat)
     * = sin((r+0.5)*pi/dem_h), with the bake's 0.05 longitude-arc
     * floor (polar cap). Per-row reciprocals (not per-cell divisions):
     * the build is 2 M cells, and the Xtensa FPU's division is the
     * slowest op in the loop. */
    float rho = sinf(((float)r + 0.5f) * (3.14159265f / (float)dem_h));
    if (rho < 0.05f)
        rho = 0.05f;
    const float inv_glon = 1.0f / (two_r * dlon * rho);
    const float inv_glat = 1.0f / (two_r * dlat);

    /* Edge cells carry the longitude wrap (c=0's west neighbor is
     * W-1, c=W-1's east neighbor is 0) -- handle them outside the loop. */
    {
        float dhlon = ((float)row_m[0] - (float)row_m[W - 1]) * mps;
        float dhlat = ((float)row_n[0] - (float)row_s[0]) * mps;
        gln[0] = slope_q(dhlon * inv_glon);
        glt[0] = slope_q(dhlat * inv_glat);
        dhlon = ((float)row_m[0] - (float)row_m[W - 2]) * mps;
        dhlat = ((float)row_n[W - 1] - (float)row_s[W - 1]) * mps;
        gln[W - 1] = slope_q(dhlon * inv_glon);
        glt[W - 1] = slope_q(dhlat * inv_glat);
    }
    /* Interior: 4 cells per iteration, so up to 24 PSRAM loads (4 rows x
     * ... 3 window rows x ~2-3 loads/cell) are in flight together and the
     * octal-PSRAM latency overlaps with the FPU work. */
    for (int c = 1; c + 3 <= W - 2; c += 4) {
        relief_cell(row_n, row_m, row_s, c, mps, inv_glon, inv_glat, gln, glt);
        relief_cell(row_n, row_m, row_s, c + 1, mps, inv_glon, inv_glat, gln, glt);
        relief_cell(row_n, row_m, row_s, c + 2, mps, inv_glon, inv_glat, gln, glt);
        relief_cell(row_n, row_m, row_s, c + 3, mps, inv_glon, inv_glat, gln, glt);
    }
    for (int c = 1 + 4 * ((W - 4) / 4); c <= W - 2; c++) { /* the 0-3 leftover cells */
        relief_cell(row_n, row_m, row_s, c, mps, inv_glon, inv_glat, gln, glt);
    }
}

/* Build the whole per-cell relief-gradient cache from the int8 DEM (see
 * file header): glon_q/glat_q are MOON_DEM_W x MOON_DEM_H int8 planes.
 * Pure C (no allocation): the caller (asset backend) owns the planes;
 * the whole-array build is just a loop over moon_relief_row. */
void moon_relief_build(const int8_t *dem, int dem_w, int dem_h, int8_t *glon_q, int8_t *glat_q) {
    for (int r = 0; r < dem_h; r++) {
        const int8_t *row_m = dem + (size_t)r * dem_w;
        const int8_t *row_n = dem + (size_t)(r > 0 ? r - 1 : 0) * dem_w;
        const int8_t *row_s = dem + (size_t)(r + 1 < dem_h ? r + 1 : dem_h - 1) * dem_w;
        moon_relief_row(row_n, row_m, row_s, dem_w, dem_h, r, glon_q + (size_t)r * dem_w, glat_q + (size_t)r * dem_w);
    }
}

/* One pixel of the bake (see file header for the per-pixel model). Row
 * scalars ride in the context so the inner loop can unroll without an
 * argument-list blowup.
 *
 * always_inline is load-bearing: with bake_px as a separate call target
 * the compiler cannot see the 4 pixels' PSRAM loads
 * together, so every load pays the full ~500-cycle octal-PSRAM latency
 * (the cache indices are computed immediately before the loads) --
 * measured ~2.1 s/frame with a separate gather, i.e. the unroll buys
 * nothing. Inlined, the 4 pixels' index math can run ahead and their
 * loads be issued together, overlapping the latency with the FPU work.
 * (Before the gradient cache the per-pixel DEM gather was on top of
 * that: 6 dependent PSRAM loads/px -> ~3.3 s/frame; the cache cut it to
 * 3 independent loads/px -> ~1.3 s/frame on the S3.) */
typedef struct {
    const moon_geom_t *g;
    const uint8_t *alb;
    const moon_grel_t *grel; /* relief-gradient cache or NULL (relief-free) */
    uint8_t *row;
    int alb_w, alb_h;
    float radius, cx, v;
    float per_deg; /* albedo columns per degree */
} bake_ctx_t;

static __attribute__((always_inline)) inline void bake_px(bake_ctx_t *c, int x) {
    const moon_geom_t *g = c->g;
    float u = ((float)x - c->cx) / c->radius;
    float r2 = u * u + c->v * c->v;
    if (r2 > 1.0f) {
        c->row[x] = 0;
        return;
    }
    float w = sqrtf(1.0f - r2);
    /* Base direction, body frame (pole = +z). */
    float rx = u * g->right[0] + c->v * g->up[0] + w * g->E[0];
    float ry = u * g->right[1] + c->v * g->up[1] + w * g->E[1];
    float rz = u * g->right[2] + c->v * g->up[2] + w * g->E[2];
    float d = rx * g->S[0] + ry * g->S[1] + rz * g->S[2];

    /* Texture coordinates, table-based (see file header): pure CPU table
     * lookups (no PSRAM), shared by the DEM texel and the albedo sample.
     * Computed before lighting so the relief path can find its gradient
     * cache cell; dark pixels pay the lookups but skip the albedo load. */
    float theta = atan2_deg(rx, ry); /* lon, deg, [0, 360) */
    float row_frac = row_frac_interp(rz);

    /* Lighting first: full-disk displaced-normal Lambert (see header).
     * lns is unclipped; clip at 0 so the dark side (d < 0 and shadowed
     * slopes) gets no direct light -- earthshine below carries the dark
     * zone. lns <= 0  <=>  d - G.S <= 0 (the sqrt denominator is always
     * positive), so the dark side is provable from d + the two gradient
     * bytes alone -- and its output is TONE_LUT[0] + earthshine(d),
     * albedo-independent (A * 0 rounds to LUT entry 0). The albedo
     * PSRAM load (the scattered byte that is the perf floor) is dead
     * work for a dark pixel and is skipped below: bit-identical. */
    float L;
    if (c->grel == NULL) {
        L = d > 1.0f ? 1.0f : (d < 0.0f ? 0.0f : d);
    } else {
        /* DEM texel for the gradient cache: the old per-pixel gather's
         * same equirect mapping, from the theta (deg) / row_frac
         * (normalized) above. */
        float dcf = (theta + MOON_TEX_LON_OFFSET) * ((float)MOON_DEM_W / 360.0f);
        if (dcf >= (float)MOON_DEM_W)
            dcf -= (float)MOON_DEM_W;
        int dc = (int)(dcf + 0.5f);
        if (dc >= MOON_DEM_W)
            dc = MOON_DEM_W - 1;
        int dr = (int)(row_frac * (float)MOON_DEM_H + 0.5f);
        if (dr >= MOON_DEM_H)
            dr = MOON_DEM_H - 1;
        int i = dr * MOON_DEM_W + dc;
        float glon = (float)c->grel->glon[i] * MOON_GREL_INV;
        float glat = (float)c->grel->glat[i] * MOON_GREL_INV;
        /* Tangent basis dotted with S (east/north from r itself). */
        float rho = sqrtf(rx * rx + ry * ry); /* = cos(lat) */
        if (rho < 1e-4f)
            rho = 1e-4f;
        float inv = 1.0f / rho;
        float east_s = (-ry * g->S[0] + rx * g->S[1]) * inv;
        float north_s = (-rz * rx * g->S[0] - rz * ry * g->S[1] + rho * g->S[2]) * inv;
        float gs = glon * east_s + glat * north_s;
        L = (d - gs) / sqrtf(1.0f + glon * glon + glat * glat);
        if (L < 0.0f)
            L = 0.0f;
        else if (L > 1.0f)
            L = 1.0f;
    }
    if (L <= 0.0f) { /* dark side: earthshine only, no albedo load */
        float out = MOON_TONE_LUT[0] + earthshine(d);
        c->row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
        return;
    }

    /* Albedo, nearest-neighbor -- lit pixels only (see the note above). */
    float colf = (theta + MOON_TEX_LON_OFFSET) * c->per_deg;
    if (colf >= (float)c->alb_w) /* theta + 180 < 540: one wrap max */
        colf -= (float)c->alb_w;
    int col = (int)(colf + 0.5f);
    if (col >= c->alb_w)
        col = c->alb_w - 1;
    int r = (int)(row_frac * (float)c->alb_h + 0.5f);
    if (r < 0)
        r = 0;
    else if (r >= c->alb_h)
        r = c->alb_h - 1;
    float A = (float)c->alb[r * c->alb_w + col];

    float raw = A * L;
    int idx = (int)(raw + 0.5f);
    if (idx < 0)
        idx = 0;
    else if (idx > 255)
        idx = 255;
    float out = MOON_TONE_LUT[idx] + earthshine(d);
    c->row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
}

/* Bake one 8-bit grayscale row range [y0, y1) (see file header for the
 * per-pixel model). moon_bake() = the full range; the device boot
 * animation bakes row-by-row so it can feed the progressive strip display
 * (components/ui/moon_ui.c) while the rest of the frame is still being
 * computed.
 *
 * The inner loop processes 4 pixels per iteration: each LIT pixel issues
 * 3 independent PSRAM int8 loads (1 albedo + 2 relief-gradient cache), a
 * DARK pixel only 2 (the albedo load is dead work there -- see bake_px),
 * and a few FPU ops (~2 sqrt + ~1 div + ~30 FMA). Unrolling keeps up to
 * 12 PSRAM loads in flight per 4 pixels so their ~500-cycle octal-PSRAM
 * latency overlaps with the FPU work -- the same trick
 * moon_rotate_range uses in moon_render.c (sequential per-pixel: every
 * load waits on its own latency; unrolled: measured ~1.3 s/frame vs the
 * old per-pixel 4-neighbor gather's ~3.3 s). The remaining per-frame
 * cost is the FPU + PSRAM bandwidth floor, not load latency. */
void moon_bake_rows(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *frame, int size,
                    float radius, float cx, float cy, int alb_w, int alb_h, int y0, int y1) {
    if (!s_tbl_init)
        tables_build();
    const float per_deg = (float)alb_w / 360.0f;
    for (int y = y0; y < y1; y++) {
        bake_ctx_t c;
        c.g = g;
        c.alb = albedo;
        c.grel = grel;
        c.row = &frame[y * size];
        c.alb_w = alb_w;
        c.alb_h = alb_h;
        c.radius = radius;
        c.cx = cx;
        c.v = (cy - (float)y) / radius; /* v up: row 0 is top */
        c.per_deg = per_deg;
        for (int x0 = 0; x0 < size; x0 += 4) {
            int n = size - x0;
            bake_px(&c, x0);
            if (n > 1)
                bake_px(&c, x0 + 1);
            if (n > 2)
                bake_px(&c, x0 + 2);
            if (n > 3)
                bake_px(&c, x0 + 3);
        }
    }
}

void moon_bake(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *frame, int size,
               float radius, float cx, float cy, int alb_w, int alb_h) {
    moon_bake_rows(g, albedo, grel, frame, size, radius, cx, cy, alb_w, alb_h, 0, size);
}
