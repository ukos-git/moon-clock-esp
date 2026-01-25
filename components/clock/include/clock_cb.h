#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clock_types.h"
#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Generic callback function type for updates (matches lv_event_t signature)
 */
typedef void (*clock_update_cb_t)(void *user_data);

/**
 * @brief Register callback for clock time updates
 *
 * @param callback Function to call when time should be updated (called with NULL)
 */
void clock_register_time_update_callback(clock_update_cb_t callback);

/**
 * @brief Register callback for alarm updates
 *
 * @param callback Function to call when alarm should be updated (called with NULL)
 */
void clock_register_alarm_update_callback(clock_update_cb_t callback);

/**
 * @brief Trigger time display update (calls registered callback)
 */
void clock_trigger_time_update(void);

/**
 * @brief Trigger alarm display update (calls registered callback)
 */
void clock_trigger_alarm_update(void);

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
