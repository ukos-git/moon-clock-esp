#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "clock.h"
#include "clock_cb.h"
#include "clock_interrupt.h"

static const char *TAG = "clock_cb";

// Registered callbacks
static clock_update_cb_t s_time_update_callback = NULL;
static clock_update_cb_t s_alarm_update_callback = NULL;

void clock_register_time_update_callback(clock_update_cb_t callback) {
    s_time_update_callback = callback;
    ESP_LOGI(TAG, "Time update callback registered");
}

void clock_register_alarm_update_callback(clock_update_cb_t callback) {
    s_alarm_update_callback = callback;
    ESP_LOGI(TAG, "Alarm update callback registered");
}

void clock_trigger_time_update(void) {
    if (s_time_update_callback) {
        s_time_update_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Time update triggered but no callback registered");
    }
}

void clock_trigger_alarm_update(void) {
    if (s_alarm_update_callback) {
        s_alarm_update_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Alarm update triggered but no callback registered");
    }
}

void clock_rtc_interrupt_handler_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "clock_rtc_interrupt_handler_cb called");

    bool display_update_needed = false;
    esp_err_t ret = clock_handle_interrupts(&display_update_needed);

    if (ret == ESP_OK && display_update_needed) {
        ESP_LOGD(TAG, "Display update needed - triggering callback");
        clock_trigger_time_update();
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to handle RTC interrupts: %s", esp_err_to_name(ret));
    }
}
