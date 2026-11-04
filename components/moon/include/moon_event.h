/* Pre-calculated sky-event classification for the moon display.
 *
 * The clock's moon should read clearly for the special full moons (the
 * 2026-11-04 bugreport: "pre-calculate sky-event colors"): harvest,
 * blue, super-/micromoon and blood (lunar eclipse). The classifier is
 * ephemeris-only (moon_geometry's cheap eraEpv00 + eraMoon98 pair plus
 * the civil calendar -- no network), so the exact same translation
 * unit builds into the device and the host simulator.
 *
 * The color itself is a pre-calculated 256-entry RGB565 LUT built ONCE
 * per event (moon_gray565_set_event in moon_render.c), composed over
 * the display's warm tint -- the bake stays 8-bit gray, so the event
 * costs one LUT rebuild, not per-pixel color math. The one exception is
 * the BLOOD moon's per-pixel umbra occlusion (the eclipse "bite" lands
 * in the right place on the disk): the bake multiplies the lit gray by
 * (1 - 0.85 * occ) BEFORE the LUT, where occ is the smooth 0..1 Earth
 * shadow occlusion of the pixel's surface direction.
 */
#ifndef MOON_EVENT_H
#define MOON_EVENT_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "moon_consts.h"

/* Earth-shadow cone geometry (physical constants, not part of the
 * Python-validated render model -- the gate never exercises them, so
 * they live here, not in the generated moon_consts.h). Both cones are
 * the straight line from one solar limb to the opposite terrestrial
 * limb, so their radii at the axial coordinate a (m from the Earth)
 * are linear in a:
 *   umbra tip    D_tip = R_earth * AU / (R_sun - R_earth) ~ 1.383e9 m
 *                (~ 1,383,000 km, the classical umbra-tip distance)
 *                (the line sun-limb -> earth-limb crosses the axis just
 *                past the Moon; the cone radius there is 0)
 *   penumbra     slope = (R_sun + R_earth) / AU ~ 0.004693 1/m
 *                (the line sun-limb -> OPPOSITE earth-limb, i.e. the
 *                penumbral edge diverges from the Earth at the sum of
 *                the two angular radii)
 * At the mean lunar distance: umbra ~ 4,580 km, penumbra ~ 8,175 km
 * (the eclipse figures every almanac prints). */
#define MOON_EV_EARTH_R_M 6371000.0
#define MOON_EV_SUN_R_M 695700000.0
#define MOON_EV_AU_M 149597870.7e3
#define MOON_EV_UMBRA_TIP_M (MOON_EV_EARTH_R_M * MOON_EV_AU_M / (MOON_EV_SUN_R_M - MOON_EV_EARTH_R_M))
#define MOON_EV_PENUMBRA_SLOPE ((MOON_EV_SUN_R_M + MOON_EV_EARTH_R_M) / MOON_EV_AU_M)
/* The lit-side multiplier during a blood moon: L *= (1 - 0.85 * occ)
 * (the plan's value: a total eclipse still shows a faint copper disk,
 * not a black hole). */
#define MOON_EV_BLOOD_K 0.85f

/* A UTC instant's moon events (moon_event_classify). All flags false =
 * an ordinary day (the bake takes a NULL / all-false event and is
 * byte-identical to the pre-event bake).
 *
 *   harvest    full moon whose civil date falls in Sep 22 .. Oct 23
 *              (the closest full moon to the autumn equinox -- the
 *              civil rule, deterministic)
 *   blue       full moon that is the 2nd full moon of its civil month
 *              (the PREVIOUS full-moon instant F_prev falls in the same
 *              month; a month holds at most two full moons, so this is
 *              exact)
 *   super_moon full moon with geocentric distance < 357,500 km at F
 *   micro_moon full moon with geocentric distance > 405,500 km at F
 *              (the 60-year census 2000..2060, all 742 full moons,
 *               envelopes 356,431 .. 406,528 km -- the bounds sit
 *               ~1,000 km inside the extremes and fire 60 / 61 times,
 *               ~once a year; the naive perigee/apogee values would
 *               fire at most once in 60 years)
 *
 * "Full moon" = |epoch - F| <= 18 h, F = the exact opposition (the
 * argmax of S . E, the minimum phase angle). A literal S . E > 0.999
 * never fires when the full moon's ecliptic latitude exceeds 2.56 deg
 * (up to 5.15 deg, S . E down to 0.996), so a dot-product test would
 * lose roughly half of all full moons; the moment test catches every
 * one (across the window the disk is at least ~98.5% illuminated). The
 * date-based flags (harvest, blue) key on F's civil date, stable across
 * the whole window.
 *   blood      lunar eclipse at the epoch (the Moon's center inside the
 *              Earth's shadow cone) -- the per-pixel umbra occlusion
 *              below carries the geometry.
 *
 * The shadow fields (valid when blood) locate the Earth's shadow in the
 * body frame: the umbra cone's AXIS passes through shadow_off (meters,
 * from the Moon's center; the closest point of the axis to the Moon's
 * center) in the unit direction shadow_dir (the geocentric anti-solar).
 * The cone radii at a point's axial coordinate a (meters from the
 * Earth) are linear (see the constants above for the tip and slope):
 *   umbra    rho_u = R_earth * (1 - a / D_tip)
 *   penumbra rho_p = R_earth + a * (R_sun + R_earth) / AU */
typedef struct {
    bool blood;
    bool harvest;
    bool blue;
    bool super_moon;
    bool micro_moon;
    /* Eclipse shadow geometry (body frame; valid when blood). */
    float shadow_dir[3];
    float shadow_off[3];
    float off_ax; /* The Moon CENTER's axial coordinate a = mg . s (meters from
                     the Earth), where mg = geocenter->Moon and s = shadow_dir:
                     shadow_off is the axis point at exactly that coordinate,
                     so a pixel's axial coordinate is off_ax + (p . dir) */
} moon_event_t;

/* Optional bake event (moon_bake / moon_bake_rows / moon_shade_map_rows):
 * NULL (or an all-false event) = no event, byte-identical to the
 * pre-event bake. When blood, the bake multiplies the lit Lambert by
 * (1 - 0.85 * moon_event_occ(bev, r)) per pixel BEFORE the tone LUT. */
typedef moon_event_t moon_bake_ev_t;

/* Classify the UTC instant (see the struct for the rules). Cheap per
 * call: a handful of ephemeris evaluations plus the cached full-moon
 * peak search (the 60-day S . E scan runs once per calendar day, not
 * per call). */
void moon_event_classify(int64_t utc_epoch, moon_event_t *ev);

/* True when ANY of the instant's events is set (the device's
 * "pass the event to the bake" test -- an all-false event is baked as
 * NULL, byte-identically). */
static inline bool moon_event_any(const moon_event_t *ev) {
    return ev != NULL && (ev->blood || ev->harvest || ev->blue || ev->super_moon || ev->micro_moon);
}

/* The event's 5-bit signature (0 = an ordinary instant). The device's
 * day-tick compares it against the last baked signature to re-bake when
 * an event STARTS or ENDS mid-day (a lunar eclipse happens at the hour
 * it happens, not at the UTC midnight the date-change tick sees). */
static inline int moon_event_signature(const moon_event_t *ev) {
    return (int)(ev->blood ? 1 : 0) | (ev->harvest ? 2 : 0) | (ev->blue ? 4 : 0) | (ev->super_moon ? 8 : 0) |
           (ev->micro_moon ? 16 : 0);
}

/* One pixel's Earth-shadow occlusion, smooth 0..1: 1 fully inside the
 * umbra, 0 outside the penumbra, a linear ramp between the two cone
 * edges (the partial-eclipse "bite"). d = the pixel's unit surface
 * direction, body frame (the bake's r). 0 for a non-blood event.
 * Header-inline: the bake hot path (moon_bake.c) and the host tools
 * share it without a link dependency. */
static inline float moon_event_occ(const moon_event_t *ev, const float d[3]) {
    if (ev == NULL || !ev->blood)
        return 0.0f;
    /* Surface point (meters, Moon-centered): the Moon's radius along d.
     * Distance from the shadow axis (a line through shadow_off in
     * shadow_dir): |rel - (rel . dir) dir| with rel = p - shadow_off. */
    const float px = MOON_RADIUS_M * d[0] - ev->shadow_off[0];
    const float py = MOON_RADIUS_M * d[1] - ev->shadow_off[1];
    const float pz = MOON_RADIUS_M * d[2] - ev->shadow_off[2];
    const float t = px * ev->shadow_dir[0] + py * ev->shadow_dir[1] + pz * ev->shadow_dir[2];
    float perp2 = px * px + py * py + pz * pz - t * t;
    if (perp2 < 0.0f)
        perp2 = 0.0f;
    const float perp = sqrtf(perp2);
    /* Axial coordinate of the point (meters from the Earth): the
     * closest point of the axis plus the along-axis component. */
    const float a = ev->off_ax + t;
    const float tip = MOON_EV_UMBRA_TIP_M; /* D_tip = R_earth * AU / (R_sun - R_earth) */
    float ru = MOON_EV_EARTH_R_M * (1.0f - a / tip);
    if (ru < 0.0f)
        ru = 0.0f;
    const float rp = MOON_EV_EARTH_R_M + a * MOON_EV_PENUMBRA_SLOPE; /* slope = (R_sun + R_earth) / AU */
    if (perp >= rp)
        return 0.0f;
    if (perp <= ru)
        return 1.0f;
    return (rp - perp) / (rp - ru);
}

#endif /* MOON_EVENT_H */
