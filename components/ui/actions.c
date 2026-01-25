#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "actions.h"
#include "screens.h"
#include "ui.h"
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
    // Get the cached next scheduled alarm time (not from RTC hardware)
    clock_time_t alarm_time;
    esp_err_t ret = clock_alarm_get_next_scheduled(&alarm_time);

    if (ret == ESP_OK) {
        char alarm_str[16];
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", alarm_time.hour, alarm_time.minute);
        set_var_alarm_label(alarm_str);
        ESP_LOGI(TAG, "Alarm updated: %s (hour=%d, min=%d)", alarm_str, alarm_time.hour, alarm_time.minute);
    } else {
        ESP_LOGD(TAG, "No alarm scheduled: %s", esp_err_to_name(ret));
        set_var_alarm_label("--:--");
    }
}

/**
 * @brief Increment clock seconds variable by 1s.
 */
void action_increment_clock_seconds(lv_event_t *e) {
    int32_t current_seconds = get_var_clock_seconds();
    set_var_clock_seconds(current_seconds + 1);
}

/**
 * @brief Switch to deep sleep screen.
 *
 * Called when idle timeout expires.
 */
void action_switch_to_deep_sleep_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to deep sleep screen");

    // Load deep sleep screen using EEZ Studio function
    loadScreen(SCREEN_ID_DEEP_SLEEP);
}

/**
 * @brief Switch to main screen.
 *
 * Called when user interacts after idle timeout.
 */
void action_switch_to_main_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to main screen");

    // Load main screen using EEZ Studio function
    loadScreen(SCREEN_ID_MAIN);
}

/**
 * @brief Switch to alarm screen.
 *
 * Called when navigating to alarm screen.
 */
void action_switch_to_alarm_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to alarm screen");

    // Load alarm screen using EEZ Studio function
    loadScreen(SCREEN_ID_ALARM);
}
