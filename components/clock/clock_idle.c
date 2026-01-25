#include "clock_idle.h"
#include "clock_cb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "clock_idle";

// Idle state
static uint64_t s_last_activity_time_ms = 0;
static bool s_is_in_idle_mode = false;

// Configuration from Kconfig
#define IDLE_TIMEOUT_MS (CONFIG_CLOCK_IDLE_TIMEOUT_SEC * 1000ULL)

/**
 * @brief Input device read callback to detect user activity
 */
static void indev_read_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_target(e);

    // Check if input device is currently pressed/active
    if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        ESP_LOGI(TAG, "User activity detected (touch)");

        // Update last activity time
        s_last_activity_time_ms = esp_timer_get_time() / 1000;

        // If we were in idle mode, trigger wake callback
        if (s_is_in_idle_mode) {
            ESP_LOGI(TAG, "Waking from idle mode");
            s_is_in_idle_mode = false;

            clock_trigger_idle_wake();
        }
    }
}

esp_err_t clock_idle_init(void) {
    ESP_LOGI(TAG, "Initializing idle timer (timeout: %llu seconds)", (unsigned long long)(IDLE_TIMEOUT_MS / 1000));

    // Initialize last activity time to now
    s_last_activity_time_ms = esp_timer_get_time() / 1000;
    s_is_in_idle_mode = false;

    // Register callback on input device to detect touch activity
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, indev_read_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(indev, indev_read_cb, LV_EVENT_PRESSING, NULL);
        ESP_LOGI(TAG, "Input device event callbacks registered");
    } else {
        ESP_LOGE(TAG, "Failed to get input device");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void clock_idle_reset_timer(void) {
    s_last_activity_time_ms = esp_timer_get_time() / 1000;
    ESP_LOGD(TAG, "Idle timer reset");
}

void clock_idle_check_cb(lv_timer_t *timer) {
    // Don't check if already in idle mode
    if (s_is_in_idle_mode) {
        return;
    }

    uint64_t current_time_ms = esp_timer_get_time() / 1000;
    uint64_t idle_duration_ms = current_time_ms - s_last_activity_time_ms;

    // Check if idle timeout has been reached
    if (idle_duration_ms >= IDLE_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Idle timeout reached after %llu ms", (unsigned long long)idle_duration_ms);

        s_is_in_idle_mode = true;

        // Trigger idle timeout callback
        clock_trigger_idle_timeout();
    }
}

bool clock_is_idle(void) { return s_is_in_idle_mode; }
