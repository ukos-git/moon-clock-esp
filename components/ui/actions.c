#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "actions.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"
#include "vars.h"

#include "clock.h"
#include "wifi.h"

static const char *TAG = "ui_actions";

// Callback for seconds increment timer
static void seconds_increment_timer_cb(lv_timer_t *timer) { action_increment_clock_seconds(NULL); }

// Callback for UI tick timer
static void ui_tick_timer_cb(lv_timer_t *timer) { ui_tick(); }

// Global reference to UI tick timer for dynamic adjustment
static lv_timer_t *g_ui_tick_timer = NULL;

// Global reference to seconds increment timer
static lv_timer_t *g_seconds_timer = NULL;

// State tracking
static bool g_wifi_deinited = false;
static bool g_idle_timer_active = false;
static bool g_alarm_active = false; // Track if we're showing alarm (for wake callback)

// Configuration with fallback defaults if Kconfig not available
#ifndef CONFIG_UI_LIGHT_SLEEP_TICK_MS
#define CONFIG_UI_LIGHT_SLEEP_TICK_MS 100
#endif

#ifndef CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC
#define CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC 30
#endif

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

    // Re-bake the moon once per calendar day (cheap probe: compares day numbers).
    moon_ui_day_tick();

    // Trigger UI update immediately to reflect the change
    ui_tick();
}

/**
 * @brief Update the alarm display by reading alarm directly from RTC hardware
 */
void action_update_alarm_display(lv_event_t *e) {
    // Read alarm time directly from RTC hardware registers
    clock_time_t alarm_time;
    esp_err_t ret = clock_get_alarm(&alarm_time);

    if (ret == ESP_OK) {
        char alarm_str[16];
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", alarm_time.hour, alarm_time.minute);
        set_var_alarm_label(alarm_str);
        ESP_LOGI(TAG, "Alarm updated from hardware: %s (hour=%d, min=%d)", alarm_str, alarm_time.hour,
                 alarm_time.minute);
    } else {
        ESP_LOGD(TAG, "Failed to read alarm from hardware: %s", esp_err_to_name(ret));
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
 * @brief Handle alarm triggered event
 *
 * Called when RTC alarm goes off.
 */
void action_alarm_triggered(lv_event_t *e) {
    ESP_LOGI(TAG, "Alarm triggered!");

    // Mark alarm as active so wake callback knows we're dismissing alarm
    g_alarm_active = true;

    // Restore active LVGL tick timing (alarm screen is an active state)
    if (g_ui_tick_timer) {
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
        ESP_LOGI(TAG, "UI tick timer restored to 10ms for alarm");
    }

    // Alarm is an active (touch) state: the moon resumes tilt rotation.
    moon_ui_pause(false);

    // Explicitly disable RTC countdown timer (no sleep transitions during alarm)
    esp_err_t ret = clock_timer_disable();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC countdown timer disabled for alarm screen");
    }

    // Keep idle monitoring enabled so touch can trigger wake callback to return to main
    // But reset the timer so alarm screen won't auto-sleep
    clock_idle_reset_timer();
    ESP_LOGI(TAG, "Alarm screen active (touch to dismiss)");

    // Switch to alarm screen
    loadScreen(SCREEN_ID_ALARM);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
}

/**
 * @brief Switch to deep sleep screen.
 *
 * Called when RTC countdown timer expires in light sleep.
 */
void action_switch_to_deep_sleep_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to deep sleep screen");

    // Ensure WiFi is deinitialized
    if (!g_wifi_deinited && wifi_manager_is_connected()) {
        wifi_manager_deinit();
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "WiFi deinitialized for deep sleep");
    }

    // Mark idle timer as inactive (deep sleep state)
    g_idle_timer_active = false;

    // Sleep mode: the moon stays at its current (static) phase.
    moon_ui_pause(true);

    // Explicitly disable RTC countdown timer (no longer needed in deep sleep)
    esp_err_t ret = clock_timer_disable();
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "RTC countdown timer disabled for deep sleep");
    }

    // Load deep sleep screen using EEZ Studio function
    loadScreen(SCREEN_ID_DEEP_SLEEP);

    // Update UI one final time to show current state
    ui_tick();

    // Now pause UI tick timer for deep sleep
    if (g_ui_tick_timer) {
        lv_timer_pause(g_ui_tick_timer);
        ESP_LOGI(TAG, "UI tick timer paused for deep sleep");
    }

    // Pause seconds timer for deep sleep
    if (g_seconds_timer) {
        lv_timer_pause(g_seconds_timer);
        ESP_LOGI(TAG, "Seconds timer paused for deep sleep");
    }
}

