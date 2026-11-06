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
#include "freertos/task.h"
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
#define MAX_RETRY 2                     // Reduced retries for faster failure detection
#define WIFI_CONNECTION_TIMEOUT_MS 8000 // 8 second timeout

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool wifi_connected = false;
#if defined(CONFIG_BOARD_P4)
// The remote (esp_hosted) radio on the 4C board is initialized exactly once
// and never deinit'd: the host-side LWIP netif attach is not re-runnable, so a
// second esp_wifi_init() re-adds the persistent netif and asserts (netif_add
// "netif already added"). Power-cycling the radio is stop/start only. The S3
// (built-in radio) does not need this and deinits/reinits freely.
static bool s_wifi_inited = false;
#endif

// Read WiFi credentials from SPIFFS file
static esp_err_t read_wifi_credentials(char *ssid, char *password) {
    FILE *f = fopen(WIFI_CREDENTIALS_FILE, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", WIFI_CREDENTIALS_FILE);
        ESP_LOGE(TAG, "Please flash SPIFFS with your credentials file");
        return ESP_FAIL;
    }

    char line[256]; // Large buffer to read entire lines including long comments
    bool ssid_found = false;
    bool password_found = false;

    // Read lines, skipping comments and empty lines
    while (fgets(line, sizeof(line), f) != NULL) {
        // Remove newline characters
        line[strcspn(line, "\r\n")] = 0;

        // Trim leading whitespace
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }

        // Skip empty lines and comments (lines starting with #)
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        // First non-comment line is SSID. strnlen+memcpy: a plain
        // strncpy/snprintf of the 256-byte line into the 32-byte field
        // trips -Wstringop-truncation / -Wformat-truncation under
        // -Werror=all (the P4's GCC 14 RISC-V toolchain) even though the
        // truncation is the point.
        if (!ssid_found) {
            size_t n = strnlen(trimmed, MAX_SSID_LEN - 1);
            memcpy(ssid, trimmed, n);
            ssid[n] = '\0';
            ssid_found = true;
            ESP_LOGI(TAG, "Found SSID: %s", ssid);
            continue;
        }

        // Second non-comment line is password (same bounded copy)
        if (!password_found) {
            size_t n = strnlen(trimmed, MAX_PASSWORD_LEN - 1);
            memcpy(password, trimmed, n);
            password[n] = '\0';
            password_found = true;
            ESP_LOGI(TAG, "Found password (length: %d)", strlen(password));
            break;
        }
    }

    fclose(f);

    if (!ssid_found || !password_found) {
        ESP_LOGE(TAG, "Failed to read valid SSID and password from credentials file");
        ESP_LOGE(TAG, "The file appears to contain only comments or is empty");
        ESP_LOGE(TAG, "Please add your WiFi SSID on the first non-comment line");
        ESP_LOGE(TAG, "and your WiFi password on the second non-comment line");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Loaded WiFi credentials from %s", WIFI_CREDENTIALS_FILE);
    return ESP_OK;
}

// Write WiFi credentials to the SPIFFS file (the inverse of
// read_wifi_credentials: first non-comment line = SSID, second = password).
// Comment header mirrors spiffs/wifi_credentials.example.txt so the file
// stays readable when inspected by hand. The password is never logged.
esp_err_t wifi_credentials_write(const char *ssid, const char *password) {
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len == 0 || ssid_len >= MAX_SSID_LEN) {
        ESP_LOGW(TAG, "SSID not writable: empty or too long (%zu chars, max %d)", ssid_len, MAX_SSID_LEN - 1);
        return ESP_ERR_INVALID_SIZE;
    }
    if (password_len == 0 || password_len >= MAX_PASSWORD_LEN) {
        ESP_LOGW(TAG, "Password not writable: empty or too long (%zu chars, max %d)", password_len,
                 MAX_PASSWORD_LEN - 1);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *f = fopen(WIFI_CREDENTIALS_FILE, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for writing", WIFI_CREDENTIALS_FILE);
        return ESP_FAIL;
    }
    fprintf(f, "# WiFi Credentials\n# Line 1: WiFi SSID\n# Line 2: WiFi Password\n%s\n%s\n", ssid, password);
    int rc = fclose(f);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to write %s", WIFI_CREDENTIALS_FILE);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wrote credentials (SSID %zu chars, password %zu chars)", ssid_len, password_len);
    return ESP_OK;
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // Auto-connect (STA_START) and the disconnect retry are the MANAGER's
    // behaviour and must only run while the manager owns the radio
    // (s_wifi_event_group != NULL). The AP scan brings the radio up on its
    // own (radio_was_up == false) and must not trigger a connect: on the P4
    // the manager's SSID config persists across the stop-only deinit (the
    // remote radio is never esp_wifi_deinit'ed), so a stray esp_wifi_connect()
    // here would start a real association and make the blocking scan fail
    // with ESP_ERR_WIFI_STATE. The same gating stops the zombie "Retry
    // connecting / connection failed" the airplane/light-sleep deinit used to
    // leave behind.
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_wifi_event_group) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_event_group) {
            if (s_retry_num < MAX_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retry connecting to WiFi...");
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
        wifi_connected = false;
        ESP_LOGI(TAG, "WiFi connection failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        // Check if event group still exists before setting bits
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t wifi_manager_init(void) {
    // If WiFi is already initialized (e.g., reconnecting after deinit), clean up first
    if (s_wifi_event_group != NULL) {
        ESP_LOGI(TAG, "WiFi already initialized, reinitializing...");

        // Unregister event handlers first to prevent events during cleanup
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

        // Now safe to deinit
        wifi_manager_deinit();
    }

    // Initialize NVS (safe to call multiple times)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Mount SPIFFS for WiFi credentials (check if already mounted)
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
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted successfully");
    }

    // Initialize network interface (safe to call multiple times)
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create default event loop if needed (safe to call multiple times)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create default WiFi STA interface if not exists
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    // Initialize WiFi
#if defined(CONFIG_BOARD_P4)
    // Re-bringing the (already-inited, stopped) remote radio is set_mode+start
    // below — esp_wifi_init is not re-runnable on the host (see
    // wifi_manager_deinit), so it runs only once.
    if (!s_wifi_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        s_wifi_inited = true;
    }
#else
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
#endif

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
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");

    // Wait for connection (with timeout)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECTION_TIMEOUT_MS));

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
#if !defined(CONFIG_BOARD_P4)
    esp_wifi_deinit();
#endif
    // The remote radio stays inited on the 4C (esp_wifi_deinit would break the
    // host netif re-attach — see s_wifi_inited). wifi_connected drops either way.
    wifi_connected = false;
}

/* ---------------- AP scan (the WiFi settings screen) ----------------
 *
 * The scan runs on its own task (esp_wifi_scan_start blocks ~3 s) and
 * keeps its results in a static buffer the screen polls. The radio's
 * lifecycle is owned by whoever brought it up: the manager (boot /
 * hourly sync, s_wifi_event_group != NULL) or this task. When we
 * bring it up we deinit it again when the scan is done (back to
 * modem sleep; the credentials file is never touched). */
static bool s_scan_running = false;
static wifi_ap_info_t s_scan_aps[WIFI_SCAN_MAX_APS];
static int s_scan_count = 0;
static wifi_ap_record_t s_scan_raw[WIFI_SCAN_MAX_APS];

static void wifi_scan_task_fn(void *arg) {
    (void)arg;
    bool radio_was_up = (s_wifi_event_group != NULL);
    bool we_own_radio = false;

    if (!radio_was_up) {
        /* Minimal radio bring-up: no credentials, no connection. The
         * manager's still-registered event handler will attempt
         * esp_wifi_connect() on STA_START; with no SSID configured it
         * fails fast and its event-group writes are NULL-guarded. */
#if defined(CONFIG_BOARD_P4)
        /* The remote radio is always inited (the manager inits it once; see
         * s_wifi_inited) — bring it up with set_mode+start only. */
        esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret == ESP_OK) {
            we_own_radio = true;
            esp_wifi_start();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
#else
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t ret = esp_wifi_init(&cfg);
        if (ret == ESP_OK) {
            we_own_radio = true;
            if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
                esp_wifi_start();
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                we_own_radio = false;
            }
        }
#endif
        if (we_own_radio) {
            ESP_LOGI(TAG, "Scan: radio brought up");
        } else {
            ESP_LOGE(TAG, "Scan: failed to bring up the radio: %s", esp_err_to_name(ret));
        }
    }

    /* Bounded retry (2026-11-06): the FIRST scan of a screen open --
     * the esp_hosted RPC scan through the remote C6 while the radio is
     * connected, or right after the 100 ms bring-up settle -- can
     * intermittently return 0 APs. The screen renders the result ONCE
     * (the refresh runs per entry only), so a 0-AP first scan would
     * park it on "NO APS FOUND" until the user closes and re-opens the
     * screen (the 2026-11-06 report). Up to 4 attempts with a 1 s
     * settle between them cover the flaky first scan; a genuinely
     * empty radio range still ends on NO APS FOUND after ~9 s. */
    const int WIFI_SCAN_MAX_ATTEMPTS = 4;
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .channel = 0,
    };
    for (int attempt = 1; attempt <= WIFI_SCAN_MAX_ATTEMPTS; attempt++) {
        esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
        if (ret == ESP_OK) {
            uint16_t ap_cnt = 0;
            esp_wifi_scan_get_ap_num(&ap_cnt);
            if (esp_wifi_scan_get_ap_records(&ap_cnt, s_scan_raw) == ESP_OK) {
                s_scan_count = ap_cnt < WIFI_SCAN_MAX_APS ? (int)ap_cnt : WIFI_SCAN_MAX_APS;
                for (int i = 0; i < s_scan_count; i++) {
                    size_t len = strlen((const char *)s_scan_raw[i].ssid);
                    if (len > 32) {
                        len = 32;
                    }
                    memcpy(s_scan_aps[i].ssid, s_scan_raw[i].ssid, len);
                    s_scan_aps[i].ssid[len] = '\0';
                    s_scan_aps[i].rssi = s_scan_raw[i].rssi;
                    s_scan_aps[i].auth = s_scan_raw[i].authmode;
                }
                ESP_LOGI(TAG, "Scan done: %d APs%s", s_scan_count, attempt > 1 ? " (retry)" : "");
            } else {
                s_scan_count = 0;
            }
        } else {
            s_scan_count = 0;
            ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(ret));
        }
        if (s_scan_count > 0 || attempt == WIFI_SCAN_MAX_ATTEMPTS) {
            break;
        }
        ESP_LOGW(TAG, "Scan attempt %d returned 0 APs -- retrying in 1 s", attempt);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (we_own_radio) {
        esp_wifi_stop();
#if !defined(CONFIG_BOARD_P4)
        esp_wifi_deinit();
#endif
        ESP_LOGI(TAG, "Scan: radio down again");
    }
    s_scan_running = false;
    vTaskDelete(NULL);
}

void wifi_scan_begin(void) {
    if (s_scan_running) {
        return;
    }
    s_scan_running = true;
    s_scan_count = 0;
    if (xTaskCreate(wifi_scan_task_fn, "wifi_scan", 6144, NULL, 5, NULL) != pdPASS) {
        s_scan_running = false;
        ESP_LOGE(TAG, "Scan task creation failed");
    }
}

bool wifi_scan_in_progress(void) { return s_scan_running; }

int wifi_scan_get_results(wifi_ap_info_t *out, int max) {
    if (!out || max <= 0) {
        return 0;
    }
    int n = s_scan_count < max ? s_scan_count : max;
    for (int i = 0; i < n; i++) {
        out[i] = s_scan_aps[i];
    }
    return n;
}
