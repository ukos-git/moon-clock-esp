/* Device asset backend: equirect albedo + relief gradient cache.
 *
 * The combined image (assets/images/moon/moon_assets.bin) is
 *   [0 .. ALB)                     albedo, MOON_ALB_W x MOON_ALB_H u8
 *   [ALB .. ALB + DEM)             relief DEM, MOON_DEM_W x MOON_DEM_H i8
 * and lives on the moon_assets data partition (subtype "undefined").
 *
 * The albedo is copied to octal PSRAM once (NOT mmapped: the S3 DROM
 * requires 4-byte-aligned access, while the bake does per-pixel byte
 * samples). The relief DEM is the one-time INPUT to the relief gradient
 * cache: moon_relief() reads the whole 2 MB DEM in a single esp_partition_read
 * into a transient PSRAM buffer, builds the two 2 MB PSRAM planes
 * (glon/glat) with moon_relief_build (the same builder the host gate uses),
 * and frees the DEM. Steady-state relief memory is the two 2 MB planes only.
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

static const char *TAG = "moon";

static const esp_partition_t *part;
static uint8_t *psram_alb;
static int8_t *psram_glon; /* relief cache: longitude/east slope */
static int8_t *psram_glat; /* relief cache: latitude/north slope */
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

/* Build the two-plane relief cache from a whole-DEM read. The 2 MB DEM
 * is read in ONE esp_partition_read into a transient PSRAM buffer
 * (fast: one flash transaction, vs ~4 s for 1024 row reads), passed to
 * moon_relief_build (the same whole-array builder the host gate uses),
 * then freed. The DEM is never kept: steady-state relief memory is the
 * two 2 MB planes alone. The transient DEM coexists with the planes only
 * during the build; the caller allocates the planes FIRST so the tightest
 * point (all three live) still leaves the largest block the allocator
 * needs to place the final 2 MB. */
static void build_relief_whole(int8_t *dem_tmp) {
    const int W = MOON_DEM_W, H = MOON_DEM_H;
    const size_t dem_sz = (size_t)W * H;
    const size_t dem_off = (size_t)MOON_ALB_W * MOON_ALB_H; /* DEM starts after the albedo */
    int64_t t0 = esp_timer_get_time();
    if (esp_partition_read(part, dem_off, dem_tmp, dem_sz) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (DEM, %u bytes)", (unsigned)dem_sz);
        return;
    }
    moon_relief_build(dem_tmp, W, H, psram_glon, psram_glat);
    ESP_LOGI(TAG, "relief gradient cache built (%d KB glon + %d KB glat) in %lld ms", W * H / 1024, W * H / 1024,
             (long long)(esp_timer_get_time() - t0) / 1000);
}

const moon_grel_t *moon_relief(void) {
    if (!loaded && !load_albedo())
        return NULL;
    if (!relief_built) {
        size_t plane = (size_t)MOON_DEM_W * MOON_DEM_H; /* 2 MB per plane */
        const int64_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "building relief cache: PSRAM free %d KB (need 2 x %d KB planes + %d KB transient DEM)",
                 (int)(psram_free / 1024), (int)(plane / 1024), (int)(plane / 1024));
        /* Planes first (persistent, max free space), then the transient
         * DEM buffer. Each 2 MB block is placed with >= ~3 MB free, so the
         * largest-block requirement is met even before the DEM is freed. */
        psram_glon = (int8_t *)heap_caps_malloc(plane, MALLOC_CAP_SPIRAM);
        psram_glat = (int8_t *)heap_caps_malloc(plane, MALLOC_CAP_SPIRAM);
        int8_t *dem_tmp = (int8_t *)heap_caps_malloc(plane, MALLOC_CAP_SPIRAM);
        if (!psram_glon || !psram_glat || !dem_tmp) {
            ESP_LOGE(TAG, "PSRAM allocation failed (relief 2 x %u KB planes + %u KB DEM); free %d KB",
                     (unsigned)(plane / 1024), (unsigned)(plane / 1024),
                     (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            free(psram_glon);
            free(psram_glat);
            free(dem_tmp);
            psram_glon = NULL;
            psram_glat = NULL;
            return NULL;
        }
        build_relief_whole(dem_tmp);
        free(dem_tmp); /* transient: the DEM is never kept */
        relief_built = true;
        s_grel.glon = psram_glon;
        s_grel.glat = psram_glat;
    }
    return &s_grel;
}

void moon_deinit(void) {
    if (psram_alb || psram_glon || psram_glat) {
        free(psram_alb);
        free(psram_glon);
        free(psram_glat);
        psram_alb = NULL;
        psram_glon = NULL;
        psram_glat = NULL;
        loaded = false;
        relief_built = false;
    }
}
