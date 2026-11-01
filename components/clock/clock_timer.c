#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "clock.h"
#include "clock_timer.h"
#include "pcf85063a.h"

static const char *TAG = "clock_timer";

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern bool s_initialized;

// Track current countdown timer configuration
static uint8_t s_countdown_timer_value = 0;
static bool s_countdown_timer_active = false;
static int64_t s_countdown_enabled_at_us = 0; /* host time the active countdown started */

// P4 (no PCF85063A): the countdown is an esp_timer one-shot. The one-shot
// fires in the esp_timer task context, where it may NOT run the UI callback
// (the S3 runs it from the LVGL task via the interrupt poll); it only sets
// this flag, which clock_interrupt.c's poll picks up and acts on -- the
// same context split as the S3's TF flag.
#if !CONFIG_CLOCK_USE_PCF85063A
static esp_timer_handle_t s_countdown_esp_timer = NULL;
static volatile bool s_countdown_expired_flag = false;

static void countdown_esp_timer_cb(void *arg) {
    (void)arg;
    s_countdown_expired_flag = true;
}
#endif

esp_err_t clock_timer_enable(uint8_t timer_seconds, uint8_t timer_mode) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_CLOCK_USE_PCF85063A
    if (timer_seconds == 0) {
        // Disable timer
        esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Timer disabled");
            s_countdown_timer_active = false;
            s_countdown_timer_value = 0;
            s_countdown_enabled_at_us = 0;
        }
        return ret;
    }

    ESP_LOGI(TAG, "Enabling timer (%d seconds, mode 0x%02x)", timer_seconds, timer_mode);

    // Reset the RTC timer state
    esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure timer with specified settings
    ret = pcf85063a_set_timer_safe(&s_rtc_dev, timer_seconds, timer_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Timer enabled: %d seconds", timer_seconds);

    // Track timer configuration for expiration detection. The host-side
    // start timestamp is the expiration oracle: the PCF85063A's TF flag is
    // SHARED with the minute interrupt and the hardware timer auto-reloads
    // (re-setting TF every period), so the register value alone cannot tell
    // "just expired" from "just started a while ago" (see
    // clock_interrupt.c -- the old value-based heuristic misfired on the
    // first poll after a start).
    s_countdown_timer_value = timer_seconds;
    s_countdown_timer_active = true;
    s_countdown_enabled_at_us = esp_timer_get_time();

    // Verify timer was set correctly
    uint8_t verify_val, verify_mode;
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_VAL, &verify_val, 1);
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_MODE, &verify_mode, 1);
    ESP_LOGI(TAG, "Timer verification - Value: %d, Mode: 0x%02x", verify_val, verify_mode);

    return ESP_OK;
#else
    (void)timer_mode;

    if (timer_seconds == 0) {
        // Disable the one-shot. esp_timer_stop() is idempotent; clearing
        // the flag here also cancels a one-shot that already fired but the
        // poller has not consumed yet (wake during the countdown).
        if (s_countdown_esp_timer) {
            esp_timer_stop(s_countdown_esp_timer);
        }
        s_countdown_expired_flag = false;
        s_countdown_timer_active = false;
        s_countdown_timer_value = 0;
        s_countdown_enabled_at_us = 0;
        ESP_LOGI(TAG, "Countdown disabled (software)");
        return ESP_OK;
    }

    if (!s_countdown_esp_timer) {
        const esp_timer_create_args_t args = {
            .callback = countdown_esp_timer_cb,
            .name = "clock_countdown",
        };
        esp_err_t ret = esp_timer_create(&args, &s_countdown_esp_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create countdown esp_timer: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // (Re)arm: stop any previous one-shot, reset the flag, start fresh.
    esp_timer_stop(s_countdown_esp_timer);
    s_countdown_expired_flag = false;
    esp_err_t ret = esp_timer_start_once(s_countdown_esp_timer, (int64_t)timer_seconds * 1000000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start countdown esp_timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Countdown armed (software): %d seconds", timer_seconds);
    s_countdown_timer_value = timer_seconds;
    s_countdown_timer_active = true;
    s_countdown_enabled_at_us = esp_timer_get_time();

    return ESP_OK;
#endif
}

esp_err_t clock_timer_disable(void) { return clock_timer_enable(0, 0); }

bool clock_timer_is_active(void) { return s_countdown_timer_active; }

uint8_t clock_timer_get_value(void) { return s_countdown_timer_value; }

int64_t clock_timer_get_enabled_at_us(void) { return s_countdown_enabled_at_us; }

esp_err_t clock_timer_process_interrupt(bool *is_timer_tick) {
    if (!is_timer_tick || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_CLOCK_USE_PCF85063A
    *is_timer_tick = false;

    // Check for timer flag
    uint8_t timer_flag = 0;
    esp_err_t ret = pcf85063a_get_timer_flag(&s_rtc_dev, &timer_flag);
    if (ret != ESP_OK || !timer_flag) {
        // Not a timer interrupt or error reading flag
        return ret;
    }

    // Clear the timer flag
    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Valid timer tick
    *is_timer_tick = true;

    return ESP_OK;
#else
    // No PCF TF flag: the software countdown's expiration is the flag set
    // by the one-shot (see clock_timer_take_expired).
    *is_timer_tick = false;
    return ESP_OK;
#endif
}

bool clock_timer_take_expired(void) {
#if CONFIG_CLOCK_USE_PCF85063A
    // The S3's poller judges expiration from the TF flag + the host-side
    // start timestamp; this flag is unused there.
    return false;
#else
    // Consume the one-shot's expiration flag (set from the esp_timer task).
    bool expired = s_countdown_expired_flag;
    s_countdown_expired_flag = false;
    return expired;
#endif
}
