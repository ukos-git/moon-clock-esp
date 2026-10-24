/* Device asset backend: equirect albedo from the moon_assets data
 * partition (subtype "undefined": a plain raw binary region).
 *
 * The 512KB albedo is copied to octal PSRAM once at first use. It is NOT
 * mmapped: the S3 DROM (mmap) requires 4-byte-aligned access, while the
 * bake does per-pixel byte samples (bilinear = 4 reads/px). The one-time
 * copy is ~13ms at 2MB/s flash reads; afterwards the bake reads plain
 * PSRAM.
 */
#include "moon_assets.h"
#include "moon_consts.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "moon";

static const esp_partition_t *part;
static uint8_t *psram;
static bool loaded;

const uint8_t *moon_albedo(void) {
    if (!loaded) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "moon_assets");
        if (!part) {
            ESP_LOGE(TAG, "moon_assets partition not found");
            return NULL;
        }
        size_t need = (size_t)MOON_ALB_W * MOON_ALB_H;
        if (part->size < need) {
            ESP_LOGE(TAG, "moon_assets partition too small: %u < %u", (unsigned)part->size, (unsigned)need);
            return NULL;
        }
        psram = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
        if (!psram) {
            ESP_LOGE(TAG, "PSRAM allocation failed for albedo (%u bytes)", (unsigned)need);
            return NULL;
        }
        if (esp_partition_read(part, 0, psram, need) != ESP_OK) {
            ESP_LOGE(TAG, "esp_partition_read failed");
            free(psram);
            psram = NULL;
            return NULL;
        }
        loaded = true;
        ESP_LOGI(TAG, "albedo %dx%d loaded to PSRAM from moon_assets (%u bytes)", MOON_ALB_W, MOON_ALB_H,
                 (unsigned)need);
    }
    return psram;
}

void moon_deinit(void) {
    if (psram) {
        free(psram);
        psram = NULL;
        loaded = false;
    }
}
