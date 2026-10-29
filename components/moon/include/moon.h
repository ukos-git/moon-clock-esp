/* Moon phase display: geometry, bake, and in-plane rotation.
 *
 * The portable core (core/) is pure C + vendored public-domain ERFA
 * ephemeris code -- no ESP- or LVGL-dependency, so the exact same
 * translation units build into the host simulator (host_sim/) and the
 * device. The math ports the validated Python prototype 1:1
 * (assets/images/moon/moon/):
 *
 *   - body frame: calibrated IAU_Moon rotation (pole + prime meridian,
 *     moon_consts.h), ephemeris eraEpv00 + eraMoon98 (the same models
 *     the validated astropy pipeline uses internally).
 *   - bake: per-pixel base-sphere direction r = u*right + v*up + w*E,
 *     full-disk displaced-normal Lambert L = clip(lns, 0, 1) from the int8
 *     relief DEM (4-neighbor gradient; mountain shadows at the
 *     terminator), equirect albedo sampled with the live camera basis
 *     (libration + posangle exact), then the phase-independent
 *     earthshine, then the 256-entry tone LUT.
 *   - rotate: fixed-point in-plane gather ("ball in hand").
 */
#ifndef MOON_H
#define MOON_H

#include <stdint.h>

/* Body-frame geometry for one UTC instant (moon_consts.h for the math). */
typedef struct {
    float S[3];         /* Moon->Sun, unit, body frame (pole = +z) */
    float E[3];         /* Moon->geocenter, unit (the visible pole) */
    float right[3];     /* image +x (east left), body frame         */
    float up[3];        /* image +y (celestial north up), body frame */
    float subsolar_lon; /* deg (gate/debug only) */
    float subsolar_lat;
    float subearth_lon;
    float subearth_lat;
    float posangle; /* deg, east of celestial north */
    float phase_angle_deg;
    float illum; /* (1 + cos i) / 2 */
    float distance_km;
} moon_geom_t;

/* UTC epoch seconds -> body-frame geometry. Cheap ephemeris (eraEpv00 +
 * eraMoon98), validated against NASA SVS 5415: max 0.43 deg subsolar /
 * 0.51 deg subearth / 0.44 deg posangle over the 32-frame gate set. */
void moon_geometry(int64_t utc_epoch, moon_geom_t *g);

/* The per-cell relief gradient cache (see moon_relief_build): ONE
 * uint16 array of MOON_DEM_W x MOON_DEM_H packed cells, the DEM's
 * central-difference slopes quantized to int8 (see moon_bake.c for the
 * scale), glon (longitude/east) in the low byte and glat
 * (latitude/north) in the high byte. Built once from the relief DEM;
 * the bake reads ONE 16-bit cache load per pixel instead of a
 * 4-neighbor DEM gather. NULL grel (or NULL pack) = relief-free
 * Lambert. */
typedef struct {
    const uint16_t *pack; /* packed glon (low) / glat (high) slope cells */
} moon_grel_t;

/* Bake one lit 8-bit grayscale frame (size x size, row-major) from an
 * equirect albedo (alb_w x alb_h u8, row 0 = north) and the relief
 * gradient cache (moon_grel_t, moon_relief_build output; NULL =
 * relief-free Lambert). alb_w/alb_h are the albedo's dimensions (the
 * device passes MOON_ALB_W/MOON_ALB_H for the full texture, or a
 * smaller SRAM albedo for the fast boot animation). 0 outside the
 * disk. Lighting/earthshine/tone order = the validated model. The
 * cached per-cell slope (quantized, see moon_bake.c) replaces the old
 * per-pixel 4-neighbor DEM gather: same displaced-normal model, 2
 * PSRAM loads/px (1 albedo + 1 packed gradient) instead of 6.
 * dark_zero writes the dark side as a CONSTANT night floor
 * (MOON_NIGHT_FLOOR, moon_consts.h) instead of the earthshine
 * gradient -- the device wake-sweep / dusk / static phase use it: the
 * ~17-step earthshine ramp shows up as banded/dithered speckle that
 * reads as "moving earthshine" in the fast animation, and a constant
 * floor keeps the far side faintly visible on the AMOLED (flat 0x0000
 * was invisible). Static/spin bakes keep the earthshine (dark_zero =
 * 0). dk_sup (dusk only; 0.0f = the plain d < 0 terminator): the
 * night floor is written for d < dk_sup instead of d < 0 -- the dusk's
 * endpoint view still carries a geometrically lit rim sliver (the pole
 * spin never changes the view latitude, d up to sqrt(1 - E.S(phi*)^2)),
 * which would otherwise linger on the dark sleep screen as a bright arc
 * around the disk edge (the "white ray"). The dusk passes that bound;
 * every other caller passes 0.0f (byte-identical to the old bake). */
