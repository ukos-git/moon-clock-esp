#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Callback function type for idle timeout
 *
 * Called when the idle timeout period expires without user interaction
 */
typedef void (*clock_idle_timeout_cb_t)(void);

/**
 * @brief Initialize the idle timer system
 *
 * This sets up activity monitoring and idle timeout checking.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_idle_init(void);

/**
 * @brief Reset the idle timer (called on user activity)
 *
 * This is automatically called by the display event handler,
 * but can be called manually if needed.
 */
void clock_idle_reset_timer(void);

/**
 * @brief Notify the idle system of tilt (IMU) activity
 *
 * The moon is operated by device tilt, not touch, so sustained motion
 * must hold the idle timer and wake from sleep. Safe to call from the
 * LVGL task context at most a few times per second; only a genuine
 * transition logs at INFO level.
 */
void clock_idle_notify_tilt_activity(void);

/**
 * @brief LVGL timer callback for idle checking
 *
 * This should be called periodically to check if idle timeout has expired.
 *
 * @param timer LVGL timer handle (unused)
 */
void clock_idle_check_cb(lv_timer_t *timer);

/**
 * @brief Check if currently in idle state
 *
 * @return true if in idle state (deep sleep screen showing)
 */
bool clock_is_idle(void);

/**
 * @brief Enable or disable idle monitoring
 *
 * When disabled, idle timeout will not trigger. Useful for screens
 * that should remain active indefinitely (e.g., alarm screen).
 *
 * @param enabled true to enable idle monitoring, false to disable
 */
void clock_idle_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif
