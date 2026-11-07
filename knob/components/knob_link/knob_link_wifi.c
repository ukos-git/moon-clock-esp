/**
 * @file knob_link_wifi.c
 * @brief STA join for the knob — the clock's components/wifi/wifi.c join
 *        shape (credentials from a baked spiffs file), simplified: the
 *        knob owns its radio for the whole boot (no scan/deinit dance), so
 *        there is one event handler, one netif and no re-init path; a lost
 *        association just reconnects with a back-off.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "knob_link.h"
#include "nvs_flash.h"

static const char *TAG = "knob_wifi";

#define KNOB_CREDENTIALS_FILE "/spiffs/knob_credentials.txt"
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define WIFI_CONNECTED_BIT BIT0
#define JOIN_WAIT_MS 10000 /* how long knob_link_wifi_init waits once */
#define RECONNECT_DELAY_MS 5000

static EventGroupHandle_t s_wifi_event_group;
static bool s_connected;
static bool s_started; /* one-shot guard: join shape, no re-init */

static esp_err_t read_knob_credentials(char *ssid, char *password) {
    FILE *f = fopen(KNOB_CREDENTIALS_FILE, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s — flash the spiffs image", KNOB_CREDENTIALS_FILE);
        return ESP_FAIL;
    }
    char line[256];
    bool ssid_found = false, password_found = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = 0;
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        size_t n = strlen(trimmed);
        if (!ssid_found) {
            if (n >= MAX_SSID_LEN) {
                n = MAX_SSID_LEN - 1;
            }
            memcpy(ssid, trimmed, n);
            ssid[n] = '\0';
            ssid_found = true;
            ESP_LOGI(TAG, "Found SSID: %s", ssid);
        } else {
            if (n >= MAX_PASSWORD_LEN) {
                n = MAX_PASSWORD_LEN - 1;
            }
            memcpy(password, trimmed, n);
            password[n] = '\0';
            password_found = true;
            /* password length only — never log it */
            ESP_LOGI(TAG, "Found password (length: %d)", (int)strlen(password));
        }
    }
    fclose(f);
    if (!ssid_found || !password_found) {
        ESP_LOGE(TAG, "Need SSID on line 1 and password on line 2 of %s", KNOB_CREDENTIALS_FILE);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "WiFi lost, retrying in %d ms", RECONNECT_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t knob_link_wifi_init(void) {
    if (s_started) {
        return ESP_OK;
    }

    const esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    { /* what is actually on the partition? (format_if_mount_failed can
       * silently re-format it empty on a geometry mismatch) */
        size_t total = 0, used = 0;
        if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mounted (%s): %u/%u bytes used", esp_err_to_name(ret), (unsigned)total,
                     (unsigned)used);
        }
        DIR *d = opendir("/spiffs");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                ESP_LOGI(TAG, "spiffs file: %s", e->d_name);
            }
            closedir(d);
        }
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif"); /* INVALID_STATE tolerated below */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL), TAG,
        "wifi handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL), TAG,
        "ip handler");

    char ssid[MAX_SSID_LEN] = {0};
    char password[MAX_PASSWORD_LEN] = {0};
    ESP_RETURN_ON_ERROR(read_knob_credentials(ssid, password), TAG, "credentials");

    wifi_config_t wifi_config = {
        .sta =
            {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            },
    };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    s_started = true;
    ESP_LOGI(TAG, "WiFi join started (SSID %s)", ssid);

    EventBits_t bits =
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(JOIN_WAIT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Not connected after %d ms — retrying in background", JOIN_WAIT_MS);
    return ESP_ERR_TIMEOUT;
}

bool knob_link_wifi_is_connected(void) { return s_connected; }
