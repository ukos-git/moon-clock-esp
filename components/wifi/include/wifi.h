/**
 * WiFi Connection Manager Component
 * Handles WiFi initialization and connection management with SPIFFS credential storage
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi connection
 *
 * Initializes NVS, mounts SPIFFS, reads WiFi credentials from /spiffs/wifi_credentials.txt,
 * configures and starts WiFi connection.
 *
 * @return esp_err_t ESP_OK on success, ESP_FAIL on credential read failure, ESP_ERR_TIMEOUT on connection timeout
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Write WiFi credentials to /spiffs/wifi_credentials.txt
 *
 * The inverse of the credential read: first non-comment line = SSID,
 * second = password (a comment header is written so the file stays
 * readable by hand). Both must be non-empty and fit the credential
 * limits (SSID < 32 chars, password < 64 chars); the password is never
 * logged.
 *
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG/ESP_ERR_INVALID_SIZE on bad input
 */
esp_err_t wifi_credentials_write(const char *ssid, const char *password);

/**
 * @brief Check if WiFi is connected
 *
 * @return bool true if WiFi is connected and has IP address
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Deinitialize WiFi connection
 *
 * Stops WiFi, cleans up resources and event groups
 */
void wifi_manager_deinit(void);

#define WIFI_SCAN_MAX_APS 16

/**
 * @brief One scanned access point
 */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth; /* wifi_auth_mode_t */
} wifi_ap_info_t;

/**
 * @brief Start a background AP scan
 *
 * Runs the scan on a dedicated task. When the radio is down (the
 * normal at-rest state) the task brings it up minimally (init + STA
 * mode + start, no credentials, no connection) and deinit's it again
 * when done; when the manager owns the radio the scan shares it and
 * leaves it running. No-op while a scan is already in progress.
 */
void wifi_scan_start(void);

/**
 * @brief Whether a scan is currently running
 */
bool wifi_scan_in_progress(void);

/**
 * @brief Copy the last completed scan's results
 *
 * @param out   destination buffer
 * @param max   buffer capacity
 * @return int  number of APs copied (0 until the first completion)
 */
int wifi_scan_get_results(wifi_ap_info_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif // WIFI_H
