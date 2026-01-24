#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clock.h"
#include "clock_alarm.h"
#include "clock_interrupt.h"
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
bool s_minute_timer_synced = false;

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

esp_err_t clock_init(const clock_config_t *cfg) {
    if (!cfg) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing clock component");

    // Get I2C master bus handle from BSP
    s_bus_handle = bsp_i2c_get_handle();
    if (!s_bus_handle) {
        ESP_LOGE(TAG, "Failed to get I2C bus handle from BSP");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize PCF85063A using the library
    esp_err_t ret = pcf85063a_init(&s_rtc_dev, s_bus_handle, PCF85063A_ADDRESS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCF85063A: %s", esp_err_to_name(ret));
        return ret;
    }

    // Store IO expander handle from BSP
    s_io_expander = bsp_io_expander_init();
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

    // Mark as initialized before attempting time operations
    s_initialized = true;

    // Handle time initialization
    ESP_LOGD(TAG, "Reading current time from RTC.");
    clock_time_t current_time;
    ret = clock_get_time(&current_time);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d", current_time.year, current_time.month,
                 current_time.day, current_time.hour, current_time.minute, current_time.second);
    } else {
        ESP_LOGE(TAG, "Failed to read RTC time: %s", esp_err_to_name(ret));
    }

    if (cfg->initial_time) {
        ESP_LOGD(TAG, "Setting initial time on RTC.");
        clock_time_t time_to_set;
        time_to_set = *cfg->initial_time;

        ESP_LOGI(TAG, "Setting RTC to: %04d-%02d-%02d %02d:%02d:%02d", time_to_set.year, time_to_set.month,
                 time_to_set.day, time_to_set.hour, time_to_set.minute, time_to_set.second);

        ret = clock_set_time(&time_to_set);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RTC time set successfully");
        } else {
            ESP_LOGW(TAG, "Failed to set RTC time: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
}

esp_err_t clock_get_time(clock_time_t *out) {
    if (!out || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063a_datetime_t pcf_time;
    esp_err_t ret = pcf85063a_get_time_date(&s_rtc_dev, &pcf_time);
    if (ret != ESP_OK) {
        return ret;
    }

    pcf_to_clock_time(&pcf_time, out);
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

    pcf85063a_datetime_t pcf_time;
    clock_to_pcf_time(time, &pcf_time);

    return pcf85063a_set_time_date(&s_rtc_dev, pcf_time);
}
