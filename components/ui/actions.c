#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "actions.h"
#include "screens.h"
#include "vars.h"

#include "clock.h"

static const char *TAG = "ui_actions";

/**
 * @brief Update the clock display with current time from RTC
 */
void action_update_clock_display(lv_event_t *e) {
    clock_time_t current_time;
    esp_err_t ret = clock_get_time(&current_time);

    if (ret == ESP_OK) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", current_time.hour, current_time.minute);

        set_var_clock_label(time_str);
        set_var_clock_seconds(current_time.second);
        ESP_LOGD(TAG, "Clock updated: %s", time_str);
    } else {
        ESP_LOGE(TAG, "Failed to get time from RTC: %s", esp_err_to_name(ret));
        set_var_clock_label("--:--");
        set_var_clock_seconds(0);
    }
}

/**
 * @brief Update the alarm display by reading next alarm from RTC
 */
void action_update_alarm_display(lv_event_t *e) {
    // Read the currently set alarm from RTC
    clock_time_t alarm_time;
    esp_err_t ret = clock_get_alarm(&alarm_time);

    if (ret == ESP_OK) {
        char alarm_str[16];
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", alarm_time.hour, alarm_time.minute);
        set_var_alarm_label(alarm_str);
        ESP_LOGD(TAG, "Alarm updated: %s", alarm_str);
    } else {
        ESP_LOGD(TAG, "No alarm set or failed to read alarm: %s", esp_err_to_name(ret));
        set_var_alarm_label("--:--");
    }
}

/**
 * @brief Increment clock seconds display
 */
void action_increment_clock_seconds(lv_event_t *e) {
    int32_t current_seconds = get_var_clock_seconds();
    set_var_clock_seconds(current_seconds + 1);
}
