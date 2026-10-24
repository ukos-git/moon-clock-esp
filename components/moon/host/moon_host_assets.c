/* Host asset backend: the generated C arrays (moon_assets_gen.c).
 * Identical bytes to the device's moon_assets.bin (albedo.bin + dem.bin
 * concatenated); the host simulator is the C-vs-NASA gate, so the two
 * backends must agree. */
#include "moon_assets.h"
#include "moon_consts.h"

extern const uint8_t MOON_ALBEDO[MOON_ALB_W * MOON_ALB_H];
extern const int8_t MOON_DEM[MOON_DEM_W * MOON_DEM_H];

const uint8_t *moon_albedo(void) { return MOON_ALBEDO; }

const int8_t *moon_dem(void) { return MOON_DEM; }

void moon_deinit(void) { /* no-op */ }
