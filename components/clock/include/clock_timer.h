#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Timer frequency constants (maps to TCF[1:0] bits in Timer Mode register)
#define CLOCK_TIMER_FREQ_4096HZ (0x00) // 4.096 kHz timer source
#define CLOCK_TIMER_FREQ_64HZ (0x08)   // 64 Hz timer source
#define CLOCK_TIMER_FREQ_1HZ (0x10)    // 1 Hz timer source (most common)
#define CLOCK_TIMER_FREQ_1_60HZ (0x18) // 1/60 Hz timer source

// Timer mode flags
#define CLOCK_TIMER_MODE_ENABLE_INT (0x02) // Enable timer interrupt (TIE bit)
#define CLOCK_TIMER_MODE_PULSE (0x01)      // Pulse mode vs level mode (TI_TP bit)

/**
 * @brief Enable/disable countdown timer with specified configuration
 *
 * This function enables the PCF85063A countdown timer with custom settings.
 * The timer can be configured with different clock frequencies and modes.
 * Pass 0 for timer_seconds to disable the timer.
 *
 * Common timer_mode configurations:
 * - CLOCK_TIMER_FREQ_4096HZ: 4.096 kHz timer source
 * - CLOCK_TIMER_FREQ_64HZ: 64 Hz timer source
 * - CLOCK_TIMER_FREQ_1HZ: 1 Hz timer source (most common)
 * - CLOCK_TIMER_FREQ_1_60HZ: 1/60 Hz timer source
 * Combine with:
 * - CLOCK_TIMER_MODE_ENABLE_INT: Enable timer interrupt
 * - CLOCK_TIMER_MODE_PULSE: Pulse mode (vs level mode)
 *
 * Example: timer_mode = CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT
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
 * @brief Check if countdown timer is currently active
 *
 * @return true if timer is enabled and running
 */
bool clock_timer_is_active(void);

/**
 * @brief Get the initial countdown timer value
 *
 * @return Initial timer value that was set, or 0 if timer is not active
 */
uint8_t clock_timer_get_value(void);

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
