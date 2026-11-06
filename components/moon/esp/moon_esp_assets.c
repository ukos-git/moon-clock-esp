/* Device asset backend: equirect albedo + relief gradient cache.
 *
 * The combined image (assets/images/moon/out/<target>/moon_assets.bin,
 * per-board -- the root CMakeLists picks out/s3 or out/p4 by IDF_TARGET)
 * is
 *   [0 .. ALB)                     albedo, MOON_ALB_W x MOON_ALB_H u8
 *   [ALB .. ALB + DEM)             relief DEM, MOON_DEM_W x MOON_DEM_H i8
 * and lives on the moon_assets data partition (subtype "undefined").
 *
 * The albedo is copied to octal PSRAM once (NOT mmapped: the S3 DROM
 * requires 4-byte-aligned access, while the bake does per-pixel byte
 * samples). The relief DEM is the one-time INPUT to the relief gradient
 * cache: moon_relief() reads the whole 2 MB DEM in a single esp_partition_read
 * into a transient PSRAM buffer, builds the ONE 4 MB packed PSRAM array
 * (glon low byte / glat high byte per cell) with moon_relief_build (the
 * same builder the host gate uses), and frees the DEM. Steady-state
 * relief memory is that one 4 MB array only.
 * The build runs eagerly on the main task (moon_ui_init), before any
 * orbit/sweep bake task exists, so there is no concurrent-build race.
 */
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

/* The asset blob and the bake assume an equirect 2:1 albedo (the generator
 * enforces this; a hand-edited header must not break it silently). */
#if MOON_ALB_W % 2 || MOON_ALB_H * 2 != MOON_ALB_W
#error                                                                                                                 \
    "moon_consts.h: albedo must be equirect 2:1 (even MOON_ALB_W, MOON_ALB_H == MOON_ALB_W / 2) -- regenerate the assets (python -m moon.assets)"
#endif

static const char *TAG = "moon";

static const esp_partition_t *part;
static uint8_t *psram_alb;
static uint16_t *psram_gpack; /* relief cache: packed glon (low) / glat (high) */
static moon_grel_t s_grel;
static bool loaded;
static bool relief_built;

/* Copy the albedo to PSRAM once. */
static bool load_albedo(void) {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "moon_assets");
    if (!part) {
        ESP_LOGE(TAG, "moon_assets partition not found");
        return false;
    }
    size_t need_a = (size_t)MOON_ALB_W * MOON_ALB_H;
    size_t need_d = (size_t)MOON_DEM_W * MOON_DEM_H;
    if (part->size < need_a + need_d) {
        ESP_LOGE(TAG, "moon_assets partition too small: %u < %u (albedo + DEM)", (unsigned)part->size,
                 (unsigned)(need_a + need_d));
        return false;
    }
    psram_alb = heap_caps_malloc(need_a, MALLOC_CAP_SPIRAM);
    if (!psram_alb) {
        ESP_LOGE(TAG, "PSRAM allocation failed (albedo %u bytes)", (unsigned)need_a);
        psram_alb = NULL;
        return false;
    }
    if (esp_partition_read(part, 0, psram_alb, need_a) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (albedo)");
        free(psram_alb);
        psram_alb = NULL;
        return false;
    }
    loaded = true;
    ESP_LOGI(TAG, "albedo %dx%d loaded to PSRAM from moon_assets (%u bytes)", MOON_ALB_W, MOON_ALB_H, (unsigned)need_a);
    return true;
}

const uint8_t *moon_albedo(void) {
    if (!loaded && !load_albedo())
        return NULL;
    return psram_alb;
}

/* Build the packed relief cache from a whole-DEM read. The 2 MB DEM
 * is read in ONE esp_partition_read into a transient PSRAM buffer
 * (fast: one flash transaction, vs ~4 s for 1024 row reads), passed to
 * moon_relief_build (the same whole-array builder the host gate uses),
 * then freed. The DEM is never kept: steady-state relief memory is the
 * one 4 MB packed array alone. The transient DEM coexists with the
 * array only during the build; the caller allocates the array FIRST so
 * the tightest point (both live) still leaves the largest block the
 * allocator needs to place it. */
static void build_relief_whole(int8_t *dem_tmp) {
    const int W = MOON_DEM_W, H = MOON_DEM_H;
    const size_t dem_sz = (size_t)W * H;
    const size_t dem_off = (size_t)MOON_ALB_W * MOON_ALB_H; /* DEM starts after the albedo */
    int64_t t0 = esp_timer_get_time();
    if (esp_partition_read(part, dem_off, dem_tmp, dem_sz) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (DEM, %u bytes)", (unsigned)dem_sz);
        return;
    }
    moon_relief_build(dem_tmp, W, H, psram_gpack);
    ESP_LOGI(TAG, "relief gradient cache built (%d KB packed) in %lld ms", (W * H * (int)sizeof(uint16_t)) / 1024,
             (long long)(esp_timer_get_time() - t0) / 1000);
}

const moon_grel_t *moon_relief(void) {
    if (!loaded && !load_albedo())
        return NULL;
    if (!relief_built) {
        size_t cells = (size_t)MOON_DEM_W * MOON_DEM_H;
        size_t pack_sz = cells * sizeof(uint16_t); /* 4 MB packed array */
        const int64_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "building relief cache: PSRAM free %d KB (need %d KB packed array + %d KB transient DEM)",
                 (int)(psram_free / 1024), (int)(pack_sz / 1024), (int)(cells / 1024));
        /* Packed array first (persistent, max free space), then the
         * transient DEM buffer. The 4 MB block is placed with ~3.8 MB
         * free, so the largest-block requirement is met even before
         * the DEM is freed. */
        psram_gpack = (uint16_t *)heap_caps_malloc(pack_sz, MALLOC_CAP_SPIRAM);
        int8_t *dem_tmp = (int8_t *)heap_caps_malloc(cells, MALLOC_CAP_SPIRAM);
        if (!psram_gpack || !dem_tmp) {
            ESP_LOGE(TAG, "PSRAM allocation failed (relief %u KB packed array + %u KB DEM); free %d KB",
                     (unsigned)(pack_sz / 1024), (unsigned)(cells / 1024),
                     (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            free(psram_gpack);
            free(dem_tmp);
            psram_gpack = NULL;
            return NULL;
        }
        build_relief_whole(dem_tmp);
        free(dem_tmp); /* transient: the DEM is never kept */
        relief_built = true;
        s_grel.pack = psram_gpack;
    }
    return &s_grel;
}

void moon_deinit(void) {
    if (psram_alb || psram_gpack) {
        free(psram_alb);
        free(psram_gpack);
        psram_alb = NULL;
        psram_gpack = NULL;
        loaded = false;
        relief_built = false;
    }
}
