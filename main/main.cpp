#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h" // required by bsp until PR goes through
#include "esp_io_expander.h"
#include "esp_log.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "lvgl.h"

#include "actions.h"
#include "imu.h"
#include "moon_ui.h"
#include "ui.h"

#include "clock.h"
#include "clock_alarm_scheduler.h"
#include "clock_idle.h"
#include "settings_store.h"
#include "wifi.h"

#define RTC_POLL_INTERVAL_MS 1300

#ifndef CONFIG_TEST_AUTO_WAKE_SEC
#define CONFIG_TEST_AUTO_WAKE_SEC 0
#endif
#ifndef CONFIG_TEST_SETTINGS_DEMO_SEC
#define CONFIG_TEST_SETTINGS_DEMO_SEC 0
#endif

static const char *TAG = "main";

// Test hook (CONFIG_TEST_AUTO_WAKE_SEC): fire the idle-wake transition
// while the device has been asleep for N seconds, so the wake path
// (WiFi reconnect, screen switch, moon re-bake/animation) is exercised
// without touching the device. Runs in the LVGL task context, exactly
// like a real tilt wake: it goes through clock_idle_notify_tilt_activity,
// which clears the idle flag BEFORE triggering the wake callback -- a
// direct clock_trigger_idle_wake() here would leave the device flagged
// idle while awake (the DIAG "idle 1" after a test wake).
static void auto_wake_test_cb(lv_timer_t *timer) {
    (void)timer;
    if (CONFIG_TEST_AUTO_WAKE_SEC <= 0 || !clock_is_idle())
        return;
    static uint32_t asleep_s = 0;
    asleep_s++;
    if (asleep_s < CONFIG_TEST_AUTO_WAKE_SEC)
        return;
    asleep_s = 0;
    ESP_LOGW(TAG, "TEST auto-wake: firing idle wake after %us in sleep", CONFIG_TEST_AUTO_WAKE_SEC);
    clock_idle_notify_tilt_activity();
}

// Test hook (CONFIG_TEST_SETTINGS_DEMO_SEC): the scripted settings run.
// Headless acceptance path for the settings stack: it walks hub ->
// pomodoro -> alarm-time -> light-sleep -> airplane one screen per
// second, writes a real pomodoro interval (90 min -- also the cron
// N>59 sanity case), a fixed-time alarm (12:34, all days), and the
// sleep settings (idle timeout, countdown, animation flags), logs
// every transition + write, and reads both SPIFFS files back. Runs in
// the LVGL task like the real user flow; idle monitoring is off for
// the ~5 s it runs (it would otherwise dusk-sleep mid-script).
static void settings_demo_cb(lv_timer_t *timer) {
    (void)timer;
    if (CONFIG_TEST_SETTINGS_DEMO_SEC <= 0)
        return;
    static uint32_t wait_s = (uint32_t)CONFIG_TEST_SETTINGS_DEMO_SEC;
    static int step = 0;
    if (step >= 6)
        return;
    if (wait_s > 0) {
        if (--wait_s > 0)
            return;
        ESP_LOGW(TAG, "TEST settings demo: starting");
        clock_idle_set_enabled(false);
    }
    /* Wait out the boot sync: a hub switch during the transient WiFi
     * screen would race the sync-complete pickup. */
    if (step == 0 && ui_current_screen() == SCREEN_ID_WIFI)
        return;
    esp_err_t r;
    switch (step++) {
    case 0:
        loadScreen(SCREEN_ID_SETTINGS_HUB);
        ui_tick();
        ESP_LOGW(TAG, "TEST settings demo: -> hub");
        break;
    case 1:
        loadScreen(SCREEN_ID_SETTINGS_POMODORO);
        ui_tick();
        r = clock_alarm_set_pomodoro_interval(90);
        ESP_LOGW(TAG, "TEST settings demo: pomodoro every 90 min -> %s", esp_err_to_name(r));
        break;
    case 2:
        loadScreen(SCREEN_ID_SETTINGS_ALARM_TIME);
        ui_tick();
        r = clock_alarm_set_clock_alarm(12, 34, 0x7F);
        ESP_LOGW(TAG, "TEST settings demo: clock alarm 12:34 (all days) -> %s", esp_err_to_name(r));
        break;
    case 3:
        loadScreen(SCREEN_ID_SETTINGS_LIGHT_SLEEP);
        ui_tick();
        settings_set_idle_timeout_sec(60);
        clock_idle_set_timeout_ms(60ULL * 1000ULL);
        settings_set_deep_sleep_countdown_sec(60);
        settings_set_boot_anim_enable(false);
        settings_set_dusk_anim_enable(true);
        r = settings_store_save();
        ESP_LOGW(TAG, "TEST settings demo: sleep settings (idle=60, countdown=60, boot=off, dusk=on) -> %s",
                 esp_err_to_name(r));
        break;
    case 4: {
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        clock_idle_set_enabled(true);
        clock_idle_reset_timer();
        ESP_LOGW(TAG, "TEST settings demo: -> airplane; reading back the SPIFFS files");
        FILE *f = fopen("/spiffs/alarm.txt", "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                ESP_LOGW(TAG, "  alarm.txt| %s", line);
            }
            fclose(f);
        } else {
            ESP_LOGW(TAG, "  alarm.txt| (missing)");
        }
        f = fopen("/spiffs/settings.txt", "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                ESP_LOGW(TAG, "  settings.txt| %s", line);
            }
            fclose(f);
        } else {
            ESP_LOGW(TAG, "  settings.txt| (missing)");
        }
        break;
    }
    default:
        break;
    }
}