void moon_bake(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *frame, int size,
               float radius, float cx, float cy, int alb_w, int alb_h, int dark_zero, float dk_sup);

/* Row-range bake: identical per-pixel math as moon_bake() for rows
 * [y0, y1). The device boot animation bakes row by row and converts each
 * row to RGB565 immediately, so the display can show finished rows (as
 * progressive strips) while the rest of the frame is still being
 * computed -- the frame "sweeps" in instead of popping in.
 * dark_zero / dk_sup: see moon_bake. */
void moon_bake_rows(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *frame, int size,
                    float radius, float cx, float cy, int alb_w, int alb_h, int y0, int y1, int dark_zero,
                    float dk_sup);

/* Constant-projection pass (the light-sleep dusk): the renderer's color
 * depends only on the body direction and the epoch's fixed body-frame
 * sun -- the camera basis selects which direction is visible but never
 * enters the color -- so a pole spin at a fixed phase re-projects ONE
 * pre-shaded body-frame equirect map instead of re-baking (no per-frame
 * shadow/lighting recompute; a fixed surface point keeps its color, no
 * terminator flicker). moon_shade_map_* builds the map, moon_reproject_*
 * renders it through a (folded) camera basis. See moon_bake.c. */

/* Build one row range [y0, y1) of the pre-shaded body-frame equirect map
 * (map_w x map_h bytes == the albedo's dimensions, row 0 = north, column
 * 0 = the texture column): map cell (c, r) = the full bake color (albedo
 * x displaced-normal Lambert x tone LUT + earthshine) of that cell's
 * body direction, at S = g->S; dark_zero = 0 keeps the earthshine dark
 * side, 1 writes every geometrically dark cell (d < 0 or L <= 0) 0 (the
 * flat-black night side). dk_sup (dusk only; 0.0f = the plain d < 0
 * terminator): with dark_zero, the night floor is written for d < dk_sup
 * instead of d < 0 -- the same sliver kill the dusk's FINAL bake applies
 * (see moon_bake), applied to the MAP so the intermediate re-projected
 * frames carry no lit rim sliver either (the "white ray": the relief-
 * boosted 0 < d < dk_sup band sweeping the disk as a latitudinal arc
 * while the pole spins). The lit region the frames then show is the
 * body-fixed d > dk_sup region -- constant across frames (it only
 * rotates), and it AGREES with the final dk_sup bake at the endpoint (no
 * pop between the last re-projected frame and the dark-side bake). The
 * dusk builds the map with its endpoint dk_sup; every other caller
 * passes 0.0f (byte-identical to the old map). Row-range build so the
 * caller can honor an abort between rows; grel NULL = relief-free
 * Lambert. */
void moon_shade_map_rows(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *map, int map_w,
                         int map_h, int y0, int y1, int dark_zero, float dk_sup);

/* Build the whole shade map (see moon_bake.c): one-time per epoch.
 * dark_zero / dk_sup: see moon_shade_map_rows. */
