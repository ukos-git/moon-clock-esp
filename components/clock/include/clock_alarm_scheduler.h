#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clock_types.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_ALARM_RULES 10

/**
 * @brief Alarm rule structure (cron-like)
 */
typedef struct {
    int minute;          // 0-59, or -1 for wildcard
    int hour;            // 0-23, or -1 for wildcard
    int minute_interval; // For */N syntax: trigger every N minutes (0 = disabled)
    int hour_interval;   // For */N syntax: trigger every N hours (0 = disabled)
    uint8_t day_mask;    // Bit mask for days: bit 0 = Sunday, bit 1 = Monday, etc.
    bool all_days;       // True if alarm should trigger every day
} alarm_rule_t;

/**
 * @brief Initialize alarm scheduler from SPIFFS
 *
 * Reads alarm.txt from SPIFFS and loads alarm rules.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_scheduler_init(void);

/**
 * @brief Calculate and set the next alarm based on loaded rules
 *
 * Finds the next alarm time from current time based on all loaded rules,
 * and programs it into the RTC.
 *
 * @param current_time Current time to calculate next alarm from
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_schedule_next(const clock_time_t *current_time);

/**
 * @brief Get the number of loaded alarm rules
 *
 * @return int Number of alarm rules
 */
int clock_alarm_get_rule_count(void);

/**
 * @brief Get the next scheduled alarm time
 *
 * Returns the cached next alarm time that was scheduled.
 *
 * @param time Pointer to store the next alarm time
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if no alarm scheduled
 */
esp_err_t clock_alarm_get_next_scheduled(clock_time_t *time);

/**
 * @brief Write the alarm file (/spiffs/alarm.txt) with the given content
 *
 * The file is re-parsed by clock_alarm_scheduler_reload() after writing;
 * the writers below (set_pomodoro_interval / set_clock_alarm /
 * deactivate_all) do both in one call. All on the LVGL task, like the
 * RTC alarm poll -- no lock needed.
 *
 * @param content NUL-terminated cron rules (5 fields: minute hour dom month dow)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_scheduler_write(const char *content);

/**
 * @brief Re-parse the alarm file and reschedule the next alarm
 *
 * A file with no valid rules disarms the RTC alarm (the PCF85063A's
 * ALM register persists on the RTC, so a stale armed time would
 * otherwise fire a phantom alarm).
 *
 * @return esp_err_t ESP_OK on success (incl. 0 rules -> disarmed)
 */
esp_err_t clock_alarm_scheduler_reload(void);

/**
 * @brief Bitmask of the kinds of loaded alarm rules
 *
 * Bit 0: an interval rule (star-slash-N minutes -- the pomodoro timer).
 * Bit 1: a fixed-time rule (HH:MM on selected days).
 *
 * @return uint8_t 0 when no rules are loaded
 */
uint8_t clock_alarm_rule_types(void);

/**
 * @brief Get the interval of the loaded star-slash-N minutes rule
 *
 * @return int The interval in minutes, or -1 when no interval rule is loaded
 */
int clock_alarm_get_interval_rule(void);

/**
 * @brief Set the pomodoro timer: ring every N minutes (whole-day grid)
 *
 * Writes the star-slash-N minutes cron rule to the alarm file and reloads.
 *
 * @param minutes_total 1..1440
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_set_pomodoro_interval(int minutes_total);

/**
 * @brief Set a real alarm: fixed time of day on selected weekdays
 *
 * Writes "MM HH * * <dow>" to the alarm file and reloads.
 *
 * @param hour    0..23
 * @param minute  0..59
 * @param dow_mask Bits 0..6 (Sunday..Saturday); 0x7F = every day
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_set_clock_alarm(int hour, int minute, uint8_t dow_mask);

/**
 * @brief Deactivate all alarms
 *
 * Writes a comment-only alarm file and disarms the RTC alarm on reload.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_deactivate_all(void);

#ifdef __cplusplus
}
#endif
