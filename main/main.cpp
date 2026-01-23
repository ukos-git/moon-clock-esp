#include "driver/gpio.h"
#include "esp_err.h" // required by bsp until PR goes through
#include "esp_log.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "lvgl.h"

#include "actions.h"
#include "clock.h"
#include "esp_io_expander.h"
#include "ui.h"
#include "vars.h"

static const char *TAG = "main";

static void ui_tick_timer_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "ui_tick_timer_cb called");
    ui_tick();
}

static void lvgl_seconds_timer_cb(lv_timer_t *timer) {
    int32_t current_seconds = get_var_clock_seconds();
    set_var_clock_seconds(current_seconds + 1);
}

// Use prime number (1.3 seconds) for debugging - will align with minute interrupts
#define RTC_POLL_INTERVAL_MS 1300

static void rtc_interrupt_handler_cb(lv_timer_t *timer) {
    ESP_LOGD(TAG, "rtc_interrupt_handler_cb called");

    // Check if there's a pending interrupt on EXIO 3
    bool interrupt_pending = clock_is_interrupt_pending();

    ESP_LOGD(TAG, "Interrupt pending: %s", interrupt_pending ? "YES" : "NO");

    if (interrupt_pending) {
        clock_interrupt_t ints;
        esp_err_t ret = clock_get_interrupts(&ints);

        if (ret == ESP_OK && ints != CLOCK_INT_NONE) {
            ESP_LOGD(TAG, "RTC interrupt detected, ints=0x%x", ints);

            // Handle timer interrupts with proper synchronization
            if (ints & CLOCK_INT_TIMER) {
                bool is_minute_tick = false;
                esp_err_t process_ret = clock_process_minute_interrupt(&is_minute_tick);

                if (process_ret == ESP_OK && is_minute_tick) {
                    ESP_LOGD(TAG, "Minute tick processed - updating display");
                    action_update_clock_display(NULL);
                } else if (process_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to process minute interrupt: %s", esp_err_to_name(process_ret));
                    // Fallback: clear timer flag manually
                    clock_clear_interrupts(CLOCK_INT_TIMER);
                }
            }

            // Handle alarm interrupts separately
            if (ints & CLOCK_INT_ALARM) {
                ESP_LOGD(TAG, "Alarm interrupt detected");
                action_update_clock_display(NULL);

                // Clear alarm flag
                esp_err_t clear_ret = clock_clear_interrupts(CLOCK_INT_ALARM);
                if (clear_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(clear_ret));
                }
            }
        }
    }
}

extern "C" void app_main(void) {

    // Configure and initialize the clock component (BSP handles I2C)
    // Clock component will handle time initialization automatically
    clock_config_t clock_cfg = {
        .i2c_freq_hz = 400000, // 400kHz I2C frequency (for reference)
        .initial_time = NULL   // Use system time
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
    action_update_clock_display(NULL);

    bsp_display_unlock();

    // Create timer for UI updates (10ms)
    lv_timer_create(ui_tick_timer_cb, 10, NULL);

    // Create simple LVGL seconds counter (1 second interval)
    lv_timer_create(lvgl_seconds_timer_cb, 1000, NULL);

    // Create polling timer for RTC interrupt handling - debug interval
    // This checks for RTC interrupts and handles timer restarts
    lv_timer_create(rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS, NULL);

    ESP_LOGI(TAG, "Using RTC interrupts with %.1f second polling.", RTC_POLL_INTERVAL_MS / 1000.0f);

    ESP_LOGI(TAG, "Application started - RTC interrupts with polling backup");
}
