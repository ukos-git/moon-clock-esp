/* Device asset backend: equirect albedo + relief DEM from the moon_assets
 * data partition (subtype "undefined": a plain raw binary region).
 *
 * The combined image (assets/images/moon/moon_assets.bin) is
 *   [0 .. ALB)                     albedo, MOON_ALB_W x MOON_ALB_H u8
 *   [ALB .. ALB + DEM)             relief DEM, MOON_DEM_W x MOON_DEM_H i8
 * and is copied to octal PSRAM once at first use. It is NOT mmapped: the
 * S3 DROM (mmap) requires 4-byte-aligned access, while the bake does
 * per-pixel byte samples. The one-time copy is ~250ms for ~500KB at
 * 2MB/s flash reads; afterwards the bake reads plain PSRAM.
 */
#include "moon_assets.h"
#include "moon_consts.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "moon";

static const esp_partition_t *part;
static uint8_t *psram_alb;
static int8_t *psram_dem;
static bool loaded;

/* Copy the partition (albedo @0, DEM after it) to PSRAM once. */
static bool load_assets(void) {
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
    psram_dem = (int8_t *)heap_caps_malloc(need_d, MALLOC_CAP_SPIRAM);
    if (!psram_alb || !psram_dem) {
        ESP_LOGE(TAG, "PSRAM allocation failed (albedo %u + DEM %u bytes)", (unsigned)need_a, (unsigned)need_d);
        free(psram_alb);
        free(psram_dem);
        psram_alb = NULL;
        psram_dem = NULL;
        return false;
    }
    if (esp_partition_read(part, 0, psram_alb, need_a) != ESP_OK ||
        esp_partition_read(part, need_a, psram_dem, need_d) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed");
        free(psram_alb);
        free(psram_dem);
        psram_alb = NULL;
        psram_dem = NULL;
        return false;
    }
    loaded = true;
    ESP_LOGI(TAG, "albedo %dx%d + DEM %dx%d loaded to PSRAM from moon_assets (%u bytes)", MOON_ALB_W, MOON_ALB_H,
             MOON_DEM_W, MOON_DEM_H, (unsigned)(need_a + need_d));
    return true;
}

const uint8_t *moon_albedo(void) {
    if (!loaded && !load_assets())
        return NULL;
    return psram_alb;
}

const int8_t *moon_dem(void) {
    if (!loaded && !load_assets())
        return NULL;
    return psram_dem;
}

void moon_deinit(void) {
    if (psram_alb || psram_dem) {
        free(psram_alb);
        free(psram_dem);
        psram_alb = NULL;
        psram_dem = NULL;
        loaded = false;
    }
}
