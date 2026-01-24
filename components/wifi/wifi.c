/**
 * WiFi Connection Manager Component
 * Handles WiFi initialization and connection management with SPIFFS credential storage
 */

#include "wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "WIFI";

#define WIFI_CREDENTIALS_FILE "/spiffs/wifi_credentials.txt"
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_RETRY 5

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool wifi_connected = false;

// Read WiFi credentials from SPIFFS file
static esp_err_t read_wifi_credentials(char *ssid, char *password) {
    FILE *f = fopen(WIFI_CREDENTIALS_FILE, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", WIFI_CREDENTIALS_FILE);
        ESP_LOGE(TAG, "Please flash SPIFFS with your credentials file");
        return ESP_FAIL;
    }

    // Read SSID (first line)
    if (fgets(ssid, MAX_SSID_LEN, f) == NULL) {
        ESP_LOGE(TAG, "Failed to read SSID from credentials file");
        fclose(f);
        return ESP_FAIL;
    }
    ssid[strcspn(ssid, "\r\n")] = 0; // Remove newline

    // Read password (second line)
    if (fgets(password, MAX_PASSWORD_LEN, f) == NULL) {
        ESP_LOGE(TAG, "Failed to read password from credentials file");
        fclose(f);
        return ESP_FAIL;
    }
    password[strcspn(password, "\r\n")] = 0; // Remove newline

    fclose(f);
    ESP_LOGI(TAG, "Loaded WiFi credentials from %s", WIFI_CREDENTIALS_FILE);
    return ESP_OK;
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        wifi_connected = false;
        ESP_LOGI(TAG, "WiFi connection failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Mount SPIFFS for WiFi credentials
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true // Auto-format if mount fails
    };
    ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Please ensure SPIFFS partition exists and is properly configured");
        return ret;
    }
    ESP_LOGI(TAG, "SPIFFS mounted successfully");

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure WiFi
    char ssid[MAX_SSID_LEN] = {0};
    char password[MAX_PASSWORD_LEN] = {0};

    ret = read_wifi_credentials(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi credentials not configured. Please set valid credentials.");
        return ret;
    }

    wifi_config_t wifi_config = {
        .sta =
            {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");

    // Wait for connection (with timeout)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi SSID: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

bool wifi_manager_is_connected(void) { return wifi_connected; }

void wifi_manager_deinit(void) {
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_connected = false;
}
