/* Asset backend seam: device (raw partition) vs host (linked array).
 *
 * moon_albedo() returns the MOON_ALB_W x MOON_ALB_H equirect 8-bit albedo
 * (row 0 = north, col 0 = longitude 0), and moon_relief() the relief
 * gradient cache -- the DEM's per-cell 4-neighbor slopes quantized to
 * int8 (see moon_relief_build / moon_relief_row), packed into ONE
 * MOON_DEM_W x MOON_DEM_H uint16 array (glon = longitude/east low byte,
 * glat = latitude/north high byte), valid until moon_deinit(). Built
 * once from the int8 relief DEM (MOON_DEM_W x MOON_DEM_H, same
 * equirect convention) that the partition carries: the device reads
 * the whole DEM in one esp_partition_read into a transient PSRAM
 * buffer, builds the cache, and frees it (the DEM is never kept
 * resident -- steady-state relief memory is the one 4 MB array, which
 * is what lets the cache fit the 8 MB pool alongside albedo, s_frame,
 * s_rgb and LVGL). Host: the generated C arrays (moon_assets_gen.c).
 */
#ifndef MOON_ASSETS_H
#define MOON_ASSETS_H

#include "moon.h"
#include <stdint.h>

const uint8_t *moon_albedo(void);

/* The relief gradient cache (mountain shadows at the terminator): ONE
 * MOON_DEM_W x MOON_DEM_H uint16 array (glon low byte / glat high
 * byte). NULL if the partition is missing / too small, or the PSRAM
 * allocation fails. */
const moon_grel_t *moon_relief(void);

/* Device only: release the buffers. Host: no-op. */
void moon_deinit(void);

#endif /* MOON_ASSETS_H */
