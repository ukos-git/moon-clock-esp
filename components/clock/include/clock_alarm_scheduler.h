#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clock_types.h"
#include "esp_err.h"
#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif
