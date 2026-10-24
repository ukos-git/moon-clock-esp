/* Asset backend seam: device (raw partition) vs host (linked array).
 *
 * moon_albedo() returns the MOON_ALB_W x MOON_ALB_H equirect 8-bit albedo
 * (row 0 = north, col 0 = longitude 0), valid until moon_deinit().
 * Device: esp_partition_mmap of the moon_assets raw partition (read-only,
 * zero-copy). Host: the generated C array (moon_assets_gen.c).
 */
#ifndef MOON_ASSETS_H
#define MOON_ASSETS_H

#include <stdint.h>

const uint8_t *moon_albedo(void);

/* Device only: release the mapping. Host: no-op. */
void moon_deinit(void);

#endif /* MOON_ASSETS_H */
