#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h" // required by bsp until PR goes through
#include "esp_heap_caps.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "lvgl.h"

#include "actions.h"
#include "alarm_audio.h"
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
#ifndef CONFIG_TEST_ROLL_TILT_SEC
#define CONFIG_TEST_ROLL_TILT_SEC 0
#endif
#ifndef CONFIG_TEST_HANG_WATCH
#define CONFIG_TEST_HANG_WATCH 0
#endif
#ifndef CONFIG_TEST_WIFI_SCAN_SEC
#define CONFIG_TEST_WIFI_SCAN_SEC 0
#endif

// Hang watcher (TEST_HANG_WATCH): the LVGL task is the heartbeat source
// (a 200 ms lv_timer runs on it; every other lv_timer -- the RTC poll,
// the idle check, the UI tick -- stops when it blocks). If the
// heartbeat goes stale, the system is hung: some task is parked on a
// lock. That kind of hang is otherwise SILENT (a parked task keeps the
// idle tasks alive, so the task WDT never fires; the console just stops
// mid-line while the panel holds a half-drawn frame -- the "white
// stripes"). The watcher prints every task with its state and a
// symbolized backtrace, then aborts so the panic prints the final
// context too. Diagnostic aid only; off by default.
static const char *TAG = "main";
#if CONFIG_TEST_HANG_WATCH
static volatile int64_t s_lvgl_hb_ms;
static void lvgl_hb_cb(lv_timer_t *t) {
    (void)t;
    s_lvgl_hb_ms = esp_timer_get_time();
}
static void hang_watch_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t age_us = esp_timer_get_time() - s_lvgl_hb_ms;
        if (age_us < 15000000) /* 15 s: the longest legitimate LVGL block is the
                                * ~340 ms sweep flush; a real hang grows
                                * without bound. (esp_timer_get_time is us.) */
            continue;
        ESP_LOGE(TAG, "HANG: LVGL heartbeat stale %lld ms; dumping tasks", (long long)(age_us / 1000));
        moon_ui_dump_hang_context();
        UBaseType_t n = uxTaskGetNumberOfTasks();
        TaskStatus_t *ts = (TaskStatus_t *)heap_caps_malloc(n * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
        if (!ts) {
            ESP_LOGE(TAG, "HANG: task table alloc failed");
            continue;
        }
        configRUN_TIME_COUNTER_TYPE run = 0;
        uxTaskGetSystemState(ts, n, &run);
        for (UBaseType_t i = 0; i < n; i++) {
            ESP_LOGW(TAG, "  task %-14s state=%d prio=%u hwm=%u", ts[i].pcTaskName, (int)ts[i].eCurrentState,
                     (unsigned)ts[i].uxCurrentPriority, (unsigned)ts[i].usStackHighWaterMark);
        }
        heap_caps_free(ts);
        ESP_LOGE(TAG, "HANG: aborting");
        abort();
    }
}
#endif

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

// Test hook (CONFIG_TEST_WIFI_SCAN_SEC): open the settings WiFi screen N
// seconds after boot -- the screen that starts the background AP scan
// (wifi_scan_begin) and renders the results as rows. Headless verification
// of the scan path (on the P4: the esp_hosted RPC scan through the remote
// C6, including its 5 GHz APs). Like the settings demo it waits out the
// boot sweep (a screen switch mid-sweep is guarded off) and keeps idle
// monitoring off so the capture is not cut by the sleep transition.
static void wifi_scan_test_cb(lv_timer_t *timer) {
    (void)timer;
    if (CONFIG_TEST_WIFI_SCAN_SEC <= 0)
        return;
    static uint32_t wait_s = (uint32_t)CONFIG_TEST_WIFI_SCAN_SEC;
    static bool opened = false;
    if (wait_s > 0) {
        if (--wait_s > 0)
            return;
        ESP_LOGW(TAG, "TEST wifi scan: wait done, waiting out the boot sweep");
        clock_idle_set_enabled(false);
    }
    if (ui_current_screen() == SCREEN_ID_WIFI || moon_ui_sweeping())
        return; // hold until the sweep is done
    if (opened)
        return;
    opened = true;
    ESP_LOGW(TAG, "TEST wifi scan: opening the WiFi settings screen");
    loadScreen(SCREEN_ID_SETTINGS_WIFI);
    ui_tick();
    /* loadScreen + ui_tick only switch the root; the per-screen refresh
     * (which calls wifi_scan_begin for the WiFi screen) is driven by the
     * entry's event handler in the real flow. Mirror it here, else the
     * background scan never starts. */
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI);
}

