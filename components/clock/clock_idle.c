#include "clock_idle.h"
#include "clock_cb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "clock_idle";

// Idle state
static uint64_t s_last_activity_time_ms = 0;
static uint64_t s_idle_entered_ms = 0; /* when the current idle episode started (0 = not idle) */
static bool s_is_in_idle_mode = false;
static bool s_idle_monitoring_enabled = true;

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

        // Only trigger wake callback if we were in idle mode
        // (This prevents triggering on every touch when already on main screen)
        if (s_is_in_idle_mode) {
            uint64_t in_idle_ms = esp_timer_get_time() / 1000 - s_idle_entered_ms;
            ESP_LOGI(TAG, "Waking from idle mode after %llu ms in sleep", (unsigned long long)in_idle_ms);
            s_is_in_idle_mode = false;
            s_idle_entered_ms = 0;
            clock_trigger_idle_wake();
        } else {
            ESP_LOGD(TAG, "touch while active (no wake)");
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

/* Tilt (IMU) is the moon's input, so sustained device motion counts as
 * user activity: it holds the idle timer while the user is manipulating
 * the ball, and it is the wake path from sleep for a device operated by
 * tilt rather than touch (touch was the only wake input before; the RTC
 * alarm was the only thing that could recover a tilt-only device from
 * sleep). Called at most once per second by the moon UI's tilt monitor. */
void clock_idle_notify_tilt_activity(void) {
    s_last_activity_time_ms = esp_timer_get_time() / 1000;
    if (s_is_in_idle_mode) {
        uint64_t in_idle_ms = s_last_activity_time_ms - s_idle_entered_ms;
        ESP_LOGI(TAG, "Waking from idle mode via tilt after %llu ms in sleep", (unsigned long long)in_idle_ms);
        s_is_in_idle_mode = false;
        s_idle_entered_ms = 0;
        clock_trigger_idle_wake();
    } else {
        ESP_LOGD(TAG, "tilt activity while active (no wake)");
    }
}

void clock_idle_check_cb(lv_timer_t *timer) {
    // Don't check if idle monitoring is disabled (e.g., on alarm screen)
    if (!s_idle_monitoring_enabled) {
        return;
    }

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
        s_idle_entered_ms = current_time_ms;

        // Trigger idle timeout callback
        clock_trigger_idle_timeout();
    }
}

bool clock_is_idle(void) { return s_is_in_idle_mode; }

uint64_t clock_idle_last_activity_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000) - s_last_activity_time_ms; }

void clock_idle_set_enabled(bool enabled) {
    s_idle_monitoring_enabled = enabled;
    if (enabled) {
        ESP_LOGI(TAG, "Idle monitoring enabled");
        // Reset timer when re-enabling
        s_last_activity_time_ms = esp_timer_get_time() / 1000;
    } else {
        ESP_LOGI(TAG, "Idle monitoring disabled");
    }
}
