#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include <time.h>

/**
 * @brief Clock time structure
 */
typedef struct {
    int year;   // 2025
    int month;  // 1–12
    int day;    // 1–31
    int hour;   // 0–23
    int minute; // 0–59
    int second; // 0–59
} clock_time_t;

/**
 * @brief Clock interrupt types
 */
typedef enum {
    CLOCK_INT_NONE = 0,
    CLOCK_INT_TIMER = 1 << 0,
    CLOCK_INT_ALARM = 1 << 1,
} clock_interrupt_t;

/**
 * @brief Clock configuration structure
 */
typedef struct {
    // Configuration is minimal since BSP handles I2C
    uint32_t i2c_freq_hz;       // For reference only
    clock_time_t *initial_time; // Initial time to set, or NULL to use system time
} clock_config_t;

/**
 * @brief Initialize the clock component
 *
 * This function initializes the RTC, reads current time, and sets it if needed.
 * If cfg->initial_time is NULL, it uses system time. If RTC time appears invalid
 * (before 2020), it will be corrected automatically.
 *
 * @param cfg Clock configuration including initial time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_init(const clock_config_t *cfg);

/**
 * @brief Get current time from RTC
 *
 * @param out Pointer to store time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_get_time(clock_time_t *out);

/**
 * @brief Set RTC time
 *
 * @param time Time to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_set_time(const clock_time_t *time);

/**
 * @brief Enable/disable minute tick timer
 *
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_enable_minute_tick(bool enable);

/**
 * @brief Initialize and enable minute timer immediately
 *
 * @return ESP_OK on success
 */
esp_err_t clock_init_minute_tick(void);

/**
 * @brief Set alarm time
 *
 * @param time Alarm time (hour and minute used)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_set_alarm(const clock_time_t *time);

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
