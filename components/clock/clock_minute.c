#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_minute.h"
#include "pcf85063a.h"

static const char *TAG = "clock_minute";

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern bool s_initialized;

// Track which minute interrupt is currently enabled
static clock_minute_int_type_t s_minute_int_type = CLOCK_MINUTE_INT_NONE;

esp_err_t clock_minute_enable_interrupt(clock_minute_int_type_t type) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Per datasheet: MI/HMI must only be used when MODE = 0 (normal mode)
    // Read Offset register to check/ensure MODE = 0
    uint8_t offset_reg = 0;
    esp_err_t ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_OFFSET_ADDR, &offset_reg, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Offset register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if MODE bit is set (bit 7)
    if (offset_reg & PCF85063A_RTC_OFFSET_MODE) {
        ESP_LOGW(TAG, "Offset register in coarse mode (MODE=1), switching to normal mode (MODE=0) for MI/HMI");
        // Clear MODE bit to set normal mode
        offset_reg &= ~PCF85063A_RTC_OFFSET_MODE;
        uint8_t write_data[2] = {PCF85063A_RTC_OFFSET_ADDR, offset_reg};
        ret = pcf85063a_write_register(&s_rtc_dev, write_data, sizeof(write_data));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set normal mode: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Note: For consistent MI/HMI periods, OFFSET[6:0] should be 00h
    // Log warning if offset is non-zero
    uint8_t offset_value = offset_reg & 0x7F;
    if (offset_value != 0) {
        ESP_LOGW(TAG, "Offset value is 0x%02x (non-zero) - MI/HMI periods may be affected", offset_value);
    }

    // Read current Control_2 register
    uint8_t ctrl2_value = 0;
    ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Control_2 register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Clear MI and HMI bits first
    ctrl2_value &= ~(PCF85063A_RTC_CTRL_2_MI | PCF85063A_RTC_CTRL_2_HMI);

    // Set appropriate bits based on type
    switch (type) {
    case CLOCK_MINUTE_INT_NONE:
        ESP_LOGI(TAG, "Disabling minute interrupts");
        break;

    case CLOCK_MINUTE_INT_HALF:
        ctrl2_value |= PCF85063A_RTC_CTRL_2_HMI;
        ESP_LOGI(TAG, "Enabling half-minute interrupt (every 30 seconds)");
        break;

    case CLOCK_MINUTE_INT_FULL:
        ctrl2_value |= PCF85063A_RTC_CTRL_2_MI;
        ESP_LOGI(TAG, "Enabling minute interrupt (every 60 seconds)");
        break;

    default:
        ESP_LOGE(TAG, "Invalid minute interrupt type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    // Write the updated Control_2 register
    uint8_t write_data[2] = {PCF85063A_RTC_CTRL_2_ADDR, ctrl2_value};
    ret = pcf85063a_write_register(&s_rtc_dev, write_data, sizeof(write_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write Control_2 register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update tracking variable
    s_minute_int_type = type;

    // Verify the register was set correctly
    uint8_t verify_value = 0;
    pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &verify_value, 1);
    ESP_LOGI(TAG, "Control_2 register verification: 0x%02x", verify_value);

    return ESP_OK;
}

esp_err_t clock_minute_disable_interrupt(void) { return clock_minute_enable_interrupt(CLOCK_MINUTE_INT_NONE); }

esp_err_t clock_minute_process_interrupt(bool *is_minute_tick) {
    if (!is_minute_tick || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    *is_minute_tick = false;

    // Check if minute interrupt is enabled
    if (s_minute_int_type == CLOCK_MINUTE_INT_NONE) {
        return ESP_OK; // Not using minute interrupts
    }

    // Check for timer flag (TF)
    uint8_t timer_flag = 0;
    esp_err_t ret = pcf85063a_get_timer_flag(&s_rtc_dev, &timer_flag);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // If timer flag is not set, this is not our interrupt
    if (!timer_flag) {
        return ESP_OK;
    }

    // Clear the timer flag (TF) by reading Control_2 and clearing the bit
    ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(ret));
        return ret;
    }

    // Valid minute tick occurred
    *is_minute_tick = true;

    const char *int_type = (s_minute_int_type == CLOCK_MINUTE_INT_FULL) ? "minute" : "half-minute";
    ESP_LOGD(TAG, "Cleared TF flag for %s interrupt", int_type);

    return ESP_OK;
}

clock_minute_int_type_t clock_minute_get_enabled_type(void) { return s_minute_int_type; }
