#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clock.h"
#include "pcf85063a.h"

static const char *TAG = "clock_component";

// EXIO 3 pin definition (IO expander pin for RTC interrupt)
#define EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3

// Internal state
static pcf85063a_dev_t s_rtc_dev;
static i2c_master_bus_handle_t s_bus_handle = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;
static bool s_initialized = false;
static bool s_minute_timer_synced = false;

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

esp_err_t clock_init_minute_tick(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing synchronized minute tick timer");

    // Step 2: Read the current RTC time
    pcf85063a_datetime_t now;
    esp_err_t ret = pcf85063a_get_time_date(&s_rtc_dev, &now);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC time for sync: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Current time: %02d:%02d:%02d", now.hour, now.min, now.sec);

    // Step 3: Calculate seconds until the next minute boundary
    uint8_t sync_delay;
    if (now.sec == 0) {
        sync_delay = 60;
    } else {
        sync_delay = 60 - now.sec;
    }

    ESP_LOGI(TAG, "Synchronizing to minute boundary in %d seconds", sync_delay);

    // Reset sync state for new synchronization
    s_minute_timer_synced = false;

    // Use the common timer function with calculated sync delay
    ret = clock_enable_minute_tick(sync_delay);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Synchronization timer started - first tick in %d seconds", sync_delay);
    }

    return ret;
}

esp_err_t clock_sync_minute_timer(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_minute_timer_synced) {
        // Already synchronized, nothing to do
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Switching to repeating 60-second timer after synchronization");
    s_minute_timer_synced = true;

    // Step 7: Switch to a repeating 60-second timer
    uint8_t timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ | PCF85063A_RTC_TIMER_MODE_TIE;
    esp_err_t ret = pcf85063a_set_timer_safe(&s_rtc_dev, 60, timer_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set repeating timer: %s", esp_err_to_name(ret));
        s_minute_timer_synced = false; // Reset on failure
        return ret;
    }

    ESP_LOGI(TAG, "Repeating 60-second timer enabled - synchronized to minute boundaries");
    return ESP_OK;
}

esp_err_t clock_enable_minute_tick(uint8_t timer_seconds) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timer_seconds == 0) {
        // Disable timer and reset sync state
        esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Timer disabled");
        }

        // Reset sync state when disabling
        s_minute_timer_synced = false;

        return ret;
    }

    // Enable timer with specified duration
    const char *timer_type = (timer_seconds == 60) ? "basic 60-second" : "synchronized";
    ESP_LOGI(TAG, "Enabling %s timer (%d seconds)", timer_type, timer_seconds);

    // Step 4: Reset the RTC timer state
    esp_err_t ret = pcf85063a_disable_timer(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure timer: 1Hz clock + timer interrupt enable
    uint8_t timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ | PCF85063A_RTC_TIMER_MODE_TIE;
    ret = pcf85063a_set_timer_safe(&s_rtc_dev, timer_seconds, timer_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "%d-second timer enabled", timer_seconds);

    // Verify timer was set correctly
    uint8_t verify_val, verify_mode;
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_VAL, &verify_val, 1);
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_MODE, &verify_mode, 1);
    ESP_LOGI(TAG, "Timer verification - Value: %d, Mode: 0x%02x", verify_val, verify_mode);

    return ESP_OK;
}

/**
 * @brief Process minute tick timer interrupt
 *
 * This function should be called from the timer interrupt handler.
 * It implements steps 6-8 from the synchronization instructions:
 * - Verifies timer flag is set
 * - Clears the flag
 * - Switches to repeating timer after first interrupt
 * - Returns whether this was a valid minute tick
 *
 * @param[out] is_minute_tick Set to true if this is a valid minute tick
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_process_minute_interrupt(bool *is_minute_tick) {
    if (!is_minute_tick || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    *is_minute_tick = false;

    // Step 6: Handle the timer interrupt
    uint8_t timer_flag = 0;
    esp_err_t ret = pcf85063a_get_timer_flag(&s_rtc_dev, &timer_flag);
    if (ret != ESP_OK || !timer_flag) {
        // Not a timer interrupt or error reading flag
        return ret;
    }

    // Clear the timer flag immediately
    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 7: Switch to repeating timer on first interrupt (one time only)
    if (!s_minute_timer_synced) {
        ESP_LOGI(TAG, "First synchronized minute tick - switching to repeating timer");
        ret = clock_sync_minute_timer();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // Step 8 & 9: Valid minute tick - optionally verify timing
    *is_minute_tick = true;

    // Optional: Verify correct alignment for debugging
    clock_time_t current_time;
    ret = clock_get_time(&current_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Minute tick at %02d:%02d:%02d", current_time.hour, current_time.minute, current_time.second);

        if (current_time.second != 0) {
            ESP_LOGW(TAG, "Minute tick not aligned! Second = %d", current_time.second);
        }
    }

    return ESP_OK;
}

/**
 * @brief Check if minute timer is synchronized to boundaries
 *
 * @return true if timer is synchronized to minute boundaries, false otherwise
 */
