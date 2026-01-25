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

#include "clock_types.h"

// Include all specialized clock headers
#include "clock_alarm.h"
#include "clock_alarm_scheduler.h"
#include "clock_cb.h"
#include "clock_interrupt.h"
#include "clock_sntp.h"
#include "clock_timer.h"

/**
 * @brief Clock configuration structure
 */
typedef struct {
    // Configuration is minimal since BSP handles I2C
    bool enable_ntp_sync; // Enable automatic NTP time synchronization
} clock_config_t;

/**
 * @brief Initialize the clock component
 *
 * This function initializes the RTC and optionally synchronizes time via NTP.
 * If enable_ntp_sync is true and WiFi is available, it will attempt NTP synchronization.
 * Otherwise, it uses current system time. If RTC time appears invalid (before 2020),
 * it will be corrected automatically.
 *
 * @param cfg Clock configuration including NTP sync option
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

#ifdef __cplusplus
}
#endif
