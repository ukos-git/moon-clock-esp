#include <stdio.h>
#include <string.h>

#include "esp_io_expander.h"
#include "esp_log.h"

#include "clock.h"
#include "clock_interrupt.h"
#include "clock_timer.h"
#include "pcf85063a.h"

static const char *TAG = "clock_interrupt";

// EXIO 3 pin definition (IO expander pin for RTC interrupt)
#define EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern esp_io_expander_handle_t s_io_expander;
extern bool s_initialized;

/**
 * @brief Handle all RTC interrupts in one function
 *
 * This function checks for pending RTC interrupts and processes them appropriately.
 * It handles both timer and alarm interrupts, managing synchronization and clearing flags.
 *
 * @param[out] display_update_needed Set to true if display should be updated
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_handle_interrupts(bool *display_update_needed) {
    if (!display_update_needed || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    *display_update_needed = false;

    // Check if there's a pending interrupt on EXIO 3
    bool interrupt_pending = clock_is_interrupt_pending();
    if (!interrupt_pending) {
        return ESP_OK; // No interrupt to process
    }

    // Get interrupt flags
    clock_interrupt_t ints;
    esp_err_t ret = clock_get_interrupts(&ints);
    if (ret != ESP_OK || ints == CLOCK_INT_NONE) {
        return ret; // No valid interrupts
    }

    ESP_LOGD(TAG, "RTC interrupt detected, ints=0x%x", ints);

    // Handle timer interrupts with proper synchronization
    if (ints & CLOCK_INT_TIMER) {
        bool is_minute_tick = false;
        esp_err_t process_ret = clock_process_minute_interrupt(&is_minute_tick);

        if (process_ret == ESP_OK && is_minute_tick) {
            ESP_LOGD(TAG, "Minute tick processed");
            *display_update_needed = true;
        } else if (process_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to process minute interrupt: %s", esp_err_to_name(process_ret));
            // Fallback: clear timer flag manually
            clock_clear_interrupts(CLOCK_INT_TIMER);
        }
    }

    // Handle alarm interrupts separately
    if (ints & CLOCK_INT_ALARM) {
        ESP_LOGD(TAG, "Alarm interrupt detected");
        *display_update_needed = true;

        // Clear alarm flag
        esp_err_t clear_ret = clock_clear_interrupts(CLOCK_INT_ALARM);
        if (clear_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(clear_ret));
        }
    }

    return ESP_OK;
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
        ESP_LOGI(TAG, "RTC interrupt confirmed - CTRL2=0x%02x, timer=%d, alarm=%d", ctrl2, rtc_timer_flag,
                 rtc_alarm_flag);
        return true;
    } else {
        ESP_LOGD(TAG, "IO interrupt active but not from RTC: CTRL2=0x%02x", ctrl2);
        return false; // Interrupt is from another TCA device, not RTC
    }
}
