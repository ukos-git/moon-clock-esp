/* Asset backend seam: device (raw partition) vs host (linked array).
 *
 * moon_albedo() returns the MOON_ALB_W x MOON_ALB_H equirect 8-bit albedo
 * (row 0 = north, col 0 = longitude 0), and moon_dem() the MOON_DEM_W x
 * MOON_DEM_H int8 relief DEM (same equirect convention), both valid until
 * moon_deinit(). Device: the moon_assets raw partition, copied to PSRAM
 * once (albedo @0, DEM immediately after; read-only thereafter). Host:
 * the generated C arrays (moon_assets_gen.c).
 */
#ifndef MOON_ASSETS_H
#define MOON_ASSETS_H

#include <stdint.h>

const uint8_t *moon_albedo(void);

/* The int8 relief DEM (mountain shadows at the terminator). NULL if the
 * partition is missing / too small for both blobs. */
const int8_t *moon_dem(void);

/* Device only: release the buffers. Host: no-op. */
void moon_deinit(void);

#endif /* MOON_ASSETS_H */
