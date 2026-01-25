#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_alarm.h"
#include "pcf85063a.h"

static const char *TAG = "clock_alarm";

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern bool s_initialized;

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

esp_err_t clock_set_alarm(const clock_time_t *time) {
    if (!time || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (time->hour < 0 || time->hour > 23 || time->minute < 0 || time->minute > 59) {
        ESP_LOGE(TAG, "Invalid alarm time");
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063a_datetime_t pcf_alarm_time;
    clock_to_pcf_time(time, &pcf_alarm_time);

    return pcf85063a_set_alarm(&s_rtc_dev, pcf_alarm_time);
}

esp_err_t clock_get_alarm(clock_time_t *time) {
    if (!time || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    pcf85063a_datetime_t pcf_alarm_time;
    esp_err_t ret = pcf85063a_get_alarm(&s_rtc_dev, &pcf_alarm_time);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert PCF format to clock format (only hour and minute are relevant for alarm)
    time->hour = pcf_alarm_time.hour;
    time->minute = pcf_alarm_time.min;
    time->second = pcf_alarm_time.sec;
    time->year = 0;
    time->month = 0;
    time->day = 0;

    return ESP_OK;
}

esp_err_t clock_enable_alarm(bool enable) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        return pcf85063a_enable_alarm(&s_rtc_dev);
    } else {
        // Disable alarm interrupt by clearing AIE bit
        uint8_t ctrl2;
        esp_err_t ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
        if (ret != ESP_OK) {
            return ret;
        }

        ctrl2 &= ~PCF85063A_RTC_CTRL_2_AIE;
        uint8_t write_buf[2] = {PCF85063A_RTC_CTRL_2_ADDR, ctrl2};
        return pcf85063a_write_register(&s_rtc_dev, write_buf, 2);
    }
}

esp_err_t clock_clear_alarm(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Disable alarm interrupt
    esp_err_t ret = clock_enable_alarm(false);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear alarm registers by writing 0 (disable alarm enable bits)
    uint8_t disabled_alarm = 0;
    uint8_t write_buf1[2] = {PCF85063A_RTC_MINUTE_ALARM, disabled_alarm};
    ret = pcf85063a_write_register(&s_rtc_dev, write_buf1, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t write_buf2[2] = {PCF85063A_RTC_HOUR_ALARM, disabled_alarm};
    return pcf85063a_write_register(&s_rtc_dev, write_buf2, 2);
}