extern "C" void app_main(void) {

    // Initialize WiFi first
    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_err_t wifi_ret = wifi_manager_init();
    bool wifi_available = (wifi_ret == ESP_OK);

    if (wifi_available) {
        ESP_LOGI(TAG, "WiFi connected successfully");
    } else {
        ESP_LOGW(TAG, "WiFi initialization failed: %s", esp_err_to_name(wifi_ret));
    }

    // Runtime settings (the settings screens persist overrides of the
    // Kconfig defaults to /spiffs/settings.txt): load them before any
    // runtime consumer. wifi_manager_init mounted SPIFFS already.
    settings_store_load();
    clock_idle_set_timeout_ms((uint64_t)settings_idle_timeout_sec() * 1000ULL);

    // Configure and initialize the clock component with NTP if WiFi available
    clock_config_t clock_cfg = {.enable_ntp_sync = wifi_available};

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

    // QMI8658 IMU (device tilt for the moon's ball-in-hand spin). Runs on
    // the shared BSP I2C bus after clock_init; soft-fails (the moon stays
    // touch-drag-only) when the chip is absent.
    ret = imu_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IMU init failed: %s (moon tilt disabled)", esp_err_to_name(ret));
    }

    // Moon phase: buffers + shared image descriptor + tick timer. Must run
    // before ui_init so create_screens() can attach the moon images (they are
    // created first in each screen, so the clock renders on top).
    moon_ui_init();

    ui_init();
    ESP_LOGI(TAG, "UI initialized");

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
    clock_register_idle_wake_callback((clock_update_cb_t)action_wake_screen);

    // Register countdown timer callback for transition from light sleep to deep sleep
    clock_register_countdown_timer_callback((clock_update_cb_t)action_switch_to_deep_sleep_screen);

    // Initialize idle timer system (sets up infrastructure, but doesn't start the timer yet)
    ret = clock_idle_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Idle timer system initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize idle timer: %s", esp_err_to_name(ret));
    }

    // Touch dismiss of the alarm screen while awake (the idle-wake path
    // only covers the asleep case).
    action_register_alarm_touch();

    bsp_display_unlock();

    // Load the initial screen (full wake setup: tick timers, idle
    // monitoring, moon resume). With the boot NTP sync succeeded the
    // device is fresh -> the airplane screen; otherwise the wake flow
    // runs the on-demand WiFi sync itself (the transient WiFi screen
    // during the handshake, airplane when it completes).
    action_wake_screen(NULL);

    // Boot NTP succeeded: the startup animation plays on the transient
    // WiFi screen (the spec: the boot sweep flows seamlessly WiFi ->
    // airplane). The hand-off poller moves to the airplane screen once
    // the sweep is done (or never started); both screens share the one
    // moon image, so the switch is the atomic whole-panel flush. Test
    // the NTP outcome, not the radio state: the fresh-NTP branch of
    // action_wake_screen already deinit'd the radio on the way to the
    // airplane screen.
    if (wifi_available && !clock_sntp_needs_sync()) {
        loadScreen(SCREEN_ID_WIFI);
        ui_tick();
        action_boot_handoff_start();
    }

    // Moon phase: start the free spin (phase-anchored: active rate while
    // in use, one turn per synodic month when settled -- the AMOLED
    // burn-in protection) and play the synodic boot sweep (the last new
    // moon -> now, from a BLACK screen, ending on the current phase; see
    // UI_MOON_ANIM_*). Wakes from sleep show the current phase static (no
    // animation); the light-sleep "dusk" (pole spin to the dark side) is
    // the sleep indicator and arms the deep-sleep countdown on its last
    // frame (UI_MOON_DUSK_*). A blank RTC (no NTP, no RTC time) leaves
    // the moon black until a trustworthy time exists.
    moon_ui_start();

    // Create polling timer for RTC interrupt handling - debug interval
    // This checks for RTC interrupts and handles timer restarts
    lv_timer_create(clock_rtc_interrupt_handler_cb, RTC_POLL_INTERVAL_MS, NULL);

    // Create idle timer check (configured via Kconfig)
    lv_timer_create(clock_idle_check_cb, CONFIG_CLOCK_IDLE_CHECK_INTERVAL_MS, NULL);

    // Test hook: auto-wake from sleep after N seconds of being asleep
    // (CONFIG_TEST_AUTO_WAKE_SEC; 0 = off).
    if (CONFIG_TEST_AUTO_WAKE_SEC > 0)
        lv_timer_create(auto_wake_test_cb, 1000, NULL);

    // Test hook: the scripted settings run (CONFIG_TEST_SETTINGS_DEMO_SEC;
    // 0 = off).
    if (CONFIG_TEST_SETTINGS_DEMO_SEC > 0)
        lv_timer_create(settings_demo_cb, 1000, NULL);

    ESP_LOGI(TAG, "Using RTC interrupts with %.1f second polling.", RTC_POLL_INTERVAL_MS / 1000.0f);
    ESP_LOGI(TAG, "Sleep transitions: AIRPLANE -(idle)-> LIGHT_SLEEP -(countdown)-> DEEP_SLEEP");
    ESP_LOGI(TAG, "Idle timeout: %u seconds (runtime settings override the Kconfig default)",
             (unsigned)settings_idle_timeout_sec());
    ESP_LOGI(TAG, "LVGL timings: AIRPLANE/WIFI=10ms, LIGHT_SLEEP=configurable, DEEP_SLEEP=paused");
    ESP_LOGI(TAG, "Alarm screen: Active state with no automatic sleep transitions");

    ESP_LOGI(TAG, "Application started - RTC interrupts with polling backup");
}
