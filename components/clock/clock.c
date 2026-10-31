#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_display.h"
#include "clock.h"
#include "clock_alarm.h"
#include "clock_interrupt.h"
#include "clock_sntp.h"
#include "clock_timer.h"
#include "pcf85063a.h"

static const char *TAG = "clock_component";

// EXIO 3 pin definition (IO expander pin for RTC interrupt)
#define EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3

// Global state accessible to other modules
pcf85063a_dev_t s_rtc_dev;
i2c_master_bus_handle_t s_bus_handle = NULL;
esp_io_expander_handle_t s_io_expander = NULL;
bool s_initialized = false;

// Validation functions
static bool validate_time(const clock_time_t *time) {
    if (!time)
        return false;
    if (time->year < 2000 || time->year > 2099)
        return false;
    if (time->month < 1 || time->month > 12)
        return false;
    if (time->day < 1 || time->day > 31)
        return false;
    if (time->hour < 0 || time->hour > 23)
        return false;
    if (time->minute < 0 || time->minute > 59)
        return false;
    if (time->second < 0 || time->second > 59)
        return false;
    return true;
}

// Helper functions to convert between our API and PCF85063A types
static void clock_to_pcf_time(const clock_time_t *clock_time, pcf85063a_datetime_t *pcf_time) {
    pcf_time->year = clock_time->year;
    pcf_time->month = clock_time->month;
    pcf_time->day = clock_time->day;
    pcf_time->hour = clock_time->hour;
    pcf_time->min = clock_time->minute;
    pcf_time->sec = clock_time->second;
    pcf_time->dotw = 0; // We don't use day of week
}

static void pcf_to_clock_time(const pcf85063a_datetime_t *pcf_time, clock_time_t *clock_time) {
    clock_time->year = pcf_time->year;
    clock_time->month = pcf_time->month;
    clock_time->day = pcf_time->day;
    clock_time->hour = pcf_time->hour;
    clock_time->minute = pcf_time->min;
    clock_time->second = pcf_time->sec;
}

esp_err_t clock_sync_time_from_ntp(uint32_t timeout_ms) {
    esp_err_t ret = clock_sntp_sync(timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NTP synchronization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    clock_time_t time_to_set;
    ret = clock_sntp_get_system_time(&time_to_set);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to obtain NTP-synchronized time: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Successfully obtained time via NTP: %04d-%02d-%02d %02d:%02d:%02d", time_to_set.year,
             time_to_set.month, time_to_set.day, time_to_set.hour, time_to_set.minute, time_to_set.second);
    /* Only valid when the RTC is up (called from clock_init after
     * s_initialized = true, and from the sync task after boot). */
    if (s_initialized) {
        ret = clock_set_time(&time_to_set);
        if (ret != ESP_OK)
            ESP_LOGW(TAG, "Failed to update RTC time: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t clock_init(const clock_config_t *cfg) {
    if (!cfg) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing clock component");

    esp_err_t ret;

#if !defined(CONFIG_BOARD_P4)
    // Get I2C master bus handle from BSP
    s_bus_handle = bsp_i2c_get_handle();
    if (!s_bus_handle) {
        ESP_LOGE(TAG, "Failed to get I2C bus handle from BSP");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize PCF85063A using the library
    ret = pcf85063a_init(&s_rtc_dev, s_bus_handle, PCF85063A_ADDRESS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCF85063A: %s", esp_err_to_name(ret));
        return ret;
    }

    // Store IO expander handle (shim: the P4 board has no expander, but
    // this path is S3-only anyway — see the CONFIG_BOARD_P4 guard).
    s_io_expander = board_io_expander_init();
    if (!s_io_expander) {
        ESP_LOGE(TAG, "Failed to initialize IO expander from BSP");
        return ESP_ERR_INVALID_STATE;
    }

    // Configure EXIO 3 pin as input for RTC interrupt
    ret = esp_io_expander_set_dir(s_io_expander, EXIO_RTC_INT_PIN, IO_EXPANDER_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure EXIO 3 as input: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    // The 4C board has no PCF85063A / IO expander: the time base is the
    // crystal-backed internal RTC timer + SNTP (the software clock path
    // lands in M5; the PCF85063A code is S3-only).
    ESP_LOGI(TAG, "P4 board: no external RTC IC (internal crystal RTC timer + SNTP)");
#endif

    // Mark as initialized before attempting time operations
    s_initialized = true;

    // Handle time initialization with NTP integration
    clock_time_t time_to_set;
    bool time_obtained = false;

    // Initialize SNTP component first
    ret = clock_sntp_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(ret));
    }

    // Try NTP synchronization if enabled and WiFi is available
    if (cfg->enable_ntp_sync) {
        ESP_LOGI(TAG, "Attempting NTP time synchronization...");
        if (clock_sync_time_from_ntp(CONFIG_CLOCK_NTP_TIMEOUT_MS) == ESP_OK) {
            // clock_sync_time_from_ntp already wrote the (its own, valid)
            // value to the RTC; fill time_to_set from the now-synced system
            // clock so the redundant clock_set_time below does not write an
            // uninitialized stack variable (it was left unfilled on the
            // NTP-success path and logged "Invalid time values").
            if (clock_sntp_get_system_time(&time_to_set) == ESP_OK)
                time_obtained = true;
        }
    }

    // If NTP failed or not enabled, try to get system time
    if (!time_obtained) {
        ESP_LOGI(TAG, "Using system time for RTC initialization");
        ret = clock_sntp_get_system_time(&time_to_set);
        if (ret == ESP_OK) {
            time_obtained = true;
        }
    }

    // Check current RTC time
    ESP_LOGD(TAG, "Reading current time from RTC.");
    clock_time_t current_time;
    ret = clock_get_time(&current_time);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d", current_time.year, current_time.month,
                 current_time.day, current_time.hour, current_time.minute, current_time.second);
    } else {
        ESP_LOGW(TAG, "Failed to read RTC time: %s", esp_err_to_name(ret));
    }

    // Update RTC with synchronized time if available
    if (time_obtained) {
        ESP_LOGI(TAG, "Setting RTC to synchronized time: %04d-%02d-%02d %02d:%02d:%02d", time_to_set.year,
                 time_to_set.month, time_to_set.day, time_to_set.hour, time_to_set.minute, time_to_set.second);

        ret = clock_set_time(&time_to_set);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RTC time updated successfully");
        } else {
            ESP_LOGW(TAG, "Failed to update RTC time: %s", esp_err_to_name(ret));
        }
    }

    // Initialize alarm scheduler from SPIFFS
    ret = clock_alarm_scheduler_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Alarm scheduler initialized, %d rules loaded", clock_alarm_get_rule_count());

        // Get current time and schedule next alarm
        clock_time_t alarm_current_time;
        ret = clock_get_time(&alarm_current_time);
        if (ret == ESP_OK) {
            ret = clock_alarm_schedule_next(&alarm_current_time);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Next alarm scheduled successfully");
            } else {
                ESP_LOGW(TAG, "Failed to schedule next alarm: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to initialize alarm scheduler: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Clock component initialized successfully");
    return ESP_OK;
}

