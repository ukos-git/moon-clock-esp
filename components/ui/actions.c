#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bsp/display.h"

#include "actions.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"
#include "vars.h"

#include "clock.h"
#include "clock_idle.h"
#include "wifi.h"

static const char *TAG = "ui_actions";

// Callback for seconds increment timer
static void seconds_increment_timer_cb(lv_timer_t *timer) { action_increment_clock_seconds(NULL); }

// State tracking
static bool g_wifi_deinited = false;
static bool g_idle_timer_active = false;
static bool g_alarm_active = false; // Track if we're showing alarm (for wake callback)

// True while the panel backlight is off (deep sleep). Deep sleep is the
// only state that turns the backlight off -- the light-sleep screens
// keep the panel lit (the dusk + the dark side with the clock show
// there). The wake handler and the alarm trigger restore it.
static bool g_backlight_off = false;

// True while the device sits on a sleep screen (light/deep sleep) or on
// the alarm screen. The sleep transitions and the alarm trigger set it;
// the wake handler (action_switch_to_main_screen) consumes (clears) it
// to know that its call is a real wake event -- which keeps a stale
// alarm (one that rang while the user slept) from hijacking the wake
// flow. It no longer drives an animation: wakes (INCLUDING a dismissed
// alarm) show the current moon phase static, no synodic sweep (the
// sweep plays at BOOT only).
static bool g_asleep = false;

/* WiFi connect/reconnect runs on a dedicated task, never on the LVGL task:
 * the wake handler (action_switch_to_main_screen) runs in the LVGL task
 * (indev event / moon tilt timer), and the old blocking
 * wifi_manager_init() there parked the LVGL task for the whole ~1.25 s
 * WPA2 handshake -- no LVGL timer fired, so the moon spin and the clock
 * were frozen on the modem-sleep screen until the connection landed.
 * The display now stays live during the handshake; the main-screen
 * upgrade is picked up by the UI tick below (which runs on the LVGL
 * task) once the connection completes. */
static volatile bool s_wifi_connecting = false;
static volatile bool s_wifi_connect_ok = false;