// Test hook (CONFIG_TEST_SETTINGS_DEMO_SEC): the scripted settings run.
// Headless acceptance path for the settings stack: it walks hub ->
// pomodoro -> alarm-time -> light-sleep -> airplane one screen per
// second, writes a real pomodoro interval (90 min -- also the cron
// N>59 sanity case), a fixed-time alarm (12:35, Mon-Fri -- the
// range-arc default), and the
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
    /* Wait out the boot sync AND the boot sweep. The user's freeze is a
     * home->settings switch while the FREE SPIN is running (post-sweep,
     * the spin's direct panel writes run concurrently with the fade). A
     * switch during the sweep is guarded off (s_sweep_baking suppresses
     * the fade and the spin is paused), so it does not reproduce the
     * bug. Hold at step 0 until the sweep is done and the spin has
     * resumed, then switch -- exactly the user's "tap home -> settings". */
    if (step == 0 && (ui_current_screen() == SCREEN_ID_WIFI || moon_ui_sweeping()))
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
        /* Mon-Fri: bit 0 = Sunday, so bits 1..5 (NOT 0x7E = Mon-Sat). */
        r = clock_alarm_set_clock_alarm(12, 35, 0x3E);
        ESP_LOGW(TAG, "TEST settings demo: clock alarm 12:35 (Mon-Fri) -> %s", esp_err_to_name(r));
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

// Test hook (CONFIG_TEST_CPU_STATS_SEC): per-second per-task CPU % from
// the FreeRTOS run-time stats (configGENERATE_RUN_TIME_STATS, the stats
// clock = esp_timer, so microseconds). Profiling aid for which task burns
// the settings-idle CPU and the spin CPU. After N seconds of waiting (so
// the boot sweep is done and the device rests) it logs once per second,
// 10 s on the home screen then 30 s on the settings hub (loading it). The
// window's "busy" total excludes the IDLE tasks (they account for the idle
// time on each core, so including them reads ~200 % = both cores); the
// per-task lines still report IDLE0/IDLE1 so the idle share is visible.
// Note: it holds the idle timer awake each second, which ALSO keeps the
// moon spin at its ACTIVE rate (activity < SETTLE_S), so the home window
// shows the active spin, not the settled coast-skip. 0 (default) = off.
#ifndef CONFIG_TEST_CPU_STATS_SEC
#define CONFIG_TEST_CPU_STATS_SEC 0
#endif

/* The run-time stats API (uxTaskGetSystemState) only exists with the
 * FreeRTOS trace facility + run-time stats enabled; without them the
 * log function is a no-op. */
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
#define CPU_STATS_MAX_TASKS 32
#define CPU_STATS_HOME_SEC 10 /* seconds on the home screen before the hub */
#define CPU_STATS_HUB_SEC 30

static TaskStatus_t s_cpu_prev[CPU_STATS_MAX_TASKS];
static UBaseType_t s_cpu_prev_n = 0;
static int64_t s_cpu_wall_prev_us = 0;