esp_err_t clock_get_time(clock_time_t *out) {
    if (!out || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(CONFIG_BOARD_P4)
    // No external RTC IC: the time base is the system clock (SNTP-synced;
    // the crystal-backed internal RTC timer + NVS epoch backup land in M5).
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    out->year = timeinfo.tm_year + 1900;
    out->month = timeinfo.tm_mon + 1;
    out->day = timeinfo.tm_mday;
    out->hour = timeinfo.tm_hour;
    out->minute = timeinfo.tm_min;
    out->second = timeinfo.tm_sec;
    return ESP_OK;
#else
    pcf85063a_datetime_t pcf_time;
    esp_err_t ret = pcf85063a_get_time_date(&s_rtc_dev, &pcf_time);
    if (ret != ESP_OK) {
        return ret;
    }

    pcf_to_clock_time(&pcf_time, out);
    return ESP_OK;
#endif
}

esp_err_t clock_get_utc_epoch(int64_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    if (!s_initialized)
        return ESP_ERR_INVALID_STATE;

    clock_time_t t;
    esp_err_t ret = clock_get_time(&t);
    if (ret != ESP_OK)
        return ret;

    // Reject implausible dates so callers (moon phase) show nothing rather
    // than a wrong phase. validate_time covers the same ranges the RTC write
    // path uses.
    if (!validate_time(&t))
        return ESP_ERR_INVALID_STATE;

    // The RTC holds local (TZ/DST-adjusted) time; mktime interprets it as
    // local and returns the UTC epoch. The TZ env var is set by
    // clock_sntp_init() during clock_init, before any moon use.
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = t.year - 1900;
    tm.tm_mon = t.month - 1;
    tm.tm_mday = t.day;
    tm.tm_hour = t.hour;
    tm.tm_min = t.minute;
    tm.tm_sec = t.second;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    if (epoch == (time_t)-1)
        return ESP_ERR_INVALID_STATE;

    *out = (int64_t)epoch;
    return ESP_OK;
}

esp_err_t clock_set_time(const clock_time_t *time) {
    if (!time || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!validate_time(time)) {
        ESP_LOGE(TAG, "Invalid time values");
        return ESP_ERR_INVALID_ARG;
    }

#if defined(CONFIG_BOARD_P4)
    // No external RTC IC: the "RTC" is the system clock. mktime interprets
    // the value as local time (TZ/DST-adjusted), which is what settimeofday
    // wants.
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = time->year - 1900;
    tm.tm_mon = time->month - 1;
    tm.tm_mday = time->day;
    tm.tm_hour = time->hour;
    tm.tm_min = time->minute;
    tm.tm_sec = time->second;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    if (epoch == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }
    struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
    return (settimeofday(&tv, NULL) == 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
#else
    pcf85063a_datetime_t pcf_time;
    clock_to_pcf_time(time, &pcf_time);

    return pcf85063a_set_time_date(&s_rtc_dev, pcf_time);
#endif
}
