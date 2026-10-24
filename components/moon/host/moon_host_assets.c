/* Host asset backend: the generated C array (moon_assets_gen.c).
 * Identical bytes to the device's albedo.bin; the host simulator is the
 * C-vs-NASA gate, so the two backends must agree. */
#include "moon_assets.h"
#include "moon_consts.h"

extern const uint8_t MOON_ALBEDO[MOON_ALB_W * MOON_ALB_H];

const uint8_t *moon_albedo(void) { return MOON_ALBEDO; }

void moon_deinit(void) { /* no-op */ }
