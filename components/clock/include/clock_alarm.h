#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

#include "clock_types.h"

/**
 * @brief Set alarm time
 *
 * @param time Alarm time (hour and minute used)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_set_alarm(const clock_time_t *time);

/**
 * @brief Get currently set alarm time
 *
 * @param time Pointer to store alarm time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_get_alarm(clock_time_t *time);

/**
 * @brief Enable/disable alarm
 *
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_enable_alarm(bool enable);

/**
 * @brief Clear alarm
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_clear_alarm(void);

#ifdef __cplusplus
}
#endif
