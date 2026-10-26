/* Host asset backend: the generated C arrays (moon_assets_gen.c).
 * Identical bytes to the device's moon_assets.bin (albedo.bin + dem.bin
 * concatenated); the host simulator is the C-vs-NASA gate, so the two
 * backends must agree. The relief cache is built lazily on the first
 * moon_relief() call into two planes -- same bytes as the device's PSRAM
 * cache, since both go through the same portable moon_relief_row. */
#include <stdlib.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

extern const uint8_t MOON_ALBEDO[MOON_ALB_W * MOON_ALB_H];
extern const int8_t MOON_DEM[MOON_DEM_W * MOON_DEM_H];

static int8_t *glon_cache;
static int8_t *glat_cache;
static moon_grel_t s_grel;

const uint8_t *moon_albedo(void) { return MOON_ALBEDO; }

const moon_grel_t *moon_relief(void) {
    if (!glon_cache) {
        size_t plane = (size_t)MOON_DEM_W * MOON_DEM_H;
        glon_cache = (int8_t *)malloc(plane);
        glat_cache = (int8_t *)malloc(plane);
        if (!glon_cache || !glat_cache)
            return NULL;
        moon_relief_build(MOON_DEM, MOON_DEM_W, MOON_DEM_H, glon_cache, glat_cache);
        s_grel.glon = glon_cache;
        s_grel.glat = glat_cache;
    }
    return &s_grel;
}

void moon_deinit(void) { /* no-op: the cache lives for the simulator's process */ }
