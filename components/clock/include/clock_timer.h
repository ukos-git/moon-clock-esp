#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Enable/disable countdown timer with specified configuration
 *
 * This function enables the PCF85063A countdown timer with custom settings.
 * The timer can be configured with different clock frequencies and modes.
 * Pass 0 for timer_seconds to disable the timer.
 *
 * Common timer_mode configurations:
 * - PCF85063A_RTC_TIMER_TCF_4096HZ: 4.096 kHz timer source
 * - PCF85063A_RTC_TIMER_TCF_64HZ: 64 Hz timer source
 * - PCF85063A_RTC_TIMER_TCF_1HZ: 1 Hz timer source (most common)
 * - PCF85063A_RTC_TIMER_TCF_1_60HZ: 1/60 Hz timer source
 * Combine with:
 * - PCF85063A_RTC_TIMER_MODE_TIE: Enable timer interrupt
 * - PCF85063A_RTC_TIMER_MODE_TI_TP: Pulse mode (vs level mode)
 *
 * Example: timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ | PCF85063A_RTC_TIMER_MODE_TIE
 *
 * @param timer_seconds Timer duration in seconds (0 to disable, 1-255 to enable)
 * @param timer_mode Timer configuration (clock frequency + interrupt settings)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_timer_enable(uint8_t timer_seconds, uint8_t timer_mode);

/**
 * @brief Disable countdown timer
 *
 * Convenience function to disable the timer.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_timer_disable(void);

/**
 * @brief Process timer interrupt
 *
 * This function should be called from the interrupt handler when a timer flag
 * is detected. It verifies the flag, clears it, and indicates if this was a
 * valid timer interrupt.
 *
 * @param[out] is_timer_tick Set to true if this is a valid timer interrupt
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_timer_process_interrupt(bool *is_timer_tick);

#ifdef __cplusplus
}
#endif