static void cpu_stats_log(const char *phase) {
    int64_t now_us = esp_timer_get_time();
    uint32_t total = 0;
    TaskStatus_t cur[CPU_STATS_MAX_TASKS];
    UBaseType_t n = uxTaskGetSystemState(cur, CPU_STATS_MAX_TASKS, &total);
    (void)total; /* cumulative since boot; not the window total */
    if (s_cpu_wall_prev_us == 0) {
        memcpy(s_cpu_prev, cur, (size_t)n * sizeof(cur[0]));
        s_cpu_prev_n = n;
        s_cpu_wall_prev_us = now_us;
        return; /* first sample: seed, nothing to diff */
    }
    /* The real window wall = since the last 1 s call (NOT the call's own
     * duration -- that was the bug that made the % read 300000%). */
    int64_t wall_us = now_us - s_cpu_wall_prev_us;
    s_cpu_wall_prev_us = now_us;
    /* Per-task delta over this window (run-time, in the stats clock --
     * esp_timer, so microseconds). The window's "busy" total EXCLUDES the
     * IDLE tasks (IDLE0/IDLE1 account for the remaining time on each core,
     * so including them would make the total read ~200 % = both cores);
     * the per-task lines still report IDLE so the idle share is visible. */
    unsigned d[CPU_STATS_MAX_TASKS] = {0};
    uint64_t busy = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        for (UBaseType_t j = 0; j < s_cpu_prev_n; j++) {
            if (strcmp(s_cpu_prev[j].pcTaskName, cur[i].pcTaskName) == 0) {
                d[i] = (unsigned)(cur[i].ulRunTimeCounter - s_cpu_prev[j].ulRunTimeCounter);
                if (strncmp(cur[i].pcTaskName, "IDLE", 4) != 0)
                    busy += d[i];
                break;
            }
        }
    }
    unsigned pct1core = (unsigned)(((uint64_t)busy * 100) / (unsigned)wall_us); /* 100% = one core */
    ESP_LOGW(TAG, "CPU %s: busy %uus/wall %uus = %u%% 1-core (IDLE excluded):", phase, (unsigned)busy,
             (unsigned)wall_us, pct1core);
    for (UBaseType_t i = 0; i < n; i++) {
        if (d[i] == 0)
            continue;
        unsigned pct = (unsigned)(((uint64_t)d[i] * 100) / (unsigned)busy);     /* share of used CPU */
        unsigned abs1 = (unsigned)(((uint64_t)d[i] * 100) / (unsigned)wall_us); /* of one core */
        ESP_LOGW(TAG, "  %-16s %3u%% used  (%u%% 1-core, %uus)", cur[i].pcTaskName, pct, abs1, d[i]);
    }
    memcpy(s_cpu_prev, cur, (size_t)n * sizeof(cur[0]));
    s_cpu_prev_n = n;
}
#else /* run-time stats off: the hook's log is a no-op */
static void cpu_stats_log(const char *phase) { (void)phase; }
#define CPU_STATS_HOME_SEC 10
#define CPU_STATS_HUB_SEC 30
#endif

