#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_timer.h"
#include "pcf85063a.h"

static const char *TAG = "clock_timer";

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern bool s_initialized;

esp_err_t clock_timer_enable(uint8_t timer_seconds, uint8_t timer_mode) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timer_seconds == 0) {
        // Disable timer
        esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Timer disabled");
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

    // Verify timer was set correctly
    uint8_t verify_val, verify_mode;
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_VAL, &verify_val, 1);
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_MODE, &verify_mode, 1);
    ESP_LOGI(TAG, "Timer verification - Value: %d, Mode: 0x%02x", verify_val, verify_mode);

    return ESP_OK;
}

esp_err_t clock_timer_disable(void) { return clock_timer_enable(0, 0); }

esp_err_t clock_timer_process_interrupt(bool *is_timer_tick) {
    if (!is_timer_tick || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

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
}
