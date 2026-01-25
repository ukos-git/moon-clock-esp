#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "clock.h"
#include "clock_cb.h"
#include "clock_idle.h"
#include "clock_interrupt.h"

static const char *TAG = "clock_cb";

// Registered callbacks
static clock_update_cb_t s_time_update_callback = NULL;
static clock_update_cb_t s_alarm_update_callback = NULL;
static clock_update_cb_t s_alarm_trigger_callback = NULL;
static clock_update_cb_t s_idle_timeout_callback = NULL;
static clock_update_cb_t s_idle_wake_callback = NULL;

void clock_register_time_update_callback(clock_update_cb_t callback) {
    s_time_update_callback = callback;
    ESP_LOGI(TAG, "Time update callback registered");
}

void clock_register_alarm_update_callback(clock_update_cb_t callback) {
    s_alarm_update_callback = callback;
    ESP_LOGI(TAG, "Alarm update callback registered");
}

void clock_register_alarm_trigger_callback(clock_update_cb_t callback) {
    s_alarm_trigger_callback = callback;
    ESP_LOGI(TAG, "Alarm trigger callback registered");
}

void clock_register_idle_timeout_callback(clock_update_cb_t callback) {
    s_idle_timeout_callback = callback;
    ESP_LOGI(TAG, "Idle timeout callback registered");
}

void clock_register_idle_wake_callback(clock_update_cb_t callback) {
    s_idle_wake_callback = callback;
    ESP_LOGI(TAG, "Idle wake callback registered");
}

void clock_trigger_time_update(void) {
    if (s_time_update_callback) {
        s_time_update_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Time update triggered but no callback registered.");
    }
}

void clock_trigger_alarm_update(void) {
    if (s_alarm_update_callback) {
        s_alarm_update_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Alarm update triggered but no callback registered.");
    }
}

void clock_trigger_alarm_triggered(void) {
    if (s_alarm_trigger_callback) {
        s_alarm_trigger_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Alarm triggered but no callback registered.");
    }
}
void clock_trigger_idle_timeout(void) {
    if (s_idle_timeout_callback) {
        s_idle_timeout_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Idle timeout but no callback registered");
    }
}

void clock_trigger_idle_wake(void) {
    if (s_idle_wake_callback) {
        s_idle_wake_callback(NULL);
    } else {
        ESP_LOGW(TAG, "Idle wake but no callback registered");
    }
}
void clock_rtc_interrupt_handler_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "clock_rtc_interrupt_handler_cb called");

    esp_err_t ret = clock_handle_interrupts();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to handle RTC interrupts: %s", esp_err_to_name(ret));
    }
}