/**
 * @brief Switch to main screen.
 *
 * Called when user interacts after idle timeout or from alarm screen.
 * Attempts WiFi initialization and NTP sync. If successful, loads main screen.
 * If WiFi fails, falls back to modem sleep screen (non-WiFi mode).
 */
void action_switch_to_main_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to main screen");

    // If we're dismissing an alarm and WiFi was off, return to modem sleep instead
    if (g_alarm_active && g_wifi_deinited) {
        ESP_LOGI(TAG, "Dismissing alarm from modem sleep - returning to modem sleep screen");
        g_alarm_active = false;
        action_switch_to_modem_sleep_screen(e);
        return;
    }

    // Clear alarm flag if set
    g_alarm_active = false;

    // Create UI tick timer if it doesn't exist yet (first call)
    if (!g_ui_tick_timer) {
        g_ui_tick_timer = lv_timer_create(ui_tick_timer_cb, 10, NULL);
        ESP_LOGI(TAG, "UI tick timer created at 10ms");
    } else {
        // Resume normal UI tick timing
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
        ESP_LOGI(TAG, "UI tick timer resumed at 10ms");
    }

    // Create or resume seconds increment timer
    if (!g_seconds_timer) {
        g_seconds_timer = lv_timer_create(seconds_increment_timer_cb, 1000, NULL);
        ESP_LOGI(TAG, "Seconds timer created");
    } else {
        lv_timer_resume(g_seconds_timer);
        ESP_LOGI(TAG, "Seconds timer resumed");
    }

    // Explicitly disable any active RTC countdown timer
    esp_err_t ret = clock_timer_disable();
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "RTC countdown timer disabled");
    }

    // Re-enable and restart idle monitoring for automatic sleep transitions
    clock_idle_set_enabled(true);
    clock_idle_reset_timer();
    g_idle_timer_active = true;
    ESP_LOGI(TAG, "Idle monitoring enabled - will transition to light sleep after %d seconds",
             CONFIG_CLOCK_IDLE_TIMEOUT_SEC);

    // Normal (active) mode: the moon rotates with tilt ("ball in hand") and
    // re-bakes if the calendar day rolled over while we slept.
    moon_ui_pause(false);
    moon_ui_day_tick();

    // Always start with modem sleep screen first (fast, no WiFi check)
    // This provides immediate UI responsiveness
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
    ESP_LOGI(TAG, "Modem sleep screen loaded (checking WiFi status...)");

    // If WiFi is not connected (either never initialized or was deinitialized), try to connect
    if (!wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "WiFi not connected, attempting to %s...", g_wifi_deinited ? "reconnect" : "connect");
        esp_err_t ret = wifi_manager_init();
        if (ret == ESP_OK) {
            g_wifi_deinited = false;
            ESP_LOGI(TAG, "WiFi connected successfully");
        } else {
            ESP_LOGW(TAG, "WiFi connection failed: %s", esp_err_to_name(ret));
            g_wifi_deinited = true;
        }
    }

    // Check if WiFi is available and upgrade to main screen if so
    if (wifi_manager_is_connected()) {
        g_wifi_deinited = false;
        loadScreen(SCREEN_ID_MAIN);
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
        ESP_LOGI(TAG, "Upgraded to main screen (WiFi active)");
    } else {
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "Staying on modem sleep screen (WiFi not connected)");
    }
}

/**
 * @brief Switch to modem sleep screen (non-WiFi mode).
 *
 * Simple clock display without WiFi. Used when WiFi is not available or needed.
 * Behaves like main screen but without network connectivity.
 */
