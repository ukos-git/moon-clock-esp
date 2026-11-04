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
 * moon_relief_build() into ONE uint16 array (quantized to MOON_GREL_INV,
 * clamped at 127 = 0.381 rad, glon low byte / glat high byte) instead of
 * being re-gathered per pixel. The bake then reads ONE 16-bit cache load
 * per pixel (1 + 1 PSRAM loads total) instead of a 4-neighbor DEM
 * scatter, and the only per-pixel relief math left is dequantize + the
 * tangent-basis dots. The quantization
 * (+-MOON_GREL_INV/2 slope -> < 0.3% of L) and the per-cell rho_c (vs
 * per-pixel; differs only in the polar-cap rows, where lns ~ (d-G.S)/|G|
 * -> 0 anyway) are re-checked by the image-diff gate after any change.
 *
 * Per-pixel cost: a few FPU ops + PSRAM byte loads -- every IN-DISK pixel
 * takes 3 (1 albedo + 2 relief gradient, independent, so the 4-pixel
 * unroll below overlaps their latency with the FPU work). The backlit
 * night side (dark_zero = 1) needs the albedo, so the old dark-pixel
 * albedo-skip is gone (the dark_zero = 0 earthshine branch ignores the
 * loaded A -- its output is still TONE_LUT[0] + earthshine(d), i.e.
 * bit-identical to the old skip, at the cost of one dead load there).
 * Measured ~1.3 s/frame at half phase on the S3 before the backlit
 * change (down from ~3.3 s/frame with the old per-pixel 4-neighbor
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

/* Backlit night side (the device's dark side, dark_zero = 1): the
 * unilluminated side is rendered as the lunar SURFACE (albedo + relief)
 * lit from the ANTI-solar direction (-S) at a faint level, instead of the
 * flat MOON_NIGHT_FLOOR (the old black overlay) or the phase-dependent
 * earthshine ("earth ray", dark_zero = 0). The backlit Lambert is
 * L_anti = clip(-lns, 0, 1) where lns is the solar displaced-normal
 * Lambert: a surface point lit from -S at exactly the strength the solar
 * Lambert rejects from +S, so the dark side shows the same relief (the
 * crater rims facing the viewer catch the light) at a dim level. It is
 * body-frame (S is fixed for the epoch, the gradient is body-frame), so
 * the shade map stays static and only the projection rotates (the pole
 * spin) -- the user's "no earth ray, the image is static and only the
 * sphere it is projected onto rotates". MOON_NIGHT_BACKLIT_K sets the
 * level: the dark side's peak gray = 255 * K (51 at 0.20). 0.20 is an
 * ARTISTIC choice (the display is meant to read as a real moon photo, not a
 * faint afterimage): the unilluminated side is clearly visible with real
 * mare/highland structure, ~2x the old 0.10 floor (peak 25). The gate
 * validates the dark_zero = 0 path, so this display-only constant is
 * invisible to it. */
#define MOON_NIGHT_BACKLIT_K 0.20f
/* Soft terminator width (lns units, EACH side of the lns = 0 terminator).
 * The hard 0 -> LUT-floor step at lns = 0 reads as a cutout edge; a narrow
 * smoothstep band blends the backlit night side into the lit side the way a
 * real photo's terminator falls off (the user: "NASA adds a gaussian blur at
 * the boundary"). Display only -- the blend is applied in the dark_zero = 1
 * path (the device bake), and the gate's dark_zero = 0 path is untouched.
 * 0.03 lns ~= 6 px each side at the 400 px disk (1 lns unit ~= 200 px). */
#define MOON_TERM_SOFT 0.03f
/* The shade-map dark-side bound erosion (replaces the re-project's old
 * per-pixel dk_sup kill): the re-project samples the map NEAREST-NEIGHBOR,
 * so a dark pixel (d < dk_sup) near the d = dk_sup boundary rounds to the
 * cell whose CENTER is up to half a theta-cell + half a lat-cell away in
 * d. |grad(r . S)| = 1 on the unit sphere, so that is (pi/map_w + pi/map_h)
 * ~= 0.0073 for the 864x432 map -- a cell that is LIT at its center (the
 * crescent's edge, up to gray 137+) would re-light the rim sliver the dk_sup
 * bound exists to kill. The map build writes the backlit night side for
 * d < dk_sup + MOON_DK_MARGIN instead of d < dk_sup, so every cell a dark
 * pixel can round to is backlit (pure black when the cell is sun-lit,
 * lns > 0). The margin is the worst-case offset plus a hair of safety.
 * dk_sup = 0.0f callers are unaffected in practice: the only dark_zero = 1
 * map caller is the dusk (dk_sup > 0). */
#define MOON_DK_MARGIN 0.010f

/* The backlit night COLOR (see MOON_NIGHT_BACKLIT_K): the lunar surface A
 * lit from -S at a faint level. lns = the solar displaced-normal Lambert
 * (unclipped); L_anti = clip(-lns, 0, 1). A direct linear gray (NOT the
 * tone LUT: the LUT's shadow lift -- bin 1 -> 27 -- is tuned for the lit
 * side and would over-brighten the faint dark side, and its non-linearity
 * breaks the "peak = 255 * K" contract). lns > 0 (a sun-lit pixel, e.g. the
 * dusk's rim sliver) clips to 0 = pure black, so the rim sliver is killed
 * exactly as the old flat-floor path. always_inline: the bake's hot path
 * (the dark side of a backlit bake is a large fraction of the disk)
 * inlines it next to the shared lns. */
static __attribute__((always_inline)) inline uint8_t moon_backlit_night(float A, float lns) {
    float l = -lns;
    if (l < 0.0f)
        l = 0.0f;
    else if (l > 1.0f)
        l = 1.0f;
    float out = A * l * MOON_NIGHT_BACKLIT_K;
    return (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
}

/* One interior cell of a relief row (c in [1, dem_w-2], so c-1/c+1 are
 * in bounds and the longitude-wrap branches stay out of the hot loop).
 * always_inline so the 4-wide unroll's PSRAM loads can be issued together
 * and overlap (the same lesson as moon_bake_rows' 4-pixel unroll: a
 * sequential per-cell loop leaves ~12 M PSRAM byte accesses stalling on
 * their own latency -- ~4 s for the whole build on the S3). */
static __attribute__((always_inline)) inline void relief_cell(const int8_t *row_n, const int8_t *row_m,
                                                              const int8_t *row_s, int c, float mps, float inv_glon,
                                                              float inv_glat, uint16_t *gpack) {
    float dhlon = ((float)row_m[c + 1] - (float)row_m[c - 1]) * mps;
    float dhlat = ((float)row_n[c] - (float)row_s[c]) * mps;
    /* Packed cell: glon (east) low byte, glat (north) high byte -- the
     * bake reads both slopes with ONE 16-bit PSRAM load per pixel
     * instead of two 1-byte loads (the PSRAM load count is the perf
     * floor). Same quantized values as the old two-plane layout. */
    int8_t qn = slope_q(dhlon * inv_glon);
    int8_t qt = slope_q(dhlat * inv_glat);
    gpack[c] = (uint16_t)((uint16_t)(uint8_t)qn | ((uint16_t)(uint8_t)qt << 8));
}

/* One output row of the per-cell relief-gradient cache (see file
 * header) from the three DEM window rows (row_n/row_m/row_s, each
 * dem_w int8): the central-difference slopes quantized to
 * MOON_GREL_INV, written packed (glon low byte / glat high byte) to
 * gpack (dem_w uint16 cells).
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
                     uint16_t *gpack) {
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
        int8_t qn = slope_q(dhlon * inv_glon), qt = slope_q(dhlat * inv_glat);
        gpack[0] = (uint16_t)((uint16_t)(uint8_t)qn | ((uint16_t)(uint8_t)qt << 8));
        dhlon = ((float)row_m[0] - (float)row_m[W - 2]) * mps;
        dhlat = ((float)row_n[W - 1] - (float)row_s[W - 1]) * mps;
        qn = slope_q(dhlon * inv_glon);
        qt = slope_q(dhlat * inv_glat);
        gpack[W - 1] = (uint16_t)((uint16_t)(uint8_t)qn | ((uint16_t)(uint8_t)qt << 8));
    }
    /* Interior: 4 cells per iteration, so up to 24 PSRAM loads (4 rows x
     * ... 3 window rows x ~2-3 loads/cell) are in flight together and the
     * octal-PSRAM latency overlaps with the FPU work. */
    for (int c = 1; c + 3 <= W - 2; c += 4) {
        relief_cell(row_n, row_m, row_s, c, mps, inv_glon, inv_glat, gpack);
        relief_cell(row_n, row_m, row_s, c + 1, mps, inv_glon, inv_glat, gpack);
        relief_cell(row_n, row_m, row_s, c + 2, mps, inv_glon, inv_glat, gpack);
        relief_cell(row_n, row_m, row_s, c + 3, mps, inv_glon, inv_glat, gpack);
    }
    for (int c = 1 + 4 * ((W - 4) / 4); c <= W - 2; c++) { /* the 0-3 leftover cells */
        relief_cell(row_n, row_m, row_s, c, mps, inv_glon, inv_glat, gpack);
    }
}

/* Build the whole per-cell relief-gradient cache from the int8 DEM (see
 * file header): pack is MOON_DEM_W x MOON_DEM_H uint16 cells (glon low
 * byte / glat high byte). Pure C (no allocation): the caller (asset
 * backend) owns the array; the whole-array build is just a loop over
 * moon_relief_row. */
void moon_relief_build(const int8_t *dem, int dem_w, int dem_h, uint16_t *pack) {
    for (int r = 0; r < dem_h; r++) {
        const int8_t *row_m = dem + (size_t)r * dem_w;
        const int8_t *row_n = dem + (size_t)(r > 0 ? r - 1 : 0) * dem_w;
        const int8_t *row_s = dem + (size_t)(r + 1 < dem_h ? r + 1 : dem_h - 1) * dem_w;
        moon_relief_row(row_n, row_m, row_s, dem_w, dem_h, r, pack + (size_t)r * dem_w);
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
    int dark_zero; /* dark side written the backlit computed surface (moon_backlit_night) instead of earthshine */
    float dk_sup;  /* dark_zero only: the night side for d < dk_sup (0.0f = the plain d < 0 terminator; the dusk's
                      rim-sliver kill, see moon_shade_map_rows) */
    const moon_event_t *bev; /* pre-calculated sky event (moon_event.h); NULL = no event, byte-identical
                                (the blood moon's per-pixel umbra occlusion, see below) */
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

    /* dk_sup (dusk only, 0.0f everywhere else) is NO LONGER an early return:
     * the backlit night side (dark_zero = 1) renders the surface (albedo +
     * relief lit from -S), not the flat floor, so it needs the albedo + the
     * relief gradient -- the d < dk_sup rim-sliver kill is folded into the
     * night branch below (the is_night test) instead of returning before the
     * texture loads. The geometrically dark condition is d < 0 -- but the
     * dusk's endpoint view still carries a geometrically LIT rim sliver (d up
     * to dk_sup = sqrt(1 - E.S(phi*)^2), the pole spin never changes the view
     * latitude): left lit it lingers on the otherwise-dark sleep screen as a
     * bright arc around the disk edge (the "white ray"). The dusk writes the
     * backlit night side for d < dk_sup instead -- and since that sliver is
     * lit by the Sun (lns > 0), its backlit L_anti = clip(-lns, 0, 1) is 0,
     * i.e. pure black, so the sliver is killed exactly as before. Every other
     * bake passes 0.0f, i.e. exactly the old d < 0. */

    /* Texture coordinates, table-based (see file header): pure CPU table
     * lookups (no PSRAM), shared by the DEM texel and the albedo sample.
     * Computed before lighting so the relief path can find its gradient
     * cache cell; dark pixels pay the lookups but skip the albedo load. */
    float theta = atan2_deg(rx, ry); /* lon, deg, [0, 360) */
    float row_frac = row_frac_interp(rz);

    /* Lighting first: full-disk displaced-normal Lambert (see header).
     * lns is the UNCLIPPED solar displaced-normal Lambert; L = clip(lns,0,1)
     * is the lit-side Lambert. The unclipped lns is kept (not just the
     * clipped L) because dark_zero's BACKLIT night side needs it: L_anti =
     * clip(-lns, 0, 1) is the anti-solar Lambert of the same surface. The
     * night test differs by caller: dark_zero = 0 (the validated model --
     * the host gate / pole_test) is lns <= 0 exactly as before (a d < 0
     * base direction whose displaced normal still catches the Sun, lns > 0,
     * stays LIT there), while dark_zero = 1 (the device) extends it to
     * d < dk_sup so the dusk's geometrically-lit rim sliver (0 < d < dk_sup,
     * lns > 0) is written the backlit side too -- where it clips to pure
     * black (the "white ray" kill, see the dk_sup note above). */
    float lns;
    if (c->grel == NULL) {
        lns = d;
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
        /* Packed cell: glon (east) low byte, glat (north) high byte --
         * ONE 2-byte PSRAM load for both slopes (was two 1-byte loads). */
        uint16_t gp = c->grel->pack[i];
        float glon = (float)(int8_t)(gp & 0xFF) * MOON_GREL_INV;
        float glat = (float)(int8_t)(gp >> 8) * MOON_GREL_INV;
        /* Tangent basis dotted with S (east/north from r itself). */
        float rho = sqrtf(rx * rx + ry * ry); /* = cos(lat) */
        if (rho < 1e-4f)
            rho = 1e-4f;
        float inv = 1.0f / rho;
        float east_s = (-ry * g->S[0] + rx * g->S[1]) * inv;
        float north_s = (-rz * rx * g->S[0] - rz * ry * g->S[1] + rho * g->S[2]) * inv;
        float gs = glon * east_s + glat * north_s;
        lns = (d - gs) / sqrtf(1.0f + glon * glon + glat * glat);
    }
    float L = lns > 1.0f ? 1.0f : (lns < 0.0f ? 0.0f : lns);

    /* Blood moon (bev->blood, moon_event.h): the Earth's umbra/penumbra
     * darkens the LIT side per pixel BEFORE the tone LUT -- L *=
     * (1 - 0.85 * occ) with occ the smooth 0..1 occlusion of this
     * pixel's surface direction (r, body frame). The eclipse "bite"
     * lands in the right place on the disk; a total eclipse still
     * leaves a faint copper disk (the LUT's tinted floor). The backlit
     * night side (lns, unmodified) is untouched: at full moon the
     * visible disk is the lit side, and the night side's artistic
     * level is calibrated separately. NULL bev = the pre-event bake
     * byte-identically (the gate + every ordinary caller). */
    if (c->bev != NULL && c->bev->blood) {
        const float r3[3] = {rx, ry, rz};
        const float occ = moon_event_occ(c->bev, r3);
        if (occ > 0.0f)
            L *= 1.0f - MOON_EV_BLOOD_K * occ;
    }

    /* Albedo, nearest-neighbor -- EVERY in-disk pixel now (the backlit dark
     * side needs it as much as the lit side; the dark_zero = 0 earthshine
     * branch ignores it, so that path stays bit-identical to the old
     * albedo-skipping dark pixel). */
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

    /* Soft terminator (display only: dark_zero = 1, the normal display bake
     * with dk_sup = 0). Blend the lit value and the backlit night value
     * across a narrow smoothstep band at lns = 0 so the terminator is a
     * soft ramp (see MOON_TERM_SOFT) instead of the hard 0 -> LUT-floor
     * step. Far lit side (lns >= band) and far dark side (lns <= -band)
     * reduce to the unmodified values; the dusk (dk_sup > 0) and the gate
     * (dark_zero = 0) both fall through to the original path below. */
    if (c->dark_zero && c->dk_sup == 0.0f) {
        int idx = (int)(A * L + 0.5f);
        if (idx < 0)
            idx = 0;
        else if (idx > 255)
            idx = 255;
        float litv = MOON_TONE_LUT[idx] + earthshine(d);
        float darkv = (float)moon_backlit_night(A, lns);
        float t = lns <= -MOON_TERM_SOFT  ? 0.0f
                  : lns >= MOON_TERM_SOFT ? 1.0f
                                          : (lns + MOON_TERM_SOFT) / (2.0f * MOON_TERM_SOFT);
        t = t * t * (3.0f - 2.0f * t); /* smoothstep */
        float out = darkv + (litv - darkv) * t;
        c->row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
        return;
    }

    int is_night = c->dark_zero ? (d < c->dk_sup) || (lns <= 0.0f) : (lns <= 0.0f);
    if (is_night) {
        if (c->dark_zero) {
            /* The computed (backlit) night side: the surface lit from -S at
             * a faint level (see moon_backlit_night), not the flat floor. */
            c->row[x] = moon_backlit_night(A, lns);
            return;
        }
        /* dark_zero = 0: the earthshine dark side (the validated model). */
        float out = MOON_TONE_LUT[0] + earthshine(d);
        c->row[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
        return;
    }

    /* Lit side (L > 0 and, for dark_zero, d >= dk_sup). */
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
 * The inner loop processes 4 pixels per iteration: every IN-DISK pixel
 * issues 2 independent PSRAM loads (1 albedo byte + 1 packed 2-byte
 * relief-gradient cell) and a few FPU ops (~2 sqrt + ~1 div + ~30 FMA) --
 * the backlit dark side needs the albedo too (see bake_px). Unrolling
 * keeps up to 8 loads in flight per 4 pixels so their ~500-cycle
 * octal-PSRAM latency overlaps with the FPU work -- the same trick
 * moon_rotate_range uses in moon_render.c (sequential per-pixel: every
 * load waits on its own latency; unrolled: measured ~1.3 s/frame before
 * the backlit change vs the old per-pixel 4-neighbor gather's ~3.3 s).
 * The remaining per-frame cost is the FPU + PSRAM bandwidth floor, not
 * load latency. */
void moon_bake_rows(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *frame, int size,
                    float radius, float cx, float cy, int alb_w, int alb_h, int y0, int y1, int dark_zero, float dk_sup,
                    const moon_bake_ev_t *bev) {
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
        c.dark_zero = dark_zero;
        c.dk_sup = dk_sup;
        c.bev = bev;
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
               float radius, float cx, float cy, int alb_w, int alb_h, int dark_zero, float dk_sup,
               const moon_bake_ev_t *bev) {
    moon_bake_rows(g, albedo, grel, frame, size, radius, cx, cy, alb_w, alb_h, 0, size, dark_zero, dk_sup, bev);
}

/* ---------------- constant-projection pass (the light-sleep dusk) ------
 *
 * The dusk spins the camera about the POLE at a FIXED phase epoch:
 * between its frames only the camera basis changes, not the light.
 * bake_px's color is a function of the body-frame surface direction r
 * and the epoch's fixed body-frame sun S alone (d = r . S, the
 * displaced-normal L from d + the per-cell gradient, the tone LUT, and
 * the phase-independent earthshine -- the view direction E selects
 * which r is visible but never enters the color). So the fully-lit
 * body-frame color map
 *
 *     C[cell] = the bake color of that cell's body direction
 *
 * is CONSTANT for the whole animation: build it once (moon_shade_map_*)
 * and every pole-spin frame re-projects that one fixed map through the
 * folded camera basis (moon_reproject_*) -- the same (theta, row_frac)
 * table pass as the bake, then ONE map load instead of the lighting. No
 * per-frame shadow/lighting recompute, and a fixed surface point keeps
 * exactly its color on every frame (no terminator flicker). The phase
 * sweep keeps per-frame bakes: there the sun moves between frames.
 *
 * The map is laid out like the albedo (row 0 = north, column 0 = the
 * texture column, i.e. body longitude - 180 + MOON_TEX_LON_OFFSET):
 * the re-projection's index math is the bake's albedo index math
 * verbatim, so map cell (c, r) is the bake color of the direction that
 * albedo cell covers, and the map is built AT the albedo's dimensions
 * (map_w x map_h == the albedo's).
 */

/* Build one row range [y0, y1) of the shade map: for each cell, the full
 * bake color of the cell's body direction (see the section header). The
 * albedo byte and the gradient cell are the cell's OWN texels (no index
 * math beyond the row pointers); the body direction walks the row by a
 * recurrence (one complex step per column, no per-cell transcendentals,
 * no tables).
 *
 * The column is the bake's THETA convention, not plain body longitude:
 * the bake's atan2_deg mirrors the longitude across the E-W axis in the
 * western half (theta = lon if cos lon >= 0 else 360 - lon -- the
 * east-left IAU image layout), and col = (theta + 180) * map_w / 360.
 * So the recurrence walks THETA (one full circle, no seam) and the body
 * direction takes (cos lam, sin lam) = (cos theta, +/- sin theta) with
 * the sign flipped in the mirrored half (cos(360 - th) = cos th,
 * sin(360 - th) = -sin th).
 *
 * The east/north tangent-basis dots with S simplify with rho = coslat
 * cancelling out:
 *   d       = coslat (cl S0 + sl S1) + sinlat S2
 *   east_s  = cl S1 - sl S0
 *   north_s = S2 - sinlat (cl S0 + sl S1)
 * (the bake's 1e-4 polar-cap floor cannot fire on a 432-row grid: the
 * row-center coslat is >= 0.0036 -- the same behavior as bake_px there).
 * lns <= 0 cells are the night side (dark color = TONE_LUT[0] +
 * earthshine(d) for dark_zero = 0, the validated model); with dark_zero
 * set, every geometrically dark cell (d < 0, or lns <= 0 once the
 * gradient tip lights the far side) is written the BACKLIT night side
 * instead -- the computed surface lit from -S (see moon_backlit_night) --
 * so the map's dark cells carry no earthshine/relief speckle that a
 * re-projected frame would pick up at the rim (the dusk's "bright
 * fragment on the edge"), yet the far side shows the faint lunar
 * surface. dk_sup (dusk only; 0.0f = the plain d < 0 terminator): with
 * dark_zero, the backlit side is written for d < dk_sup + MOON_DK_MARGIN
 * instead of d < dk_sup -- the same sliver kill the dusk's FINAL bake
 * applies (see moon_bake_rows), applied to the MAP so the intermediate
 * re-projected frames carry no lit rim sliver either (left out of the
 * map, the relief-boosted 0 < d < dk_sup band, up to gray 137 today,
 * sweeps the disk as a latitudinal arc while the pole spins -- the
 * "white ray" the user reported on 2026-10-29 -- and the last
 * re-projected frame would still show it while the final dk_sup bake
 * does not, a pop at the dusk's end). The +MOON_DK_MARGIN erosion
 * covers the re-project's NEAREST-NEIGHBOR rounding: a dark pixel
 * (d < dk_sup) within one cell of the d = dk_sup boundary can round to a
 * cell whose center is outside it, and a lit-center cell carries the FULL
 * LIT color -- so the dark band is written a bit wider than the bake's
 * bound (see MOON_DK_MARGIN). The margin's cost is sub-pixel: the lit
 * region the frames show is the body-fixed d > dk_sup (+ margin) region,
 * constant across frames (it only rotates, so no per-frame crescent
 * shrink) and in AGREEMENT with the final dk_sup bake at the endpoint
 * (the visible disk there is fully d < dk_sup, so the band between the
 * two bounds is never on screen). The one step change is at frame 1 (the
 * parked pose's true terminator -> the shifted one), which is the
 * user-accepted look: a clean crescent with a dark margin. The dusk
 * builds the map with its endpoint dk_sup; every other caller passes
 * 0.0f (the margin is inert there: the only dark_zero = 1 map caller is
 * the dusk). Row-range build so the caller (the dusk task) can honor an
 * abort between rows.
 *
 * map_dark (NULL or a second map buffer, same dimensions): the dusk's
 * LIT-FADE companion (the "earth ray" complete-kill, 2026-11-04) -- the
 * same map with every LIT cell written 0 (pure black; the backlit cells
 * are copied unchanged). The dusk fades the lit side to the backlit dark
 * side over the spin (the lit crescent slides along the disk's limb while
 * the pole spins and, hugging the rim, lingers as a bright arc through
 * the late frames -- real sun-lit cells the dk_kill margin cannot touch,
 * because their d is well above dk_kill). The re-project lerps the two
 * maps per pixel (moon_reproject_rows lit_w): the backlit cells are
 * identical in both, so only the lit cells move -- toward black as the
 * spin advances. NULL = the lit map only (every non-dusk caller). */
void moon_shade_map_rows(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *map,
                         uint8_t *map_dark, int map_w, int map_h, int y0, int y1, int dark_zero, float dk_sup,
                         const moon_bake_ev_t *bev) {
    const float S0 = g->S[0], S1 = g->S[1], S2 = g->S[2];
    const float dlon = 2.0f * (float)M_PI / (float)map_w;
    const float cosd = cosf(dlon), sind = sinf(dlon);
    const float deg2rad = (float)M_PI / 180.0f;
    const float dem_scale = (float)MOON_DEM_W / (float)map_w;
    for (int y = y0; y < y1; y++) {
        const float rf = ((float)y + 0.5f) / (float)map_h; /* the cell's row fraction */
        const float lat = 90.0f - 180.0f * rf;             /* row 0 = north */
        const float sinlat = sinf(lat * deg2rad), coslat = cosf(lat * deg2rad);
        const uint8_t *arow = albedo + (size_t)y * map_w;
        uint8_t *mrow = map + (size_t)y * map_w;
        uint8_t *mrow_dark = map_dark ? map_dark + (size_t)y * map_w : NULL;
        /* Gradient-cache row for the whole map row: the bake's
         * row_frac -> DEM-row mapping, at the cell centers. */
        int dr = 0;
        if (grel) {
            dr = (int)(rf * (float)MOON_DEM_H + 0.5f);
            if (dr >= MOON_DEM_H)
                dr = MOON_DEM_H - 1;
        }
        /* Texture column 0 is theta - 180 (MOON_TEX_LON_OFFSET): the
         * recurrence starts at the cell-center theta of column 0
         * (-180 + half a column) and walks one column per step (see the
         * theta-vs-lon note above). */
        const float th0 = -(float)M_PI + 0.5f * dlon;
        float ct = cosf(th0), st = sinf(th0); /* (cos theta, sin theta) */
        for (int x = 0; x < map_w; x++) {
            const float cl = ct;                      /* cos lam = cos theta (both halves) */
            const float sl = (ct >= 0.0f) ? st : -st; /* the mirror */
            const float a = cl * S0 + sl * S1;        /* the equatorial part of S */
            const float d = coslat * a + sinlat * S2;
            /* lns unclipped (the backlit night side needs it, see the
             * bake); L = clip(lns, 0, 1) = the lit-side Lambert. */
            float lns;
            if (grel == NULL) {
                lns = d;
            } else {
                /* == the bake's (theta + MOON_TEX_LON_OFFSET) * DEM_W/360
                 * mapping at this cell's theta center: (x + 0.5) *
                 * DEM_W / map_w. */
                const int dc = (int)((x + 0.5f) * dem_scale + 0.5f);
                const uint16_t gp = grel->pack[dr * MOON_DEM_W + dc];
                const float glon = (float)(int8_t)(gp & 0xFF) * MOON_GREL_INV;
                const float glat = (float)(int8_t)(gp >> 8) * MOON_GREL_INV;
                const float gs = glon * (cl * S1 - sl * S0) + glat * (S2 - sinlat * a);
                lns = (d - gs) / sqrtf(1.0f + glon * glon + glat * glat);
            }
            float L = lns > 1.0f ? 1.0f : (lns < 0.0f ? 0.0f : lns);
            /* Blood moon (bev, moon_event.h): the cell's LIT side carries
             * the same per-cell umbra/penumbra occlusion as the bake's
             * lit pixels (L *= 1 - 0.85*occ, BEFORE the tone LUT) so the
             * re-projected dusk frames and the final bake agree on the
             * eclipse "bite". The cell's body direction is
             * (coslat*cl, coslat*sl, sinlat). */
            if (bev != NULL && bev->blood) {
                const float dvec[3] = {coslat * cl, coslat * sl, sinlat};
                const float occ = moon_event_occ(bev, dvec);
                if (occ > 0.0f)
                    L *= 1.0f - MOON_EV_BLOOD_K * occ;
            }
            float out;
            int lit;
            /* dk_sup = 0.0f -> the plain d < 0 terminator (the margin is
             * inert there); the +MOON_DK_MARGIN erosion: see the doc above. */
            if (dark_zero ? ((d < dk_sup + MOON_DK_MARGIN) || (lns <= 0.0f)) : (lns <= 0.0f)) {
                lit = 0;
                if (dark_zero) {
                    out = (float)moon_backlit_night((float)arow[x], lns); /* the computed night side */
                } else {
                    out = MOON_TONE_LUT[0] + earthshine(d);
                }
            } else {
                lit = 1;
                const int idx = (int)((float)arow[x] * L + 0.5f);
                out = MOON_TONE_LUT[idx > 255 ? 255 : idx] + earthshine(d);
            }
            mrow[x] = (uint8_t)(out < 0.0f ? 0.0f : (out > 255.0f ? 255.0f : out + 0.5f));
            /* The dusk's lit-fade companion (see moon_reproject_rows): the
             * same cell with every LIT cell zeroed -- the all-dark map the
             * fade lerps toward. NULL map_dark = the lit map only (every
             * non-dusk caller). */
            if (mrow_dark)
                mrow_dark[x] = (uint8_t)(lit ? 0 : mrow[x]);
            /* Next cell: advance theta by one column (a complex rotation). */
            const float nct = ct * cosd - st * sind;
            st = ct * sind + st * cosd;
            ct = nct;
        }
    }
}

/* Build the whole shade map (see the section header). One-time cost per
 * dusk epoch (~2 s on the S3 for the 864 x 432 map). dark_zero / dk_sup /
 * map_dark: see moon_shade_map_rows. */
void moon_shade_map_build(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *map,
                          uint8_t *map_dark, int map_w, int map_h, int dark_zero, float dk_sup,
                          const moon_bake_ev_t *bev) {
    moon_shade_map_rows(g, albedo, grel, map, map_dark, map_w, map_h, 0, map_h, dark_zero, dk_sup, bev);
}

/* One pixel of the re-projection: bake_px WITHOUT the lighting -- the
 * base direction r from the (folded) camera basis, the same (theta,
 * row_frac) table pass, then ONE nearest-neighbor load of the pre-shaded
 * map (which carries the complete color, earthshine included).
 * always_inline so the 4-px unroll can overlap the map loads (the same
 * load-bearing note as bake_px). */
typedef struct {
    const uint8_t *map;
    const uint8_t *map_dark; /* the lit-fade companion (see the
                                moon_shade_map_rows doc): NULL = the plain
                                gather (every non-dusk caller) */
    int map_w, map_h;
    const moon_geom_t *g;
    uint8_t *row;
    float radius, cx, v;
    float per_deg;
    float lit_w; /* lit-side weight (1.0f = the lit map; 0.0f = the dark
                    companion): only the LIT cells differ between the two
                    maps (the dark cells are identical in both), so the lerp
                    moves exactly the lit crescent. The dusk's "earth ray"
                    complete-kill: the lit crescent slides along the disk's
                    limb while the pole spins and lingers as a bright rim
                    arc through the late frames -- the fade drives it to
                    black over the spin (lit_w 1 -> 0, eased, the first half
                    of the frames) so it is GONE by the endpoint (the final
                    frame is the dk_kill bake, which the fade does not
                    touch). lit_w = 1.0f and map_dark = NULL are the
                    unmodified gather. */
} reproj_ctx_t;

static __attribute__((always_inline)) inline void reproject_px(reproj_ctx_t *c, int x) {
    float u = ((float)x - c->cx) / c->radius;
    float r2 = u * u + c->v * c->v;
    if (r2 > 1.0f) {
        c->row[x] = 0;
        return;
    }
    float w = sqrtf(1.0f - r2);
    float rx = u * c->g->right[0] + c->v * c->g->up[0] + w * c->g->E[0];
    float ry = u * c->g->right[1] + c->v * c->g->up[1] + w * c->g->E[1];
    float rz = u * c->g->right[2] + c->v * c->g->up[2] + w * c->g->E[2];
    /* No dark-side kill here: the map ALREADY carries the dark side (the
     * backlit night side, written for d < dk_sup + MOON_DK_MARGIN by
     * moon_shade_map_rows), so a dark pixel (d < dk_sup) samples backlit
     * cells -- pure black where the cell is sun-lit (0 < d < dk_sup rim
     * sliver, lns > 0), faint surface where d < 0 -- and can never round
     * to a lit cell (the margin covers the nearest-neighbor rounding).
     * The frames agree with the dusk's FINAL dk_sup bake at the endpoint
     * (the visible disk there is fully d < dk_sup, all backlit in both). */
    float theta = atan2_deg(rx, ry);
    float row_frac = row_frac_interp(rz);
    float colf = (theta + MOON_TEX_LON_OFFSET) * c->per_deg;
    if (colf >= (float)c->map_w)
        colf -= (float)c->map_w;
    int col = (int)(colf + 0.5f);
    if (col >= c->map_w)
        col = c->map_w - 1;
    int r = (int)(row_frac * (float)c->map_h + 0.5f);
    if (r < 0)
        r = 0;
    else if (r >= c->map_h)
        r = c->map_h - 1;
    size_t i = (size_t)r * c->map_w + col;
    const uint8_t litv = c->map[i];
    if (c->map_dark == NULL) {
        c->row[x] = litv;
        return;
    }
    /* The lit fade (see lit_w): only the lit cells differ between the two
     * maps, so this lerp moves exactly the crescent -- backlit cells stay
     * at the companion's (identical) value, and the rim arc the dusk must
     * remove eases to black. The lerp is exact at lit_w = 0/1 (the frame-0
     * and any no-fade caller are the unmodified gather). */
    const float lw = c->lit_w;
    c->row[x] = (uint8_t)((float)litv * lw + (float)c->map_dark[i] * (1.0f - lw) + 0.5f);
}

/* Re-project the pre-shaded body-frame equirect map (moon_shade_map_build)
 * through the camera basis g into an 8-bit frame (size x size, row-major,
 * 0 outside the disk), rows [y0, y1): per pixel r = u*right + v*up + w*E
 * (the bake's base direction at the folded basis), the bake's
 * (theta, row_frac) table pass, then the nearest-neighbor map sample.
 * g = the orbit + in-plane + pole folds of the epoch's geometry (the
 * dusk's per-frame basis). The map carries the WHOLE color -- the lit
 * side and the backlit night side (see moon_shade_map_rows) -- so the
 * re-project is a pure gather: no dark-side bound, no kill (the old
 * dk_sup early return is gone; the map's MOON_DK_MARGIN erosion does the
 * sliver/aliasing work up front).
 *
 * map_dark / lit_w (see reproj_ctx_t): the dusk's lit-fade variant --
 * lerp the map toward its all-dark companion by (1 - lit_w), which moves
 * exactly the lit cells (the companion is identical there). The plain
 * gather is map_dark = NULL (or lit_w = 1.0f with it). */
void moon_reproject_rows(const uint8_t *map, const uint8_t *map_dark, int map_w, int map_h, const moon_geom_t *g,
                         uint8_t *frame, int size, float radius, float cx, float cy, int y0, int y1, float lit_w) {
    if (!s_tbl_init)
        tables_build();
    const float per_deg = (float)map_w / 360.0f;
    for (int y = y0; y < y1; y++) {
        reproj_ctx_t c;
        c.map = map;
        c.map_dark = map_dark;
        c.map_w = map_w;
        c.map_h = map_h;
        c.g = g;
        c.row = &frame[(size_t)y * size];
        c.radius = radius;
        c.cx = cx;
        c.v = (cy - (float)y) / radius; /* v up: row 0 is top */
        c.per_deg = per_deg;
        c.lit_w = lit_w;
        for (int x0 = 0; x0 < size; x0 += 4) {
            int n = size - x0;
            reproject_px(&c, x0);
            if (n > 1)
                reproject_px(&c, x0 + 1);
            if (n > 2)
                reproject_px(&c, x0 + 2);
            if (n > 3)
                reproject_px(&c, x0 + 3);
        }
    }
}

void moon_reproject(const uint8_t *map, int map_w, int map_h, const moon_geom_t *g, uint8_t *frame, int size,
                    float radius, float cx, float cy) {
    moon_reproject_rows(map, NULL, map_w, map_h, g, frame, size, radius, cx, cy, 0, size, 1.0f);
}
