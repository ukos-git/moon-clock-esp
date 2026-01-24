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
#include "wifi.h"

static const char *TAG = "clock_sntp";

#define SNTP_SYNC_BIT BIT0
static EventGroupHandle_t s_sntp_event_group = NULL;
static bool s_sntp_initialized = false;
static time_t s_last_sync_time = 0;

// Callback function called when SNTP sync is complete
static void sntp_sync_time_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized");
    s_last_sync_time = tv->tv_sec;
    if (s_sntp_event_group) {
        xEventGroupSetBits(s_sntp_event_group, SNTP_SYNC_BIT);
    }
}

esp_err_t clock_sntp_init(void) {
    if (s_sntp_initialized) {
        ESP_LOGW(TAG, "SNTP already initialized");
        return ESP_OK;
    }

    // Set timezone using Kconfig value
    setenv("TZ", CONFIG_CLOCK_DEFAULT_TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", CONFIG_CLOCK_DEFAULT_TIMEZONE);

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

    ESP_LOGI(TAG, "SNTP component deinitialized");
    return ESP_OK;
}
