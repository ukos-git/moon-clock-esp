#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clock.h"
#include "clock_timer.h"
#include "pcf85063a.h"

static const char *TAG = "clock_timer";

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern bool s_initialized;
extern bool s_minute_timer_synced;

esp_err_t clock_init_minute_tick(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing synchronized minute tick timer");

    // Step 2: Read the current RTC time
    pcf85063a_datetime_t now;
    esp_err_t ret = pcf85063a_get_time_date(&s_rtc_dev, &now);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC time for sync: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Current time: %02d:%02d:%02d", now.hour, now.min, now.sec);

    // Step 3: Calculate seconds until the next minute boundary
    uint8_t sync_delay;
    if (now.sec == 0) {
        sync_delay = 60;
    } else {
        sync_delay = 60 - now.sec;
    }

    ESP_LOGI(TAG, "Synchronizing to minute boundary in %d seconds", sync_delay);

    // Reset sync state for new synchronization
    s_minute_timer_synced = false;

    // Use the common timer function with calculated sync delay
    ret = clock_enable_minute_tick(sync_delay);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Synchronization timer started - first tick in %d seconds", sync_delay);
    }

    return ret;
}

esp_err_t clock_sync_minute_timer(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_minute_timer_synced) {
        // Already synchronized, nothing to do
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Switching to repeating 60-second timer after synchronization");
    s_minute_timer_synced = true;

    // Step 7: Switch to a repeating 60-second timer
    uint8_t timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ | PCF85063A_RTC_TIMER_MODE_TIE;
    esp_err_t ret = pcf85063a_set_timer_safe(&s_rtc_dev, 60, timer_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set repeating timer: %s", esp_err_to_name(ret));
        s_minute_timer_synced = false; // Reset on failure
        return ret;
    }

    ESP_LOGI(TAG, "Repeating 60-second timer enabled - synchronized to minute boundaries");
    return ESP_OK;
}

esp_err_t clock_enable_minute_tick(uint8_t timer_seconds) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timer_seconds == 0) {
        // Disable timer and reset sync state
        esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Timer disabled");
        }

        // Reset sync state when disabling
        s_minute_timer_synced = false;

        return ret;
    }

    // Enable timer with specified duration
    const char *timer_type = (timer_seconds == 60) ? "basic 60-second" : "synchronized";
    ESP_LOGI(TAG, "Enabling %s timer (%d seconds)", timer_type, timer_seconds);

    // Step 4: Reset the RTC timer state
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

    // Configure timer: 1Hz clock + timer interrupt enable
    uint8_t timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ | PCF85063A_RTC_TIMER_MODE_TIE;
    ret = pcf85063a_set_timer_safe(&s_rtc_dev, timer_seconds, timer_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "%d-second timer enabled", timer_seconds);

    // Verify timer was set correctly
    uint8_t verify_val, verify_mode;
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_VAL, &verify_val, 1);
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_MODE, &verify_mode, 1);
    ESP_LOGI(TAG, "Timer verification - Value: %d, Mode: 0x%02x", verify_val, verify_mode);

    return ESP_OK;
}

/**
 * @brief Process minute tick timer interrupt
 *
 * This function should be called from the timer interrupt handler.
 * It implements steps 6-8 from the synchronization instructions:
 * - Verifies timer flag is set
 * - Clears the flag
 * - Switches to repeating timer after first interrupt
 * - Returns whether this was a valid minute tick
 *
 * @param[out] is_minute_tick Set to true if this is a valid minute tick
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_process_minute_interrupt(bool *is_minute_tick) {
    if (!is_minute_tick || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    *is_minute_tick = false;

    // Handle the timer interrupt
    uint8_t timer_flag = 0;
    esp_err_t ret = pcf85063a_get_timer_flag(&s_rtc_dev, &timer_flag);
    if (ret != ESP_OK || !timer_flag) {
        // Not a timer interrupt or error reading flag
        return ret;
    }

    // Clear the timer flag immediately
    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Switch to repeating timer on first interrupt (one time only)
    if (!s_minute_timer_synced) {
        ESP_LOGI(TAG, "First synchronized minute tick - switching to repeating timer");
        ret = clock_sync_minute_timer();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // Valid minute tick
    *is_minute_tick = true;

    return ESP_OK;
}

/**
 * @brief Check if minute timer is synchronized to boundaries
 *
 * @return true if timer is synchronized to minute boundaries, false otherwise
 */
bool clock_is_minute_timer_synced(void) { return s_initialized && s_minute_timer_synced; }
