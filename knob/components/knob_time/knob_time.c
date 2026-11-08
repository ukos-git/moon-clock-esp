/**
 * @file knob_time.c
 * @brief K7d time truth: the legacy esp_sntp POLL client (same API the
 *        clock's clock_sntp.c drives) with the clock's trust discipline
 *        and nothing else -- no RTC, no NVS snapshot, no timezone. The
 *        moon view reads UTC only.
 */
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "knob_time.h"

static const char *TAG = "knob_time";

#define SYNCED_BIT BIT0

/* Plausibility floor (the clock's NVS-snapshot guard, same number): an
 * NTP answer from a dead/reflected server can carry an era wrap or a
 * 2000-era default. Baking a "current phase" from such a moment is the
 * silently-wrong moon this module exists to prevent, so it never marks
 * the clock valid; the POLL client keeps trying on its own cadence. */
#define PLAUSIBLE_EPOCH 1609459200 /* 2021-01-01 */

/* #ifndef-seeded (repo invariant: Kconfig 'n' leaves the macro
 * UNDEFINED; this seeds the Kconfig default, never redefines it). */
#ifndef CONFIG_KNOB_NTP_SERVER
#define CONFIG_KNOB_NTP_SERVER "pool.ntp.org"
#endif

static EventGroupHandle_t s_sync_evt;
static bool s_started;
static volatile bool s_valid;

/* Runs on the lwIP SNTP task right after it settimeofday()'d. Setting a
 * flag + event bit is all it may do -- esp_sntp_stop() from in here
 * would wait for this very task to die. */
static void sntp_sync_cb(struct timeval *tv) {
    if (tv == NULL || (int64_t)tv->tv_sec < PLAUSIBLE_EPOCH) {
        ESP_LOGW(TAG, "SNTP answer implausible (%lld) -- staying invalid", tv ? (long long)tv->tv_sec : 0LL);
        return;
    }
    s_valid = true;
    if (s_sync_evt) {
        xEventGroupSetBits(s_sync_evt, SYNCED_BIT);
    }
    ESP_LOGI(TAG, "SNTP synced: epoch %lld", (long long)tv->tv_sec);
}

void knob_time_start_sync(void) {
    if (s_started) {
        return;
    }
    if (!s_sync_evt) {
        s_sync_evt = xEventGroupCreate();
        if (!s_sync_evt) {
            ESP_LOGE(TAG, "no memory for the sync event group");
            return;
        }
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_KNOB_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();
    s_started = true;
    ESP_LOGI(TAG, "SNTP started (POLL, server '%s')", CONFIG_KNOB_NTP_SERVER);
}

bool knob_time_valid(void) { return s_valid; }

int64_t knob_time_now_epoch(void) {
    if (!s_valid) {
        return 0;
    }
    return (int64_t)time(NULL);
}

bool knob_time_wait_synced(uint32_t timeout_ms) {
    if (s_valid) {
        return true;
    }
    if (!s_sync_evt) {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms > 200 ? 200 : timeout_ms));
        return s_valid;
    }
    EventBits_t bits = xEventGroupWaitBits(s_sync_evt, SYNCED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & SYNCED_BIT) != 0;
}