static void wifi_reconnect_task_fn(void *arg) {
    ESP_LOGI(TAG, "WiFi connect in background task...");
    esp_err_t ret = wifi_manager_init();
    s_wifi_connect_ok = (ret == ESP_OK);
    s_wifi_connecting = false;
    if (ret == ESP_OK) {
        g_wifi_deinited = false;
        ESP_LOGI(TAG, "WiFi connected successfully (background)");
    } else {
        g_wifi_deinited = true;
        ESP_LOGW(TAG, "WiFi connection failed: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

static void wifi_reconnect_kickoff(void) {
    if (s_wifi_connecting) {
        ESP_LOGI(TAG, "WiFi connect already in progress");
        return;
    }
    s_wifi_connecting = true;
    s_wifi_connect_ok = false;
    if (xTaskCreate(wifi_reconnect_task_fn, "wifi_reconnect", 4096, NULL, 10, NULL) != pdPASS) {
        s_wifi_connecting = false;
        ESP_LOGW(TAG, "WiFi reconnect task creation failed");
    }
}

// Callback for UI tick timer
static void ui_tick_timer_cb(lv_timer_t *timer) {
    // Finished background WiFi connect: upgrade to the main screen from
    // here (the LVGL task; loadScreen touches LVGL state). Deferred to the
    // tick so the handshake never blocks the display. Only while awake:
    // if the device re-slept during the handshake, drop the upgrade and
    // let the next wake re-trigger the switch.
    if (!s_wifi_connecting && s_wifi_connect_ok) {
        s_wifi_connect_ok = false;
        if (!clock_is_idle()) {
            g_wifi_deinited = false;
            loadScreen(SCREEN_ID_MAIN);
            ui_tick();
            ESP_LOGI(TAG, "Upgraded to main screen (WiFi active)");
        } else {
            ESP_LOGI(TAG, "WiFi connected but device is asleep; main screen deferred");
        }
    }
    ui_tick();
}

// Global reference to UI tick timer for dynamic adjustment
static lv_timer_t *g_ui_tick_timer = NULL;

// Global reference to seconds increment timer
static lv_timer_t *g_seconds_timer = NULL;

// Configuration with fallback defaults if Kconfig not available
#ifndef CONFIG_UI_LIGHT_SLEEP_TICK_MS
#define CONFIG_UI_LIGHT_SLEEP_TICK_MS 100
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

    // An alarm can fire from DEEP SLEEP (the panel backlight is off
    // there): light the panel before the alarm screen is shown.
    if (g_backlight_off) {
        if (bsp_display_backlight_on() == ESP_OK) {
            g_backlight_off = false;
            ESP_LOGI(TAG, "Alarm: panel backlight restored");
        } else {
            ESP_LOGW(TAG, "Failed to restore the panel backlight for the alarm screen");
        }
    }

    // Mark alarm as active so wake callback knows we're dismissing alarm
    g_alarm_active = true;

    // Mark the alarm screen as a wake-worthy state (like the sleep
    // screens): dismissing it (touch) is a wake event. (If the alarm
    // rang while the device was already asleep, g_asleep is already
    // set by the sleep transition and the normal wake flow handles
    // it.) Wakes -- including a dismissed alarm -- show the current
    // moon phase static, no animation.
    g_asleep = true;

    // Restore active LVGL tick timing (alarm screen is an active state)
    if (g_ui_tick_timer) {
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
        ESP_LOGI(TAG, "UI tick timer restored to 10ms for alarm");
    }

    // The alarm screen has no moon image: pause the moon entirely (tick +
    // orbit re-bake), otherwise the orbit pipeline keeps burning ~90% CPU
    // baking viewports for a screen that shows none of them.
    moon_ui_pause(true);

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

/* The alarm screen can only be dismissed by touch, but the idle-wake
 * path (touch -> action_switch_to_main_screen) only fires while the
 * device is IN idle mode; while awake, a tap just resets the idle
 * timer, so tapping to dismiss never left the screen (and never let the
 * device sleep into a dismissable state) -- a one-way door. Register a
 * direct input-device callback instead: it fires on every press
 * regardless of the hit-tested object, and while the alarm screen is
 * shown (g_alarm_active) and the device is awake it dismisses it. The
 * asleep path is untouched: the idle-wake callback (registered earlier,
 * so it runs first) wakes the device and clears g_alarm_active before
 * this callback sees the same press, so the switch happens once. */
static void alarm_indev_read_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_target(e);
    if (lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED)
        return;
    if (g_alarm_active && !clock_is_idle()) {
        ESP_LOGI(TAG, "Alarm dismissed by touch (device awake)");
        action_switch_to_main_screen(e);
    }
}

esp_err_t action_register_alarm_touch(void) {
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) {
        ESP_LOGE(TAG, "no input device; alarm touch dismiss unavailable");
        return ESP_FAIL;
    }
    lv_indev_add_event_cb(indev, alarm_indev_read_cb, LV_EVENT_PRESSED, NULL);
    ESP_LOGI(TAG, "Alarm touch dismiss registered");
    return ESP_OK;
}

/**
 * @brief Switch to deep sleep screen.
 *
 * Called when RTC countdown timer expires in light sleep.
 */
void action_switch_to_deep_sleep_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to deep sleep screen");

    // The wake handler reads this to know its call is a real wake.
    g_asleep = true;

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

    // Update UI one final time to show current state (the dark side
    // from the completed dusk, with the clock, one last atomic flush)
    ui_tick();

    // Deep sleep turns the panel backlight OFF (the sleep screens
    // before it kept it on). A wake (touch, tilt or an alarm) restores
    // it (action_switch_to_main_screen / action_alarm_triggered).
    if (!g_backlight_off) {
        if (bsp_display_backlight_off() == ESP_OK) {
            g_backlight_off = true;
            ESP_LOGI(TAG, "Deep sleep: panel backlight off");
        } else {
            ESP_LOGW(TAG, "Failed to turn the panel backlight off");
        }
    }

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

    // A wake from deep sleep lights the panel again (the deep-sleep
    // transition turned the backlight off).
    if (g_backlight_off) {
        if (bsp_display_backlight_on() == ESP_OK) {
            g_backlight_off = false;
            ESP_LOGI(TAG, "Wake: panel backlight restored");
        } else {
            ESP_LOGW(TAG, "Failed to restore the panel backlight");
        }
    }

    // Was this call a real wake from sleep? The sleep transitions set
    // g_asleep; this handler consumes it. It drives the stale-alarm
    // branch below. Boot and the awake alarm-dismiss touch path see
    // false.
    bool was_asleep = g_asleep;
    g_asleep = false;

    // If we're dismissing an alarm and WiFi was off, return to modem
    // sleep instead -- but only when the user was actually AT the
    // alarm (awake). A stale alarm (rang while the user slept, so the
    // user never saw it) must not hijack the wake: it follows the
    // normal wake flow below (static current phase, no animation).
    if (g_alarm_active && g_wifi_deinited && !was_asleep) {
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

    // Normal (active) mode: the moon resumes its free spin from the
    // exact pose it parked at sleep (no snap) and re-bakes if the
    // calendar day rolled over while we slept. Wakes show the current
    // phase STATIC -- no synodic animation on wake (the sweep is the
    // boot animation only; after a dusk the wake pose display in
    // moon_ui_pause(false) replaces the dark side atomically).
    moon_ui_pause(false);
    moon_ui_day_tick();

    // Always start with modem sleep screen first (fast, no WiFi check)
    // This provides immediate UI responsiveness
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
    ESP_LOGI(TAG, "Modem sleep screen loaded (checking WiFi status...)");

    // Already connected (boot path: app_main connected before the display
    // came up): upgrade straight to the main screen.
    if (wifi_manager_is_connected()) {
        g_wifi_deinited = false;
        loadScreen(SCREEN_ID_MAIN);
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(10)); // Allow IDLE task to run and reset watchdog
        ESP_LOGI(TAG, "Upgraded to main screen (WiFi active)");
    } else {
        // Not connected: stay on the modem sleep screen and reconnect in
        // the background. wifi_manager_init() blocks for the whole ~1.25 s
        // WPA2 handshake; running it here would freeze the display, because
        // this handler runs on the LVGL task and the handshake would park
        // it with no timers firing. The UI tick picks up the finished
        // connect and upgrades the screen while the display stays live.
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "WiFi not connected, reconnecting in background (display stays live)");
        wifi_reconnect_kickoff();
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

    // The wake handler reads this to know its call is a real wake.
    g_asleep = true;

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

    // Sleep mode: the moon parks at its exact spin pose (no tilt
    // rotation), then the light-sleep "dusk" spins it about its pole
    // to the fully unilluminated side (1 degree/frame). The RTC
    // countdown for the deep-sleep transition is armed by the moon
    // tick on the dusk's LAST frame -- the sleep depth follows the
    // animation, never a fixed timer at the sleep entry. Fallback:
    // when the dusk cannot start (no static phase yet, pipeline in
    // flight, dark side already on screen), arm the countdown right
    // away so the deep-sleep transition still happens.
    moon_ui_pause(true);
    if (!moon_ui_dusk_start()) {
        esp_err_t ret =
            clock_timer_enable(CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC, CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT);
        if (ret == ESP_OK)
            ESP_LOGI(TAG, "dusk unavailable; RTC countdown %d s to deep sleep armed immediately",
                     CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC);
        else
            ESP_LOGE(TAG, "Failed to start RTC countdown timer: %s", esp_err_to_name(ret));
    }

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
