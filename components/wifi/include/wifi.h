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

#ifdef __cplusplus
}
#endif

#endif // WIFI_H
