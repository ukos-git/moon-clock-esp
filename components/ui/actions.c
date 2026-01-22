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
    ESP_LOGD(TAG, "action_update_clock_display called");

    clock_time_t current_time;
    esp_err_t ret = clock_get_time(&current_time);

    ESP_LOGD(TAG, "clock_get_time returned: %s", esp_err_to_name(ret));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Time from RTC: %04d-%02d-%02d %02d:%02d:%02d", current_time.year, current_time.month,
                 current_time.day, current_time.hour, current_time.minute, current_time.second);

        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", current_time.hour, current_time.minute);

        ESP_LOGD(TAG, "Setting clock label to: %s", time_str);

        // Update the bound variable - eez-studio will automatically update the display
        set_var_clock_label(time_str);
        set_var_clock_seconds(current_time.second);
        ESP_LOGD(TAG, "Clock label variable updated successfully");
    } else {
        ESP_LOGE(TAG, "Failed to get time from RTC: %s", esp_err_to_name(ret));

        // Display error or fallback time
        set_var_clock_label("--:--");
        set_var_clock_seconds(0);
    }
}
