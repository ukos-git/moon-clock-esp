/* Boot-movie seam: the precomputed synodic cycle baked into flash at
 * build time and played (looping) during boot while the live phase bakes.
 *
 * The container (assets/images/moon/moon/movie.py writes it, flashed into
 * the moon_movie data partition by the root CMakeLists): a 62-byte
 * little-endian header, then a frame_count x u32 offset table (offsets
 * RELATIVE to the first frame), then the q90 grayscale JPEGs packed back
 * to back. Each JPEG is a RAW bake of the portable core (moon_sim with
 * --radius/--pgm: the board's exact geometry, dark_zero, neutral LUT) --
 * pixel-identical to a device bake. Frame i's hour centre is
 * ref_center + i * hours_per_frame * 3600 (epoch seconds, UTC).
 *
 * The reader mirrors the device asset backend (moon_esp_assets.c):
 * esp_partition_find_first + esp_partition_read into PSRAM, no mmaps. The
 * decode reuses the TF hour-cache codec path (esp_new_jpeg, RGB888 out,
 * take R -- a gray JPEG decodes with R == G == B).
 */
#ifndef MOON_MOVIE_H
#define MOON_MOVIE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t frame_count;     /* frames spanning ~one synodic cycle       */
    uint32_t hours_per_frame; /* phase step, hours (1 = hourly)       */
    uint32_t s_px;            /* bake size (the board's MOON_PX)               */
    uint32_t radius;          /* disk radius px (device bake geometry)       */
    int64_t ref_center;       /* epoch s, frame 0's hour centre (UTC)      */
} moon_movie_info_t;

/* Probe the moon_movie partition and validate the header (magic +
 * calcver). Missing / zero-frame / calcver-mismatched -> false (movie
 * unavailable; the caller falls back to the card / disc path, the
 * card-less posture). info_out may be NULL. */
bool moon_movie_init(moon_movie_info_t *info_out);

/* Decode frame idx (0..frame_count-1) into gray_out (s_px x s_px u8;
 * s_px must be the info's s_px). Lazy-allocates the decode scratch
 * (s_px^2 * 3 PSRAM, once). Single-threaded caller contract (the
 * boot-replay producer) -- no locking, the same rule as tfcache. */
bool moon_movie_fetch(uint32_t idx, uint8_t *gray_out, uint32_t s_px);

#endif /* MOON_MOVIE_H */
