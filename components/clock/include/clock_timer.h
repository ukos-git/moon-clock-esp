#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Enable/disable minute tick timer with specified duration
 *
 * This function enables a timer with the specified duration in seconds.
 * Pass 0 to disable the timer. For synchronized minute ticks that align
 * to wall-clock boundaries, use clock_init_minute_tick() instead.
 *
 * @param timer_seconds Timer duration in seconds (0 to disable, 1-255 to enable)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_enable_minute_tick(uint8_t timer_seconds);

/**
 * @brief Initialize synchronized minute timer
 *
 * This function starts a synchronized timer that will fire exactly at the next
 * minute boundary (HH:MM:00) and then continue firing every 60 seconds.
 * Uses the PCF85063A countdown timer with proper RTC time alignment.
 *
 * @return ESP_OK on success
 */
esp_err_t clock_init_minute_tick(void);

/**
 * @brief Synchronize minute timer after first interrupt
 *
 * This function should be called from the timer interrupt handler after the
 * first synchronization interrupt occurs. It switches the timer from one-shot
 * synchronization mode to repeating 60-second intervals.
 *
 * @return ESP_OK on success
 */
esp_err_t clock_sync_minute_timer(void);

/**
 * @brief Process minute tick timer interrupt
 *
 * This function should be called from the timer interrupt handler to process
 * synchronized minute ticks. It verifies the timer flag, clears it, and
 * handles the synchronization logic.
 *
 * @param[out] is_minute_tick Set to true if this is a valid minute tick
 * @return ESP_OK on success
 */
esp_err_t clock_process_minute_interrupt(bool *is_minute_tick);

/**
 * @brief Check if minute timer is synchronized to boundaries
 *
 * @return true if timer is synchronized to minute boundaries, false otherwise
 */
bool clock_is_minute_timer_synced(void);

#ifdef __cplusplus
}
#endif