void moon_shade_map_build(const moon_geom_t *g, const uint8_t *albedo, const moon_grel_t *grel, uint8_t *map, int map_w,
                          int map_h, int dark_zero, float dk_sup);

/* Re-project the pre-shaded map (moon_shade_map_build) through the camera
 * basis g into an 8-bit frame (size x size, row-major, 0 outside the
 * disk): per pixel the bake's base direction r = u*right + v*up + w*E,
 * the bake's (theta, row_frac) table pass, then the nearest-neighbor map
 * sample -- the bake WITHOUT the lighting (the map carries the color).
 * g = the epoch's geometry folded by the orbit / in-plane / pole basis
 * rotations. dk_sup (dusk only; 0.0f = the plain d < 0 terminator,
 * byte-identical to the old reproject): the same dark-side bound the bake
 * and the map apply (see moon_bake / moon_shade_map_rows) -- every pixel
 * whose TRUE body direction d = r . S is below dk_sup resolves to the
 * constant night floor instead of the map sample, so the re-projected
 * frames agree with the dusk's FINAL dk_sup bake at the endpoint (no pop)
 * and the 1-2 px "white ray" (a +-y seam band of the map's mirror
 * carrying the mirrored direction's lit value) can never light a dark
 * pixel. Row-range variant so the dusk task can honor an abort between
 * rows. */
void moon_reproject_rows(const uint8_t *map, int map_w, int map_h, const moon_geom_t *g, uint8_t *frame, int size,
                         float radius, float cx, float cy, int y0, int y1, float dk_sup);
void moon_reproject(const uint8_t *map, int map_w, int map_h, const moon_geom_t *g, uint8_t *frame, int size,
                    float radius, float cx, float cy, float dk_sup);

/* Build one output ROW r of the relief gradient cache from the three
 * DEM window rows the caller holds (row_n = DEM[r-1 clamped], row_m =
 * DEM[r], row_s = DEM[r+1 clamped]), each dem_w int8. Writes dem_w
 * packed uint16 cells to gpack (glon low byte / glat high byte). The
 * whole-array moon_relief_build is a loop over this. Pure C, identical
 * math on host and device (the host gate image-diffs the built cache
 * against NASA). */
void moon_relief_row(const int8_t *row_n, const int8_t *row_m, const int8_t *row_s, int dem_w, int dem_h, int r,
                     uint16_t *gpack);

/* Build the relief gradient cache (see moon_bake.c): the DEM's
 * 4-neighbor central-difference slopes, quantized to int8, packed into
 * ONE MOON_DEM_W x MOON_DEM_H uint16 array (glon = longitude/east in
 * the low byte, glat = latitude/north in the high byte). Phase-
 * independent: build once at boot, bake forever (this is what makes
 * the wake-sweep frames cheap). */
void moon_relief_build(const int8_t *dem, int dem_w, int dem_h, uint16_t *pack);

/* In-plane rotation of the baked 8-bit frame into an RGB565 output
 * (size x size). cos15/sin15 are Q15.15 in int32 (cos(0) = 32768);
 * nearest-neighbor gather (1 PSRAM read/px); out of frame = 0 (black). */
void moon_rotate(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                 int32_t cos15, int32_t sin15);

/* Row-range rotation: identical per-pixel math as moon_rotate() for rows
 * [y0, y1). The device orbit (peek) re-bake sweeps a freshly baked frame
 * into the display buffer 25 rows at a time, so the new viewport appears
 * as a top-to-bottom sweep instead of a pop. */
void moon_rotate_rows(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                      int32_t cos15, int32_t sin15, int y0, int y1);

/* Gray 0..255 -> RGB565 (round-to-nearest 256-entry LUT; 1 lookup/px). */
uint16_t moon_gray565(uint8_t g);

/* Q15.15 sin/cos, int32 (2048-entry quarter-symmetric table, moon_fixp.c). */
int32_t moon_sin15(float rad);
int32_t moon_cos15(float rad);

#endif /* MOON_H */
