/**
 * @file clock_sntp.h
 * @brief SNTP/NTP time synchronization for clock component
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clock_types.h"
#include "esp_err.h"
#include <time.h>

/**
 * @brief Initialize SNTP component
 *
 * Sets up timezone from Kconfig and prepares SNTP for synchronization.
 * Must be called before other SNTP functions.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_sntp_init(void);

/**
 * @brief Synchronize time via SNTP/NTP
 *
 * Attempts to synchronize system time with configured NTP server.
 * Requires WiFi connection to be active.
 *
 * @param timeout_ms Maximum time to wait for synchronization
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on timeout, ESP_ERR_INVALID_STATE if WiFi not connected
 */
esp_err_t clock_sntp_sync(uint32_t timeout_ms);

/**
 * @brief Get current system time (timezone-adjusted)
 *
 * Returns current system time adjusted for the configured timezone.
 *
 * @param time_out Pointer to store the current time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_sntp_get_system_time(clock_time_t *time_out);

/**
 * @brief Check if NTP synchronization is needed
 *
 * Returns true if no sync has been performed or if the sync interval has elapsed.
 *
 * @return bool true if sync is needed
 */
bool clock_sntp_needs_sync(void);

/**
 * @brief Get timestamp of last successful NTP sync
 *
 * @return time_t Unix timestamp of last sync, or 0 if never synced
 */
time_t clock_sntp_get_last_sync_time(void);

/**
 * @brief Deinitialize SNTP component
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_sntp_deinit(void);

#ifdef __cplusplus
}
#endif
