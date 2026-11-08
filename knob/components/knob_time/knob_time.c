/**
 * @file knob_time.c
 * @brief K7d time truth: the legacy esp_sntp POLL client (same API the
 *        clock's clock_sntp.c drives) with the clock's trust discipline.
 *        The board has no RTC, so every deep-sleep wake reboots without
 *        a clock: an NVS snapshot of the last plausible SNTP answer
 *        (the clock's clock_sntp.c snapshot discipline) lets the boot
 *        arm show the current phase at once instead of waiting out the
 *        Wi-Fi join + NTP window dark -- the fix for the dial-wake that
 *        showed nothing but "no time" for half a minute. The moon view
 *        still reads UTC only; the caption formats local time itself.
 */
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "knob_time.h"
#include "nvs.h"

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

/* ---- NVS snapshot (the clock's wake-without-RTC discipline) ---------- */

#define NVS_NAMESPACE "knob_time"
#define NVS_KEY_EPOCH "epoch"

static nvs_handle_t s_nvs;
static bool s_nvs_opened;

/* Always READWRITE: the boot read (snapshot_epoch) happens before the
 * SNTP callback's store on every wake, and a handle opened READONLY
 * there would make the first store fail silently. */
static bool snapshot_open(void) {
    if (s_nvs_opened) {
        return true;
    }
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs) != ESP_OK) {
        ESP_LOGW(TAG, "NVS snapshot unavailable (open failed)");
        return false;
    }
    s_nvs_opened = true;
    return true;
}

static void snapshot_store(int64_t epoch) {
    if (!snapshot_open()) {
        return;
    }
    if (nvs_set_i64(s_nvs, NVS_KEY_EPOCH, epoch) == ESP_OK) {
        nvs_commit(s_nvs);
    }
}

void knob_time_snapshot_save(void) {
    if (s_valid) {
        snapshot_store(knob_time_now_epoch());
    }
}

int64_t knob_time_snapshot_epoch(void) {
    if (!snapshot_open()) {
        return 0;
    }
    int64_t epoch = 0;
    if (nvs_get_i64(s_nvs, NVS_KEY_EPOCH, &epoch) != ESP_OK) {
        return 0;
    }
    /* Same plausibility floor as a live SNTP answer: a stale snapshot
     * from a reflashed board must never arm a silently-wrong moon. Its
     * AGE is unverifiable without a clock (chicken-and-egg), so the arm
     * log says "last known" out loud and the POLL client's next fix
     * re-bakes via the show_moon force-rebake path. */
    return epoch >= PLAUSIBLE_EPOCH ? epoch : 0;
}

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
    /* Persist immediately (small write, rare: first boot + hourly POLL):
     * the deep-sleep window is exactly when the power supervisor has no
     * time to spare, and an answer this fresh is what a wake arms on. */
    snapshot_store((int64_t)tv->tv_sec);
    ESP_LOGI(TAG, "SNTP synced: epoch %lld (snapshot saved)", (long long)tv->tv_sec);
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
