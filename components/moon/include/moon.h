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
 *     full Lambert of the base-sphere dot L = clip(d, 0, 1) with
 *     d = r.S (relief dropped by measurement), equirect albedo
 *     sampled with the live camera basis (libration + posangle exact),
 *     then the phase-independent earthshine, then the 256-entry tone LUT.
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

/* Bake one lit 8-bit grayscale frame (size x size, row-major) from an
 * equirect albedo (alb_w x alb_h u8, row 0 = north). alb_w/alb_h are the
 * albedo's dimensions (the device passes MOON_ALB_W/MOON_ALB_H for the full
 * texture, or a smaller SRAM albedo for the fast boot animation). 0 outside
 * the disk. Lighting/earthshine/tone order = the validated model. */
void moon_bake(const moon_geom_t *g, const uint8_t *albedo, uint8_t *frame, int size, float radius, float cx, float cy,
               int alb_w, int alb_h);

/* In-plane rotation of the baked 8-bit frame into an RGB565 output
 * (size x size). cos15/sin15 are Q15.15 in int32 (cos(0) = 32768);
 * nearest-neighbor gather (1 PSRAM read/px); out of frame = 0 (black). */
void moon_rotate(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                 int32_t cos15, int32_t sin15);

/* Gray 0..255 -> RGB565 (round-to-nearest 256-entry LUT; 1 lookup/px). */
uint16_t moon_gray565(uint8_t g);

/* Q15.15 sin/cos, int32 (2048-entry quarter-symmetric table, moon_fixp.c). */
int32_t moon_sin15(float rad);
int32_t moon_cos15(float rad);

#endif /* MOON_H */