bool clock_is_minute_timer_synced(void) { return s_initialized && s_minute_timer_synced; }

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

esp_err_t clock_get_interrupts(clock_interrupt_t *out) {
    if (!out || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t ctrl2;
    esp_err_t ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    *out = CLOCK_INT_NONE;

    // Check for timer interrupt flag (TF)
    if (ctrl2 & PCF85063A_RTC_TIMER_FLAG) {
        *out |= CLOCK_INT_TIMER;
    }

    // Check for alarm interrupt flag (AF)
    if (ctrl2 & PCF85063A_RTC_CTRL_2_AF) {
        *out |= CLOCK_INT_ALARM;
    }

    return ESP_OK;
}

esp_err_t clock_clear_interrupts(clock_interrupt_t flags) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Clearing interrupt flags: 0x%x", flags);
    esp_err_t ret = ESP_OK;

    // Clear specified flags using proper functions
    if (flags & CLOCK_INT_TIMER) {
        ESP_LOGD(TAG, "Clearing TIMER flag");
        esp_err_t timer_ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
        if (timer_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(timer_ret));
            ret = timer_ret;
        }
    }

    if (flags & CLOCK_INT_ALARM) {
        ESP_LOGD(TAG, "Clearing ALARM flag");
        uint8_t ctrl2;
        esp_err_t alarm_ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
        if (alarm_ret == ESP_OK) {
            ctrl2 &= ~PCF85063A_RTC_CTRL_2_AF;
            uint8_t write_buf[2] = {PCF85063A_RTC_CTRL_2_ADDR, ctrl2};
            alarm_ret = pcf85063a_write_register(&s_rtc_dev, write_buf, 2);
        }
        if (alarm_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(alarm_ret));
            ret = alarm_ret;
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Successfully cleared interrupt flags");
    }

    return ret;
}

/**
 * @brief Check if RTC interrupt is active and has actual interrupt flags set
 *
 * This function checks both the IO expander pin AND the RTC's internal interrupt flags
 * to ensure we only process genuine RTC interrupts, not other devices on the TCA.
 *
 * @return true if RTC has pending timer/alarm interrupts, false otherwise
 */
bool clock_is_interrupt_pending(void) {
    if (!s_initialized || !s_io_expander) {
        return false;
    }

    // First check if the IO expander interrupt pin is active
    uint32_t level_mask = 0;
    esp_err_t ret = esp_io_expander_get_level(s_io_expander, EXIO_RTC_INT_PIN, &level_mask);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read EXIO 3 level: %s", esp_err_to_name(ret));
        return false;
    }

    // RTC interrupt is active low - if high, no interrupt from any TCA device
    bool io_interrupt_active = (level_mask & EXIO_RTC_INT_PIN) == 0;
    if (!io_interrupt_active) {
        return false; // No interrupt at all
    }

    // IO expander interrupt is active, but it could be from another device
    // Check if the RTC specifically has interrupt flags set
    uint8_t ctrl2;
    ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC CTRL2 for interrupt check: %s", esp_err_to_name(ret));
        return false;
    }

    // Check if RTC has any actual interrupt flags set
    bool rtc_timer_flag = (ctrl2 & PCF85063A_RTC_TIMER_FLAG) != 0;
    bool rtc_alarm_flag = (ctrl2 & PCF85063A_RTC_CTRL_2_AF) != 0;

    if (rtc_timer_flag || rtc_alarm_flag) {
        // Get current time to check if interrupt timing is correct
        clock_time_t current_time;
        esp_err_t time_ret = clock_get_time(&current_time);
        if (time_ret == ESP_OK) {
            ESP_LOGI(TAG, "RTC interrupt confirmed at %02d:%02d:%02d - CTRL2=0x%02x, timer=%d, alarm=%d",
                     current_time.hour, current_time.minute, current_time.second, ctrl2, rtc_timer_flag,
                     rtc_alarm_flag);
        } else {
            ESP_LOGI(TAG, "RTC interrupt confirmed - CTRL2=0x%02x, timer=%d, alarm=%d (time read failed)", ctrl2,
                     rtc_timer_flag, rtc_alarm_flag);
        }
        return true;
    } else {
        ESP_LOGD(TAG, "IO interrupt active but not from RTC: CTRL2=0x%02x", ctrl2);
        return false; // Interrupt is from another TCA device, not RTC
    }
}
