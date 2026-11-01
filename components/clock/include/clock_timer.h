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
 * On the PCF85063A build this enables the hardware countdown timer with
 * custom settings. On builds without the PCF85063A (CLOCK_USE_PCF85063A=n,
 * the P4) the countdown is an esp_timer one-shot of timer_seconds; the
 * timer_mode bits are accepted but ignored.
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
 * @brief Get the host-side start time of the active countdown
 *
 * @return esp_timer_get_time() (µs) at the moment the countdown was enabled,
 *         or 0 if no countdown is active. This is the expiration oracle:
 *         the PCF85063A timer auto-reloads and the TF flag is shared with the
 *         minute interrupt, so the timer register value alone cannot
 *         distinguish "just expired" from "just started a while ago".
 */
int64_t clock_timer_get_enabled_at_us(void);

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

/**
 * @brief Consume the software countdown's expiration flag (P4 path)
 *
 * On builds without the PCF85063A the countdown's esp_timer one-shot sets
 * a flag when it fires (from the esp_timer task); the interrupt poller
 * (LVGL task) consumes it here before invoking the countdown callback.
 * Returns false immediately on the PCF build (its poller judges the TF
 * flag + host start timestamp instead).
 *
 * @return true if the countdown just expired (flag was set and is now cleared)
 */
bool clock_timer_take_expired(void);

#ifdef __cplusplus
}
#endif
