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
 * @brief Trigger time display update (calls registered callback)
 */
void clock_trigger_time_update(void);

/**
 * @brief Register callback for clock alarm updates
 *
 * @param callback Function to call when alarm should be updated (called with NULL)
 */
void clock_register_alarm_update_callback(clock_update_cb_t callback);

/**
 * @brief Trigger alarm display update (calls registered callback)
 */
void clock_trigger_alarm_update(void);

/**
 * @brief Register callback for alarm trigger event
 *
 * @param callback Function to call when alarm is triggered (called with NULL)
 */
void clock_register_alarm_trigger_callback(clock_update_cb_t callback);

/**
 * @brief Trigger alarm trigger callback (called when alarm goes off)
 */
void clock_trigger_alarm_triggered(void);

/**
 * @brief Register callback for countdown timer expiration
 *
 * @param callback Function to call when countdown timer expires (called with NULL)
 */
void clock_register_countdown_timer_callback(clock_update_cb_t callback);

/**
 * @brief Trigger countdown timer callback (called when countdown timer expires)
 */
void clock_trigger_countdown_timer(void);

/**
 * @brief Register callback for idle timeout
 *
 * @param callback Function to call when idle timeout occurs (called with NULL)
 */
void clock_register_idle_timeout_callback(clock_update_cb_t callback);

/**
 * @brief Register callback for wake from idle
 *
 * @param callback Function to call when waking from idle (called with NULL)
 */
void clock_register_idle_wake_callback(clock_update_cb_t callback);

/**
 * @brief Trigger idle timeout callback
 */
void clock_trigger_idle_timeout(void);

/**
 * @brief Trigger idle wake callback
 */
void clock_trigger_idle_wake(void);

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
