/* Host asset backend: the generated C arrays (moon_assets_gen.c).
 * Identical bytes to the device's moon_assets.bin (albedo.bin + dem.bin
 * concatenated); the host simulator is the C-vs-NASA gate, so the two
 * backends must agree. The relief cache is built lazily on the first
 * moon_relief() call into the packed array -- same bytes as the
 * device's PSRAM cache, since both go through the same portable
 * moon_relief_row. */
#include <stdlib.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

extern const uint8_t MOON_ALBEDO[MOON_ALB_W * MOON_ALB_H];
extern const int8_t MOON_DEM[MOON_DEM_W * MOON_DEM_H];

static uint16_t *gpack;
static moon_grel_t s_grel;

const uint8_t *moon_albedo(void) { return MOON_ALBEDO; }

const moon_grel_t *moon_relief(void) {
    if (!gpack) {
        size_t cells = (size_t)MOON_DEM_W * MOON_DEM_H;
        gpack = (uint16_t *)malloc(cells * sizeof(uint16_t));
        if (!gpack)
            return NULL;
        moon_relief_build(MOON_DEM, MOON_DEM_W, MOON_DEM_H, gpack);
        s_grel.pack = gpack;
    }
    return &s_grel;
}

void moon_deinit(void) { /* no-op: the cache lives for the simulator's process */ }
