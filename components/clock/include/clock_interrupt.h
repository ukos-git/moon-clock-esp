#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Clock interrupt types
 */
typedef enum {
    CLOCK_INT_NONE = 0,
    CLOCK_INT_MINUTE = 1 << 0,    // Minute interrupt (MI/HMI)
    CLOCK_INT_COUNTDOWN = 1 << 1, // Countdown timer interrupt
    CLOCK_INT_ALARM = 1 << 2,     // Alarm interrupt
} clock_interrupt_t;

/**
 * @brief Handle all RTC interrupts comprehensively
 *
 * This function checks for pending RTC interrupts and processes them appropriately.
 * It handles both timer and alarm interrupts, managing synchronization and clearing flags.
 * Callbacks are invoked directly for both timer and alarm interrupts.
 * Should be called from a polling timer or interrupt handler.
 *
 * @return ESP_OK on success
 */
esp_err_t clock_handle_interrupts(void);

/**
 * @brief Get pending interrupts
 *
 * @param out Pointer to store interrupt flags
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_get_interrupts(clock_interrupt_t *out);

/**
 * @brief Clear specific interrupt flags
 *
 * @param flags Interrupt flags to clear
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_clear_interrupts(clock_interrupt_t flags);

/**
 * @brief Check if RTC interrupt is active on EXIO 3
 *
 * @return true if interrupt line is low (active), false otherwise
 */
bool clock_is_interrupt_pending(void);

/**
 * @brief Set the polling interval for RTC interrupt detection
 *
 * This is used to calculate the countdown timer expiration threshold.
 * Should be called before starting countdown timers.
 *
 * @param interval_ms Polling interval in milliseconds
 */
void clock_set_poll_interval_ms(uint32_t interval_ms);

/**
 * @brief Arm the software alarm one-shot (no-PCF builds only)
 *
 * Arms an esp_timer one-shot for the epoch of the given local alarm time
 * (the analog of the PCF's ALM register + AIE). The one-shot only sets a
 * flag; the interrupt poll (LVGL task) consumes it and fires the alarm
 * callback. Called from clock_alarm_scheduler.c after each schedule, so
 * an SNTP/manual time SET that shifts the epoch re-arms it there.
 * A no-op returning ESP_OK on PCF builds (the hardware register is armed
 * by clock_set_alarm / clock_enable_alarm instead).
 *
 * @param next The scheduled alarm time (full date, local)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_alarm_arm_esp_timer(const clock_time_t *next);

/**
 * @brief Disarm the software alarm one-shot (no-PCF builds only)
 *
 * Stops the one-shot and clears its fire flag (the 0-rules phantom
 * guard's P4 half). A no-op on PCF builds.
 */
void clock_alarm_disarm_esp_timer(void);

#ifdef __cplusplus
}
#endif
