/**
 * @file knob_time.c
 * @brief Knob time bridge (was the K7d esp_sntp client — retired onto the
 *        clock's clock_sntp.c with the family seam, P0): what remains is
 *        the one-time migration path for the bench boards' legacy
 *        "knob_time"/"epoch" NVS snapshot into the clock's
 *        "clock"/"epoch" namespace (clock_nvs_restore_epoch), plus the
 *        bench-Kconfig home for the boot-sync window, caption TZ and the
 *        frozen test epoch.
 *
        Migration, not permanent state: once the clock's snapshot exists
 *        (rewritten on every SNTP success and before every deep sleep),
 *        this file's reader has no callers' effect — clock_nvs_restore_
 *        epoch returns true first and the legacy read never happens.
 */
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "knob_time.h"
#include "nvs.h"

static const char *TAG = "knob_time";

/* Plausibility floor (the clock's NVS-snapshot guard, same number): a
 * stale snapshot from a reflashed board must never arm a silently-wrong
 * moon. */
#define PLAUSIBLE_EPOCH 1609459200 /* 2021-01-01 */

#define LEGACY_NAMESPACE "knob_time"
#define LEGACY_KEY_EPOCH "epoch"

int64_t knob_time_legacy_snapshot_epoch(void) {
    nvs_handle_t h;
    if (nvs_open(LEGACY_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    int64_t epoch = 0;
    const esp_err_t err = nvs_get_i64(h, LEGACY_KEY_EPOCH, &epoch);
    nvs_close(h);
    if (err != ESP_OK) {
        return 0;
    }
    return epoch >= PLAUSIBLE_EPOCH ? epoch : 0;
}

bool knob_time_adopt_legacy_snapshot(void) {
    const int64_t epoch = knob_time_legacy_snapshot_epoch();
    if (epoch == 0) {
        return false;
    }
    const struct timeval tv = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }
    /* Honest log: this is the LAST KNOWN instant from the pre-seam
     * firmware — the trust flag stays down (clock_sntp decides trust),
     * and the boot arm says "last known" on glass. */
    ESP_LOGW(TAG, "adopted legacy '%s' snapshot %lld (last known, untrusted)", LEGACY_NAMESPACE, (long long)epoch);
    return true;
}