static void cpu_stats_cb(lv_timer_t *timer) {
    (void)timer;
    if (CONFIG_TEST_CPU_STATS_SEC <= 0)
        return;
    /* Hold the device awake the way a user touch does: the idle timer
     * otherwise dusk-sleeps ~60 s after the boot sweep drops to the
     * airplane screen (before the hub measurement window), and the LVGL
     * task pauses for deep sleep. */
    clock_idle_reset_timer();
    static int phase = 0; /* 0 = wait, 1 = home, 2 = hub */
    static uint32_t wait_s = (uint32_t)CONFIG_TEST_CPU_STATS_SEC;
    static uint32_t measured = 0;
    if (phase == 0) {
        if (wait_s > 0) {
            if (--wait_s > 0)
                return;
            phase = 1;
            measured = 0;
            ESP_LOGW(TAG, "TEST cpu stats: start (home %ds, then hub %ds)", CPU_STATS_HOME_SEC, CPU_STATS_HUB_SEC);
        }
    }
    if (phase == 1) {
        /* The first call seeds the previous-sample baseline (no log line);
         * the rest diff against it. */
        cpu_stats_log("home");
        if (++measured >= CPU_STATS_HOME_SEC) {
            phase = 2;
            measured = 0;
            loadScreen(SCREEN_ID_SETTINGS_HUB);
            ui_tick();
            ESP_LOGW(TAG, "TEST cpu stats: -> settings hub");
        }
        return;
    }
    if (phase == 2) {
        cpu_stats_log("hub");
        if (++measured >= CPU_STATS_HUB_SEC) {
            lv_timer_delete(timer);
            ESP_LOGW(TAG, "TEST cpu stats: done");
        }
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

#if defined(CONFIG_BOARD_P4)
    /* The P4's XC BSP default (bsp_display_start) runs the LVGL adapter in
     * TRIPLE_PARTIAL tearing mode on the panel's THREE frame buffers
     * (CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3): every LVGL refresh flips the
     * continuous MIPI-DPI scanout to another FB, but the spin's direct
     * strip burst only writes the FB that is being scanned at that
     * instant -- so a flip briefly displays the spin pose from that FB's
     * LAST visit (an older theta; the spin's theta only advances CW, so
     * older reads COUNTER-CLOCKWISE) until the next spin frame corrects
     * it. Single FB + NONE tear mode: LVGL's strips and the spin's
     * strips all write the one surface the panel is scanning; the
     * residual tearing is at most one frame (sub-pixel at the settled
     * spin rate, ~1 px at the rim at the active rate) -- invisible.
     * The S3's QSPI panel keeps its own bsp_display_start (NONE mode,
     * single GRAM). */
    bsp_display_cfg_t p4_disp_cfg = {.lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
                                     .rotation = ESP_LV_ADAPTER_ROTATE_0,
                                     .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
                                     .touch_flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0}};
    bsp_display_start_with_config(&p4_disp_cfg);
#else
    bsp_display_start();
#endif
    /* Serialized backlight (direct BSP call at boot -- moon_ui_init has
     * not run yet, so the panel-io mutex is not armed and the wrapper
     * falls through to the raw write). */
    moon_ui_panel_brightness_set(100);

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

    // Alarm audio (P4: ES8311 speaker + amp on the shared I2C bus; S3:
    // no-op stubs). Soft-fails like the IMU: the alarm stays display-only.
    alarm_audio_init();

    // Base screen orientation (the panel's 90 deg CW mount offset): the
    // IMU's first sample arrives late and the pipeline only pushes on a
    // change, so the rotation must be in place before the first frame.
    action_orientation_init();

    // 2 s press re-anchors the orientation's home pose: the one-shot 1 s
    // seed can latch 180 deg off if the device was flipped while the
    // first window filled, and no tilt recovers that (the pipeline
    // re-tracks only on step 0).
    action_register_orientation_touch();

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

    // Boot NTP outcome: by the time the UI is shown the blocking boot
    // sync (wifi_manager_init + NTP, before bsp_display_start) has ALREADY
    // acquired the clock, so action_wake_screen above is already on the
    // home (airplane) screen -- the startup animation plays THERE (both
    // screens share the one moon image, s_rgb, so nothing is lost by not
    // lingering on the transient WiFi screen). The user's decision: the
    // WiFi screen is only shown while a sync is in flight (wake / manual /
    // hourly), and drops to airplane as soon as the clock is acquired --
    // which at boot is before the UI appears, so it is never shown at boot
    // on the success path.

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
    // 0 = off). Idle monitoring stays off from boot so the device holds the
    // rest-on-airplane pose while the boot sweep runs (the demo would
    // otherwise fire mid-light-sleep, with the dusk bake suppressing the
    // spin-render stop it is meant to exercise).
    if (CONFIG_TEST_SETTINGS_DEMO_SEC > 0) {
        lv_timer_create(settings_demo_cb, 1000, NULL);
        clock_idle_set_enabled(false);
    }

    // Test hook: per-second per-task CPU % (TEST_CPU_STATS_SEC; 0 = off).
    if (CONFIG_TEST_CPU_STATS_SEC > 0)
        lv_timer_create(cpu_stats_cb, 1000, NULL);

    // Test hook: the headless clock-roll tilt script (TEST_ROLL_TILT_SEC;
    // 0 = off). Feeds the real roll pipeline synthetic gravity -- the
    // transform layer (PSRAM alloc) + centre pivot run without a tilt.
    if (CONFIG_TEST_ROLL_TILT_SEC > 0)
        lv_timer_create(action_roll_test_cb, 1000, NULL);

    // Test hook: the headless WiFi scan (TEST_WIFI_SCAN_SEC; 0 = off).
    if (CONFIG_TEST_WIFI_SCAN_SEC > 0)
        lv_timer_create(wifi_scan_test_cb, 1000, NULL);

    // Test hook: the LVGL-task hang watcher (TEST_HANG_WATCH; off by
    // default). The heartbeat timer is the only thing that keeps the
    // LVGL task alive; a stuck LVGL task goes stale and the watcher
    // dumps every task + backtrace, then aborts.
#if CONFIG_TEST_HANG_WATCH
    lv_timer_create(lvgl_hb_cb, 200, NULL);
    xTaskCreatePinnedToCore(hang_watch_task, "hang_watch", 6144, NULL, 5, NULL, 1);
#endif

    ESP_LOGI(TAG, "Using RTC interrupts with %.1f second polling.", RTC_POLL_INTERVAL_MS / 1000.0f);
    ESP_LOGI(TAG, "Sleep transitions: AIRPLANE -(idle)-> LIGHT_SLEEP -(countdown)-> DEEP_SLEEP");
    ESP_LOGI(TAG, "Idle timeout: %u seconds (runtime settings override the Kconfig default)",
             (unsigned)settings_idle_timeout_sec());
    ESP_LOGI(TAG, "LVGL timings: AIRPLANE/WIFI=10ms, LIGHT_SLEEP=configurable, DEEP_SLEEP=paused");
    ESP_LOGI(TAG, "Alarm screen: Active state with no automatic sleep transitions");

    ESP_LOGI(TAG, "Application started - RTC interrupts with polling backup");
}
