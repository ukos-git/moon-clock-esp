#include "esp_log.h"
#include "lvgl.h"

#include "clock_cb.h"
#include "clock_interrupt.h"

static const char *TAG = "clock_cb";

// Registered callback for display updates
static clock_display_update_cb_t s_display_update_callback = NULL;

void clock_register_display_update_callback(clock_display_update_cb_t callback) {
    s_display_update_callback = callback;
}

void clock_rtc_interrupt_handler_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "clock_rtc_interrupt_handler_cb called");

    bool display_update_needed = false;
    esp_err_t ret = clock_handle_interrupts(&display_update_needed);

    if (ret == ESP_OK && display_update_needed) {
        ESP_LOGD(TAG, "Display update needed - calling registered callback");
        if (s_display_update_callback != NULL) {
            s_display_update_callback();
        } else {
            ESP_LOGW(TAG, "Display update needed but no callback registered");
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to handle RTC interrupts: %s", esp_err_to_name(ret));
    }
}
