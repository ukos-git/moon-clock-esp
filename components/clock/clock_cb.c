#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "clock.h"
#include "clock_cb.h"
#include "clock_interrupt.h"
#include "vars.h"

static const char *TAG = "clock_cb";

void clock_update_display_vars(void) {
    clock_time_t current_time;
    esp_err_t ret = clock_get_time(&current_time);

    if (ret == ESP_OK) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", current_time.hour, current_time.minute);
        set_var_clock_label(time_str);
        set_var_clock_seconds(current_time.second);
        ESP_LOGD(TAG, "Display updated: %s", time_str);
    } else {
        ESP_LOGE(TAG, "Failed to get time from RTC: %s", esp_err_to_name(ret));
        set_var_clock_label("--:--");
        set_var_clock_seconds(0);
    }
}

void clock_update_alarm_label(int hour, int minute) {
    char alarm_str[16];
    snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", hour, minute);
    set_var_alarm_label(alarm_str);
    ESP_LOGI(TAG, "Alarm label updated: %s", alarm_str);
}

void clock_rtc_interrupt_handler_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "clock_rtc_interrupt_handler_cb called");

    bool display_update_needed = false;
    esp_err_t ret = clock_handle_interrupts(&display_update_needed);

    if (ret == ESP_OK && display_update_needed) {
        ESP_LOGD(TAG, "Display update needed - updating UI variables");
        clock_update_display_vars();
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to handle RTC interrupts: %s", esp_err_to_name(ret));
    }
}