void action_switch_to_modem_sleep_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to modem sleep screen (non-WiFi mode)");

    // Deinitialize WiFi if it's running
    if (!g_wifi_deinited && wifi_manager_is_connected()) {
        wifi_manager_deinit();
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "WiFi deinitialized for modem sleep");
    }

    // Create UI tick timer if it doesn't exist yet
    if (!g_ui_tick_timer) {
        g_ui_tick_timer = lv_timer_create(ui_tick_timer_cb, 10, NULL);
        ESP_LOGI(TAG, "UI tick timer created at 10ms");
    } else {
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
        ESP_LOGI(TAG, "UI tick timer resumed at 10ms");
    }

    // Create or resume seconds increment timer
    if (!g_seconds_timer) {
        g_seconds_timer = lv_timer_create(seconds_increment_timer_cb, 1000, NULL);
        ESP_LOGI(TAG, "Seconds timer created");
    } else {
        lv_timer_resume(g_seconds_timer);
        ESP_LOGI(TAG, "Seconds timer resumed");
    }

    // Disable any active RTC countdown timer
    esp_err_t ret = clock_timer_disable();
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "RTC countdown timer disabled");
    }

    // Enable idle monitoring for automatic sleep transitions
    clock_idle_set_enabled(true);
    clock_idle_reset_timer();
    g_idle_timer_active = true;
    ESP_LOGI(TAG, "Idle monitoring enabled - will transition to light sleep after %d seconds",
             CONFIG_CLOCK_IDLE_TIMEOUT_SEC);

    // Modem sleep is an active (Airplane) mode: the moon keeps rotating and
    // re-bakes if the calendar day rolled over.
    moon_ui_pause(false);
    moon_ui_day_tick();

    // Load modem sleep screen
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
}

/**
 * @brief Switch to light sleep screen.
 *
 * Called when idle timeout expires on main screen.
 * Deinitializes WiFi, slows down LVGL timer, and starts countdown to deep sleep.
 */
void action_switch_to_light_sleep_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to light sleep screen");

    // Deinitialize WiFi/network to save power (modem sleep)
    if (!g_wifi_deinited && wifi_manager_is_connected()) {
        wifi_manager_deinit();
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "WiFi deinitialized for light sleep");
    }

    // Slow down UI tick timer (from 10ms to configured light sleep period)
    if (g_ui_tick_timer) {
        lv_timer_set_period(g_ui_tick_timer, CONFIG_UI_LIGHT_SLEEP_TICK_MS);
        ESP_LOGI(TAG, "UI tick timer slowed to %dms for light sleep", CONFIG_UI_LIGHT_SLEEP_TICK_MS);
    }

    // Pause seconds timer for light sleep
    if (g_seconds_timer) {
        lv_timer_pause(g_seconds_timer);
        ESP_LOGI(TAG, "Seconds timer paused for light sleep");
    }

    // Disable idle monitoring (we're in sleep state, use countdown timer instead)
    clock_idle_set_enabled(false);
    g_idle_timer_active = false;

    // Start RTC countdown timer for transition to deep sleep
    // Use 1Hz timer with interrupt enabled
    esp_err_t ret =
        clock_timer_enable(CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC, CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC countdown timer started: %d seconds to deep sleep", CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC);
    } else {
        ESP_LOGE(TAG, "Failed to start RTC countdown timer: %s", esp_err_to_name(ret));
    }

    // Sleep mode: the moon shows a static current phase (no tilt rotation).
    moon_ui_pause(true);

    // Load light sleep screen using EEZ Studio function
    loadScreen(SCREEN_ID_LIGHT_SLEEP);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
}

/**
 * @brief Switch to alarm screen.
 *
 * Called when navigating to alarm screen.
 */
void action_switch_to_alarm_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to alarm screen");

    // Mark alarm as active for proper wake callback handling
    g_alarm_active = true;

    // Ensure normal UI operation when viewing alarm (alarm screen is an active state)
    if (g_ui_tick_timer) {
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
        ESP_LOGI(TAG, "UI tick timer set to 10ms for alarm screen");
    }

    // Explicitly disable RTC countdown timer (no automatic sleep during alarm viewing)
    esp_err_t ret = clock_timer_disable();
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "RTC countdown timer disabled for alarm screen");
    }

    // Keep idle monitoring enabled so touch returns to main screen
    // Reset timer to prevent auto-sleep while viewing alarm
    clock_idle_reset_timer();

    // Load alarm screen using EEZ Studio function
    loadScreen(SCREEN_ID_ALARM);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
}
