#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Minute interrupt type
 */
typedef enum {
    CLOCK_MINUTE_INT_NONE = 0, ///< No minute interrupt
    CLOCK_MINUTE_INT_HALF = 1, ///< Half minute interrupt (every 30 seconds)
    CLOCK_MINUTE_INT_FULL = 2, ///< Full minute interrupt (every 60 seconds)
} clock_minute_int_type_t;

/**
 * @brief Enable minute or half-minute interrupt
 *
 * This function enables the RTC's built-in minute (MI) or half-minute (HMI) interrupt.
 * These interrupts run in sync with the seconds counter:
 * - MI generates an interrupt every 60 seconds (at seconds = 0)
 * - HMI generates an interrupt every 30 seconds (at seconds = 0 and 30)
 *
 * When starting MI, the first interrupt will be generated after 1-59 seconds.
 * When starting HMI, the first interrupt will be generated after 1-29 seconds.
 * Subsequent periods do not have such a delay.
 *
 * The interrupt pulse on INT pin is 1/64 seconds wide in normal mode.
 *
 * Note: Enabling both MI and HMI will result in interrupts every 30 seconds.
 * A minute interrupt on top of a half-minute interrupt is not distinguishable.
 *
 * @param type Type of minute interrupt to enable (NONE to disable both)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_minute_enable_interrupt(clock_minute_int_type_t type);

/**
 * @brief Disable all minute interrupts
 *
 * Disables both MI and HMI interrupts.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_minute_disable_interrupt(void);

/**
 * @brief Process minute interrupt
 *
 * This function should be called from the interrupt handler when a timer flag
 * is detected. It verifies the flag is from MI/HMI and clears it.
 *
 * @param[out] is_minute_tick Set to true if this was a valid minute interrupt
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_minute_process_interrupt(bool *is_minute_tick);

/**
 * @brief Check which minute interrupt is currently enabled
 *
 * @return clock_minute_int_type_t Current minute interrupt type
 */
clock_minute_int_type_t clock_minute_get_enabled_type(void);

#ifdef __cplusplus
}
#endif
