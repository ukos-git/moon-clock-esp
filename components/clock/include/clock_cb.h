#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Update clock display variables in UI
 *
 * Reads current time from RTC and updates the UI clock label and seconds.
 */
void clock_update_display_vars(void);

/**
 * @brief Update alarm label in UI
 *
 * @param hour Alarm hour (0-23)
 * @param minute Alarm minute (0-59)
 */
void clock_update_alarm_label(int hour, int minute);

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
