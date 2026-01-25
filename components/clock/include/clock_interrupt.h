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

#ifdef __cplusplus
}
#endif
