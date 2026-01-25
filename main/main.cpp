#include "driver/gpio.h"
#include "esp_err.h" // required by bsp until PR goes through
#include "esp_io_expander.h"
#include "esp_log.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "lvgl.h"

#include "actions.h"
#include "ui.h"
#include "vars.h"

#include "clock.h"
#include "wifi.h"

#define RTC_POLL_INTERVAL_MS 1300

static const char *TAG = "main";

static void ui_tick_timer_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "ui_tick_timer_cb called");
    ui_tick();
}

static void lvgl_seconds_timer_cb(lv_timer_t *timer) {
    int32_t current_seconds = get_var_clock_seconds();
    set_var_clock_seconds(current_seconds + 1);
}

extern "C" void app_main(void) {

    // Initialize WiFi first for NTP synchronization
    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi initialized and connected successfully");
    } else {
        ESP_LOGW(TAG, "WiFi initialization failed: %s", esp_err_to_name(wifi_ret));
    }

    // Configure and initialize the clock component with NTP support
    clock_config_t clock_cfg = {
        .enable_ntp_sync = (wifi_ret == ESP_OK) // Enable NTP only if WiFi is available
    };

    esp_err_t ret = clock_init(&clock_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize clock: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Clock component initialized successfully");

        // Initialize minute tick with synchronization to minute boundary
        ret = clock_init_minute_tick();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RTC minute tick initialized and synchronized");
        } else {
            ESP_LOGW(TAG, "Failed to initialize minute tick: %s", esp_err_to_name(ret));
        }
    }

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(-1);

    ui_init();
    ESP_LOGI(TAG, "UI initialized");

    // Update display now that UI objects are created
    clock_update_display_vars();

    bsp_display_unlock();

    // Create timer for UI updates (10ms)
    lv_timer_create(ui_tick_timer_cb, 10, NULL);

    // Create simple LVGL seconds counter (1 second interval)
    lv_timer_create(lvgl_seconds_timer_cb, 1000, NULL);

    // Create polling timer for RTC interrupt handling - debug interval
    // This checks for RTC interrupts and handles timer restarts
    lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS, NULL);

    ESP_LOGI(TAG, "Using RTC interrupts with %.1f second polling.", RTC_POLL_INTERVAL_MS / 1000.0f);

    ESP_LOGI(TAG, "Application started - RTC interrupts with polling backup");
}
