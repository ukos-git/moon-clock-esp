/**
 * @file clock_sntp.c
 * @brief SNTP/NTP time synchronization for clock component
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "clock_sntp.h"
#include "clock_tz.h"
#include "settings_store.h"
#include "wifi.h"

static const char *TAG = "clock_sntp";

#define SNTP_SYNC_BIT BIT0
static EventGroupHandle_t s_sntp_event_group = NULL;
static bool s_sntp_initialized = false;
static time_t s_last_sync_time = 0;

// Trust flag: the wall clock holds SNTP-verified (or manually entered)
// time. The S3's time base is the PCF85063A, which is self-trusting
// (the flag is unused there); on the P4 it gates the software alarm fire
// so the default/unsynced clock (2026-01-24 12:00) can never ring a
// phantom alarm, and it drives the UI's "unsynced" state.
static bool s_time_trusted = false;

// P4 (no PCF85063A): NVS epoch backup. A FULL POWER LOSS kills the crystal
// RTC domain (VBAT coverage is unverified on the 4C), and the crystal only
// keeps counting through the app-level sleep states (no esp_deep_sleep) --
// so the NVS snapshot is the only cross-power-loss memory of the wall
// clock. Saved on every SNTP success and every ~15 min (the minute tick);
// restored at boot in place of the 2026-01-24 default (and only then --
// the time is still NOT trusted: the flag stays down until the next SNTP
// sync or a manual entry, so the clock shows dim and no alarm can fire).
#if !CONFIG_CLOCK_USE_PCF85063A
#include "nvs.h"

#define CLOCK_NVS_NAMESPACE "clock"
#define CLOCK_NVS_KEY_EPOCH "epoch"
// 15 min: tight enough that a power loss loses little, sparse enough
// that the NVS wear is negligible (one commit per quarter hour).
#define CLOCK_NVS_SAVE_INTERVAL_SEC 900

static time_t s_last_nvs_save = 0;

esp_err_t clock_nvs_save_epoch(void) {
    nvs_handle_t h;
    if (nvs_open(CLOCK_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for the epoch backup");
        return ESP_FAIL;
    }
    esp_err_t ret = nvs_set_i64(h, CLOCK_NVS_KEY_EPOCH, (int64_t)time(NULL));
    if (ret == ESP_OK) {
        ret = nvs_commit(h);
    }
    nvs_close(h);
    if (ret == ESP_OK) {
        s_last_nvs_save = time(NULL);
    }
    return ret;
}

// The minute tick's periodic save: only write when the interval has
// elapsed (a no-op ESP_OK otherwise, so the caller needs no bookkeeping).
esp_err_t clock_nvs_maybe_save_epoch(void) {
    if (s_last_nvs_save && (time(NULL) - s_last_nvs_save) < CLOCK_NVS_SAVE_INTERVAL_SEC) {
        return ESP_OK;
    }
    esp_err_t ret = clock_nvs_save_epoch();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Periodic NVS epoch save failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// Restore the last snapshot into the system clock. Returns true (and the
// restored time is live) when a plausible snapshot was applied.
bool clock_nvs_restore_epoch(void) {
    nvs_handle_t h;
    if (nvs_open(CLOCK_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    int64_t saved = 0;
    bool found = (nvs_get_i64(h, CLOCK_NVS_KEY_EPOCH, &saved) == ESP_OK);
    nvs_close(h);
    if (!found || saved < 1609459200) { // pre-2021-01-01 snapshots are junk
        return false;
    }
    struct timeval tv = {.tv_sec = (time_t)saved, .tv_usec = 0};
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }
    ESP_LOGI(TAG, "Restored wall clock from NVS backup (unsynced until NTP): %lld", (long long)saved);
    return true;
}
#endif

// Callback function called when SNTP sync is complete
static void sntp_sync_time_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized");
    s_last_sync_time = tv->tv_sec;
    s_time_trusted = true;
#if !CONFIG_CLOCK_USE_PCF85063A
    // The just-synced epoch is the most trusted value we hold: snapshot
    // it (force, ignoring the 15 min interval).
    if (clock_nvs_save_epoch() != ESP_OK) {
        ESP_LOGW(TAG, "NVS epoch save after SNTP sync failed");
    }
#endif
    if (s_sntp_event_group) {
        xEventGroupSetBits(s_sntp_event_group, SNTP_SYNC_BIT);
    }
}

esp_err_t clock_sntp_init(void) {
    if (s_sntp_initialized) {
        ESP_LOGW(TAG, "SNTP already initialized");
        return ESP_OK;
    }

    // Set the timezone from the runtime selection (settings store; loaded
    // before clock_init in main). The default (no settings file yet) is the
    // Europe/Berlin entry = the Kconfig value, so a fresh boot is unchanged.
    const char *tz = clock_tz_string_for_index(settings_timezone_idx());
    setenv("TZ", tz ? tz : CONFIG_CLOCK_DEFAULT_TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s (idx %d)", tz ? tz : CONFIG_CLOCK_DEFAULT_TIMEZONE, settings_timezone_idx());

    // Create event group for synchronization
    s_sntp_event_group = xEventGroupCreate();
    if (!s_sntp_event_group) {
        ESP_LOGE(TAG, "Failed to create SNTP event group");
        return ESP_ERR_NO_MEM;
    }

    s_sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP component initialized");
    return ESP_OK;
}

esp_err_t clock_sntp_sync(uint32_t timeout_ms) {
    if (!s_sntp_initialized) {
        ESP_LOGE(TAG, "SNTP not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if WiFi is connected
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot sync time via NTP");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting SNTP time synchronization...");

    // Clear any previous sync bit
    if (s_sntp_event_group) {
        xEventGroupClearBits(s_sntp_event_group, SNTP_SYNC_BIT);
    }

    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_CLOCK_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_time_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "Waiting for SNTP synchronization (timeout: %lu ms)...", timeout_ms);

    // Wait for synchronization with timeout
    EventBits_t bits =
        xEventGroupWaitBits(s_sntp_event_group, SNTP_SYNC_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    esp_sntp_stop();

    if (bits & SNTP_SYNC_BIT) {
        ESP_LOGI(TAG, "SNTP synchronization successful");

        // Log the synchronized time
        time_t now;
        struct tm timeinfo;
        char strftime_buf[64];

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Current local time: %s", strftime_buf);

        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "SNTP synchronization timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t clock_sntp_get_system_time(clock_time_t *time_out) {
    if (!time_out) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now;
    struct tm timeinfo;

    time(&now);

    // P4: before falling back to the 2026-01-24 default, try the NVS
    // snapshot (a full power loss reset the crystal domain to its boot
    // default; the snapshot keeps the last known time). Still unsynced:
    // the trust flag is untouched either way.
#if !CONFIG_CLOCK_USE_PCF85063A
    if (now < 1609459200) { // Jan 1, 2021 timestamp
        if (clock_nvs_restore_epoch()) {
            time(&now);
        }
    }
#endif

    // If system time is invalid (epoch time), set a reasonable default
    if (now < 1609459200) { // Jan 1, 2021 timestamp
        ESP_LOGW(TAG, "System time appears invalid, setting default time");

        // Set to January 24, 2026 12:00:00 CET
        timeinfo.tm_year = 2026 - 1900;
        timeinfo.tm_mon = 0; // January (0-based)
        timeinfo.tm_mday = 24;
        timeinfo.tm_hour = 12;
        timeinfo.tm_min = 0;
        timeinfo.tm_sec = 0;
        timeinfo.tm_isdst = -1;

        now = mktime(&timeinfo);
        struct timeval tv = {.tv_sec = now, .tv_usec = 0};
        settimeofday(&tv, NULL);

        ESP_LOGI(TAG, "Set default system time to 2026-01-24 12:00:00");
    }

    localtime_r(&now, &timeinfo);

    // Convert to our clock format
    time_out->year = timeinfo.tm_year + 1900;
    time_out->month = timeinfo.tm_mon + 1;
    time_out->day = timeinfo.tm_mday;
    time_out->hour = timeinfo.tm_hour;
    time_out->minute = timeinfo.tm_min;
    time_out->second = timeinfo.tm_sec;

    return ESP_OK;
}

bool clock_sntp_needs_sync(void) {
    if (!s_sntp_initialized || s_last_sync_time == 0) {
        return true;
    }

    time_t now;
    time(&now);

    // Check if sync interval has elapsed
    uint32_t sync_interval_sec = CONFIG_CLOCK_NTP_SYNC_INTERVAL_HOURS * 3600;
    return (now - s_last_sync_time) > sync_interval_sec;
}

time_t clock_sntp_get_last_sync_time(void) { return s_last_sync_time; }

bool clock_sntp_time_trusted(void) { return s_time_trusted; }

// Set by the manual time-entry screen (P4) when the user commits a time
// with no network available.
void clock_sntp_set_time_trusted(bool trusted) { s_time_trusted = trusted; }

esp_err_t clock_sntp_deinit(void) {
    if (!s_sntp_initialized) {
        return ESP_OK;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    if (s_sntp_event_group) {
        vEventGroupDelete(s_sntp_event_group);
        s_sntp_event_group = NULL;
    }

    s_sntp_initialized = false;
    s_last_sync_time = 0;
    s_time_trusted = false;

    ESP_LOGI(TAG, "SNTP component deinitialized");
    return ESP_OK;
}
