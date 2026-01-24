#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Callback function type for clock display updates
 */
typedef void (*clock_display_update_cb_t)(void);

/**
 * @brief Register a callback function for clock display updates
 *
 * @param callback Function to call when display update is needed
 */
void clock_register_display_update_callback(clock_display_update_cb_t callback);

/**
 * @brief RTC interrupt handler callback for LVGL timer
 *
 * This function should be used as an LVGL timer callback to periodically
 * check for RTC interrupts and handle them appropriately.
 *
 * @param timer LVGL timer handle (unused)
 */
void clock_rtc_interrupt_handler_cb(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif
