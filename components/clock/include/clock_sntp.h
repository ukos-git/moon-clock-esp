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
#include <stdbool.h>
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
 * @brief Check whether the wall clock holds trusted time
 *
 * Trusted = the last time written to the system clock came from a
 * successful SNTP sync or from the user's manual time entry. The S3's
 * time base is the PCF85063A (self-trusting; the flag is not consulted
 * there); on the P4 it gates the software alarm fire (a default/unsynced
 * clock must not ring a phantom alarm) and drives the UI's "unsynced"
 * display state.
 *
 * @return bool true once a trusted time source has been applied
 */
bool clock_sntp_time_trusted(void);

/**
 * @brief Mark the wall clock time as trusted (manual time entry)
 *
 * Called by the P4 manual time-entry screen when the user commits a time
 * with no network available.
 *
 * @param trusted true to mark the current wall clock as trusted
 */
void clock_sntp_set_time_trusted(bool trusted);

/**
 * @brief Deinitialize SNTP component
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_sntp_deinit(void);

#if !CONFIG_CLOCK_USE_PCF85063A
/**
 * @brief Snapshot the wall clock to NVS (P4, no external RTC IC)
 *
 * A full power loss kills the crystal RTC domain, so the NVS epoch is the
 * P4's only cross-power-loss memory of the time. Forced save; the 15 min
 * interval is handled by clock_nvs_maybe_save_epoch().
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_nvs_save_epoch(void);

/**
 * @brief Periodically snapshot the wall clock to NVS (P4)
 *
 * No-op (ESP_OK) when the 15 min interval has not elapsed. Called from
 * the software minute tick.
 *
 * @return esp_err_t ESP_OK on success (or when no save was due)
 */
esp_err_t clock_nvs_maybe_save_epoch(void);

/**
 * @brief Restore the last NVS snapshot into the system clock (P4)
 *
 * Applied only when the system clock is still at its pre-first-sync
 * default. The restored time is NOT trusted: the trust flag stays down
 * until the next SNTP sync or a manual entry.
 *
 * @return bool true when a plausible snapshot was applied
 */
bool clock_nvs_restore_epoch(void);
#endif

#ifdef __cplusplus
}
#endif
