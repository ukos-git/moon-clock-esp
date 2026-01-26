#include "driver/gpio.h"
#include "esp_err.h" // required by bsp until PR goes through
#include "esp_io_expander.h"
#include "esp_log.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "lvgl.h"

#include "actions.h"
#include "ui.h"

#include "clock.h"
#include "wifi.h"

#define RTC_POLL_INTERVAL_MS 1300

static const char *TAG = "main";

static void ui_tick_timer_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "ui_tick_timer_cb called");
    ui_tick();
}

static void seconds_increment_timer_cb(lv_timer_t *timer) { action_increment_clock_seconds(NULL); }

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

        // Set polling interval for countdown timer expiration detection
        clock_set_poll_interval_ms(RTC_POLL_INTERVAL_MS);

        // Enable minute interrupt for automatic time updates
        ret = clock_minute_enable_interrupt(CLOCK_MINUTE_INT_FULL);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "RTC minute interrupt (MI) enabled");
        } else {
            ESP_LOGW(TAG, "Failed to enable minute interrupt: %s", esp_err_to_name(ret));
        }
    }

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(-1);

    ui_init();
    ESP_LOGI(TAG, "UI initialized");

    // Create UI tick timer (10ms) and save reference for dynamic adjustment
    lv_timer_t *ui_tick_timer = lv_timer_create(ui_tick_timer_cb, 10, NULL);

    // Pass timer reference to actions for sleep state management
    action_set_ui_tick_timer(ui_tick_timer);

    // Register clock component callbacks for UI updates
    // Cast action functions to callback type (they accept NULL as lv_event_t*)
    clock_register_time_update_callback((clock_update_cb_t)action_update_clock_display);
    clock_register_alarm_update_callback((clock_update_cb_t)action_update_alarm_display);
    clock_register_alarm_trigger_callback((clock_update_cb_t)action_alarm_triggered);

    clock_trigger_time_update();
    clock_trigger_alarm_update();

    // Register idle timer callbacks for screen switching
    // Idle timeout goes to LIGHT SLEEP (not deep sleep directly)
    clock_register_idle_timeout_callback((clock_update_cb_t)action_switch_to_light_sleep_screen);
    clock_register_idle_wake_callback((clock_update_cb_t)action_switch_to_main_screen);

    // Register countdown timer callback for transition from light sleep to deep sleep
    clock_register_countdown_timer_callback((clock_update_cb_t)action_switch_to_deep_sleep_screen);

    // Initialize idle timer system (sets up infrastructure, but doesn't start the timer yet)
    ret = clock_idle_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Idle timer system initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize idle timer: %s", esp_err_to_name(ret));
    }

    bsp_display_unlock();

    // Now that UI is initialized and on main screen, explicitly start the idle timer
    // This also sets the g_idle_timer_active flag in actions.c
    action_switch_to_main_screen(NULL);
    ESP_LOGI(TAG, "Main screen loaded with idle timer active");

    // Create seconds increment timer (1 second interval)
    lv_timer_create(seconds_increment_timer_cb, 1000, NULL);

    // Create polling timer for RTC interrupt handling - debug interval
    // This checks for RTC interrupts and handles timer restarts
    lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS, NULL);

    // Create idle timer check (configured via Kconfig)
    lv_timer_create(clock_idle_check_cb, CONFIG_CLOCK_IDLE_CHECK_INTERVAL_MS, NULL);

    ESP_LOGI(TAG, "Using RTC interrupts with %.1f second polling.", RTC_POLL_INTERVAL_MS / 1000.0f);
    ESP_LOGI(TAG, "Sleep transitions: MAIN -(idle)-> LIGHT_SLEEP -(countdown)-> DEEP_SLEEP");
    ESP_LOGI(TAG, "Idle timeout: %d seconds (configurable via Kconfig)", CONFIG_CLOCK_IDLE_TIMEOUT_SEC);
    ESP_LOGI(TAG, "LVGL timings: MAIN=10ms, LIGHT_SLEEP=configurable, DEEP_SLEEP=paused");
    ESP_LOGI(TAG, "Alarm screen: Active state with no automatic sleep transitions");

    ESP_LOGI(TAG, "Application started - RTC interrupts with polling backup");
}
