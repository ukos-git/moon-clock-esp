#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bsp/display.h"

#include "actions.h"
#include "imu.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"
#include "vars.h"

#include "clock.h"
#include "clock_idle.h"
#include "settings_store.h"
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
// the wake handler (action_wake_screen) consumes (clears) it
// to know that its call is a real wake event -- which keeps a stale
// alarm (one that rang while the user slept) from hijacking the wake
// flow. It no longer drives an animation: wakes (INCLUDING a dismissed
// alarm) show the current moon phase static, no synodic sweep (the
// sweep plays at BOOT only).
static bool g_asleep = false;

/* The WiFi + NTP sync runs on a dedicated task, never on the LVGL task:
 * the wake handler (action_wake_screen) runs in the LVGL task (indev
 * event / moon tilt timer / the RTC minute tick), and the old blocking
 * wifi_manager_init() there parked the LVGL task for the whole ~1.25 s
 * WPA2 handshake -- no LVGL timer fired, so the moon spin and the clock
 * were frozen until the connection landed. The display now stays live
 * during the handshake (on the transient WiFi screen); the drop back to
 * the airplane screen is picked up by the UI tick below (LVGL task)
 * once the sync completes -- on BOTH outcomes (a failure must not
 * strand the device on the WiFi screen). */
#ifndef CONFIG_CLOCK_NTP_TIMEOUT_MS
#define CONFIG_CLOCK_NTP_TIMEOUT_MS 10000
#endif
static volatile bool s_wifi_sync_running = false;
static volatile bool s_wifi_sync_done = false;
static volatile bool s_wifi_sync_ok = false;

static void wifi_sync_task_fn(void *arg) {
    ESP_LOGI(TAG, "WiFi + NTP sync in background task...");
    esp_err_t ret = wifi_manager_init();
    bool ok = (ret == ESP_OK);
    if (ok) {
        g_wifi_deinited = false;
        ESP_LOGI(TAG, "WiFi connected successfully (background)");
        if (clock_sntp_needs_sync()) {
            esp_err_t n = clock_sync_time_from_ntp(CONFIG_CLOCK_NTP_TIMEOUT_MS);
            if (n != ESP_OK)
                ESP_LOGW(TAG, "NTP sync failed (RTC keeps its time): %s", esp_err_to_name(n));
        } else {
            ESP_LOGI(TAG, "NTP sync not due; skipping");
        }
    } else {
        g_wifi_deinited = true;
        ESP_LOGW(TAG, "WiFi connection failed: %s", esp_err_to_name(ret));
    }
    s_wifi_sync_ok = ok;
    s_wifi_sync_done = true;
    s_wifi_sync_running = false;
    vTaskDelete(NULL);
}

static void wifi_sync_kickoff(void) {
    if (s_wifi_sync_running) {
        ESP_LOGI(TAG, "WiFi sync already in progress");
        return;
    }
    s_wifi_sync_running = true;
    s_wifi_sync_done = false;
    s_wifi_sync_ok = false;
    /* 6144: wifi_manager_init + the SNTP stack run here. */
    if (xTaskCreate(wifi_sync_task_fn, "wifi_sync", 6144, NULL, 10, NULL) != pdPASS) {
        s_wifi_sync_running = false;
        s_wifi_sync_done = true;
        s_wifi_sync_ok = false;
        ESP_LOGW(TAG, "WiFi sync task creation failed; dropping to airplane");
    }
}

/* ---------------- tilt -> clock orientation (keep the clock horizontal) ---
 *
 * The in-plane ROLL of the device (the angle of the gravity vector in the
 * screen plane) counter-rotates the clock text + seconds arc so the clock
 * stays HORIZONTAL as the device is tilted in its own plane. With the orbit
 * (UI_MOON_ORBIT_ENABLE) off on the S3 this is the tilt's only moon-UI
 * coupling.
 *
 * The roll is only observable when the screen plane has a gravity component
 * (|g_xy| well above 0: the device tilted between vertical and flat). At
 * exactly vertical or exactly flat the gravity vector is parallel to the
 * screen normal and the in-plane angle is undefined -- then the last
 * correction is kept (0 at a fixed resting pose).
 *
 * The correction is the roll's DEPARTURE from a settled-pose reference
 * (updated only while the tilt monitor reports settled --
 * moon_ui_tilt_settled -- so at-rest drift does not walk the text around),
 * dead-zoned to 1 deg. Steady state (settled pose) => correction 0 => LVGL
 * takes its direct draw path: zero cost. The cost is only paid while the
 * device is actively tilted. UI_CLOCK_ROLL_SIGN flips it for a mirrored
 * board mount (if the text counter-rotates the wrong way, set it to -1).
 *
 * The motion is CALM: the raw roll is rounded to 5 deg, low-passed at
 * 0.25 Hz (a dt-aware running mean that absorbs the 5-deg rounding and the
 * tick delay), and the applied correction ramps toward that target at at
 * most 5 deg/s in intermediate steps -- each step is its own LVGL frame,
 * new tilt inputs simply re-target the ramp on the next tick. The rotation
 * pivot is the screen centre (LVGL's transform_pivot default is the
 * top-left, which swung the full-screen container off the panel). */
#ifndef CONFIG_UI_IMU_ENABLE
#define CONFIG_UI_IMU_ENABLE 1
#endif
#ifndef CONFIG_UI_CLOCK_ROLL_SIGN
#define CONFIG_UI_CLOCK_ROLL_SIGN 1
#endif
#define ROLL_LPF_TAU_MS 637         /* 0.25 Hz 1st-order low-pass: tau = 1/(2*pi*0.25) s */
#define ROLL_ROUND5_DEG 5.0f        /* the filter input is rounded to 5 deg */
#define ROLL_REF_ALPHA 0.2f         /* settled-reference EMA (tracks only while settled) */
#define ROLL_SPEED_100_PER_S 500.0f /* calm ramp: 5 deg/s in 1/100 deg units */
#define ROLL_DEADZONE_DEG 1.0f      /* below this the departure is 0 (relax to rest) */
#define ROLL_FLAT_GATE 0.25f        /* below |g_xy| the roll is undefined */
#define ROLL_RATE_MS 50             /* apply-rate gate (the UI tick is 10 ms awake) */
#define ROLL_DT_CLAMP_MS 1000       /* clamp dt after a tick pause */
#define ROLL_DEG_TO_RAD 0.017453293f
#define ROLL_RAD_TO_100 5729.578f /* radians -> 1/100 deg (LVGL transform is 1/10 deg) */

static float s_lp_roll;    /* 0.25 Hz low-pass of the 5-deg-rounded departure, rad */
static float s_ref_roll;   /* the settled-pose reference roll, rad */
static int32_t s_cur_100;  /* the applied rotation, 1/100 deg (the ramp position) */
static int32_t s_last_v10; /* the last value pushed to LVGL, 1/10 deg (dedupe) */
static bool s_have_roll;
static bool s_pivot_done; /* the centre pivot is set once */
static int64_t s_roll_last_ms;

/* Wrap an angle difference to [-pi, pi] (the shortest path). */
static float roll_wrap(float a) {
    while (a > 3.14159265f)
        a -= 6.28318531f;
    while (a < -3.14159265f)
        a += 6.28318531f;
    return a;
}

/* Called from the UI tick (LVGL task): counter-rotate the clock containers
 * + seconds arcs by the device's in-plane roll departure from the settled
 * pose, so the clock text/arc stay horizontal while the device is tilted.
 *
 * The applied value (s_cur_100, 1/100 deg) ramps toward the 0.25 Hz
 * low-passed, 5-deg-rounded departure at at most 5 deg/s (intermediate
 * frames; new tilt inputs re-target the ramp on the next apply). LVGL's
 * transform_rotation is 1/10 deg, so the ramp value is divided by 10 when
 * pushed. The rotation pivot is the screen centre (LVGL's default is the
 * top-left, which swung the full-screen container off the panel). */
static void clock_roll_apply(void) {
    if (!CONFIG_UI_IMU_ENABLE || !imu_is_ready())
        return;
    float g[3];
    if (!imu_read_gravity(g))
        return;
    /* Rate gate: one apply per ROLL_RATE_MS (the UI tick is 10 ms awake /
     * 100 ms light sleep, paused in deep sleep where the clock is hidden). */
    int64_t now = esp_timer_get_time();
    int64_t dt_raw = now - s_roll_last_ms;
    if (dt_raw < ROLL_RATE_MS)
        return;
    int32_t dt_ms = (int32_t)(dt_raw > ROLL_DT_CLAMP_MS ? ROLL_DT_CLAMP_MS : dt_raw);
    if (dt_ms < ROLL_RATE_MS)
        dt_ms = ROLL_RATE_MS;
    s_roll_last_ms = now;

    /* New input only when the roll is observable (gravity with a screen-
     * plane component). Otherwise the ramp below still runs toward the last
     * low-passed target: the last correction is kept. */
    float m = hypotf(g[0], g[1]);
    if (m >= ROLL_FLAT_GATE) {
        float roll = atan2f(g[1], g[0]);
        if (!s_have_roll) {
            s_ref_roll = roll;
            s_lp_roll = 0.0f;
            s_cur_100 = 0;
            s_last_v10 = 0;
            s_have_roll = true;
        }
        /* The settled reference tracks only while settled, so at-rest drift
         * (the reference follows the pose) cannot walk the text around. */
        if (moon_ui_tilt_settled())
            s_ref_roll += ROLL_REF_ALPHA * roll_wrap(roll - s_ref_roll);
        /* Filter input: the departure from the settled pose, dead-zoned to
         * 1 deg and ROUNDED to 5 deg. */
        float dep = roll_wrap(roll - s_ref_roll);
        if (fabsf(dep) < ROLL_DEADZONE_DEG * ROLL_DEG_TO_RAD)
            dep = 0.0f;
        float r5 = ROLL_ROUND5_DEG * ROLL_DEG_TO_RAD;
        float rounded = lrintf(dep / r5) * r5;
        /* 0.25 Hz first-order low-pass (dt-aware running mean). */
        s_lp_roll += (1.0f - expf(-(float)dt_ms / (float)ROLL_LPF_TAU_MS)) * roll_wrap(rounded - s_lp_roll);
    }

    /* Ramp the applied value toward the low-passed target, <= 5 deg/s. */
    float target_100 = s_lp_roll * ROLL_RAD_TO_100 * (float)CONFIG_UI_CLOCK_ROLL_SIGN;
    float max_step = ROLL_SPEED_100_PER_S * ((float)dt_ms / 1000.0f);
    float delta = target_100 - (float)s_cur_100;
    if (delta > max_step)
        delta = max_step;
    else if (delta < -max_step)
        delta = -max_step;
    s_cur_100 = (int32_t)lrintf((float)s_cur_100 + delta);

    int32_t v10 = s_cur_100 / 10; /* 1/100 deg -> 1/10 deg (LVGL transform unit) */
    if (v10 == s_last_v10)
        return; /* nothing new to draw */
    int32_t old = s_last_v10;
    s_last_v10 = v10;
    float deg = (float)v10 / 10.0f;
    /* The clock containers rotate via the LVGL transform (pivot = screen
     * centre; LVGL's default is the top-left, which swung the full-screen
     * container off the panel). The rotated layer of a full-screen
     * container needs ~0.5-1.8 MB and comes from the C-library malloc
     * (sdkconfig.defaults: clib backend), so it lands in PSRAM -- the
     * built-in 64 KB LVGL pool cannot hold it (crash on first tilt). */
    lv_obj_t *conts[4] = {
        objects.modem_sleep_clock,
        objects.wifi_clock,
        objects.light_sleep_clock,
        objects.deep_sleep_clock,
    };
    for (int i = 0; i < 4; i++) {
        if (!conts[i])
            continue;
        if (!s_pivot_done) {
            lv_obj_set_style_transform_pivot_x(conts[i], LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(conts[i], LV_PCT(50), 0);
        }
        lv_obj_set_style_transform_rotation(conts[i], v10, 0);
    }
    s_pivot_done = true;
    /* The seconds arcs rotate NATIVELY (lv_arc's own 0-360 deg offset,
     * drawn directly -- no transform layer) about their own centre, which
     * is the screen centre: same rigid body as the containers. */
    if (objects.arc_clock_seconds_modem)
        lv_arc_set_rotation(objects.arc_clock_seconds_modem, (int32_t)lrintf(270.0f + deg));
    if (objects.arc_clock_seconds_wifi)
        lv_arc_set_rotation(objects.arc_clock_seconds_wifi, (int32_t)lrintf(270.0f + deg));
    if (old != 0 || v10 != 0)
        ESP_LOGI(TAG, "clock roll %+d -> %+d deg (sign %d)", old / 10, v10 / 10, CONFIG_UI_CLOCK_ROLL_SIGN);
}

// Callback for UI tick timer
static void ui_tick_timer_cb(lv_timer_t *timer) {
    // Keep the clock horizontal while the device is tilted in its own plane
    // (no-op at the settled pose: zero correction -> LVGL direct draw path).
    clock_roll_apply();
    // Finished background WiFi + NTP sync: drop to the airplane screen
    // from here (the LVGL task; loadScreen touches LVGL state). Deferred
    // to the tick so the handshake never blocks the display. Both
    // outcomes (success AND failure) must leave the transient WiFi
    // screen. Only while awake: if the device re-slept during the
    // handshake, drop the hand-off and let the next wake re-trigger it.
    if (!s_wifi_sync_running && s_wifi_sync_done) {
        s_wifi_sync_done = false;
        if (!clock_is_idle()) {
            if (ui_current_screen() == SCREEN_ID_WIFI) {
                loadScreen(SCREEN_ID_MODEM_SLEEP);
                ui_tick();
            }
            clock_idle_set_enabled(true);
            clock_idle_reset_timer();
            ESP_LOGI(TAG, "WiFi sync %s -> airplane screen", s_wifi_sync_ok ? "complete" : "failed");
        } else {
            ESP_LOGI(TAG, "WiFi sync done but device is asleep; airplane hand-off deferred");
        }
    }
    ui_tick();
}

/* ---------------- on-demand WiFi + NTP sync (the transient WiFi screen) ---
 *
 * The device lives on the airplane (modem-sleep) screen; WiFi is
 * activated on demand (boot, wake, the hourly tick, the settings hub's
 * exit button) purely to refresh the RTC, then the sync flow drops
 * straight back to airplane. The WiFi screen hosts the handshake and
 * the (boot) synodic sweep; it and the airplane screen share the one
 * moon image, so the hand-off is the atomic whole-panel flush with no
 * visible seam.
 */

// The boot hand-off: after boot lands on the WiFi screen (the boot NTP
// succeeded), this 100 ms poller switches to the airplane screen once
// the boot sweep is out of the way -- when the sweep has finished
// baking, or when 1 s elapsed without it starting (animation disabled,
// no trustworthy time). Both screens share the one moon image, so the
// switch is the existing atomic flush: the sweep plays seamlessly
// across WiFi -> airplane.
static lv_timer_t *s_boot_handoff_timer = NULL;
static bool s_boot_handoff_saw_sweep = false;
static int64_t s_boot_handoff_t0_ms = 0;

static void boot_handoff_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_wifi_sync_running || moon_ui_sweeping()) {
        s_boot_handoff_saw_sweep = true;
        return;
    }
    int64_t elapsed_ms = esp_timer_get_time() - s_boot_handoff_t0_ms;
    if (!s_boot_handoff_saw_sweep && elapsed_ms < 1000)
        return;
    ESP_LOGI(TAG, "Boot sync hand-off: WiFi -> airplane screen");
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
    if (s_boot_handoff_timer) {
        lv_timer_delete(s_boot_handoff_timer);
        s_boot_handoff_timer = NULL;
    }
}

void action_start_wifi_sync(void) {
    if (s_wifi_sync_running) {
        ESP_LOGI(TAG, "WiFi sync already in progress");
        return;
    }
    /* No sleep transitions during the ~8+10 s handshake: the sleep
     * paths call wifi_manager_deinit(), which must not race the sync
     * task's wifi_manager_init() (the pre-existing wake-path race). */
    clock_idle_set_enabled(false);
    loadScreen(SCREEN_ID_WIFI);
    ui_tick();
    ESP_LOGI(TAG, "WiFi screen loaded (on-demand sync)");
    wifi_sync_kickoff();
}

/* Start the boot hand-off poller (called from app_main only when boot
 * landed on the WiFi screen). */
void action_boot_handoff_start(void) {
    if (s_boot_handoff_timer)
        return;
    s_boot_handoff_saw_sweep = false;
    s_boot_handoff_t0_ms = esp_timer_get_time();
    s_boot_handoff_timer = lv_timer_create(boot_handoff_timer_cb, 100, NULL);
    ESP_LOGI(TAG, "Boot hand-off poller started (100 ms)");
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
    // Deep sleep: the clock is removed from the screen and the panel
    // backlight is off -- skip the only live clock update (the RTC
    // bookkeeping itself is untouched; the moon's day re-bake happens
    // on the wake path).
    if (g_backlight_off)
        return;
    clock_time_t current_time;
    esp_err_t ret = clock_get_time(&current_time);

    /* Hourly on-demand sync: at the top of a minute, while at rest on
     * the airplane screen and the RTC is due for a refresh (the 1 h
     * NTP interval), activate WiFi + NTP and drop back to airplane.
     * Gated to AT REST on airplane: no waking a sleeping device (a
     * 30 s radio wake for an NTP fetch is a net power loss -- the next
     * wake re-syncs), no interference with the boot sweep, no yanking
     * a settings session away. */
    if (ret == ESP_OK && current_time.minute == 0 && !clock_is_idle() && !s_wifi_sync_running && !moon_ui_sweeping() &&
        !wifi_manager_is_connected() && clock_sntp_needs_sync() && ui_current_screen() == SCREEN_ID_MODEM_SLEEP) {
        ESP_LOGI(TAG, "Hourly sync due at the top of the minute; activating WiFi");
        action_start_wifi_sync();
        return;
    }

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
 * path (touch -> action_wake_screen) only fires while the
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
        action_wake_screen(e);
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

    // Deep sleep shows the moon only: the clock is removed (the RTC
    // clock update is also gated on g_backlight_off above). The
    // flag is re-applied on every deep-sleep entry, so the next
    // entry re-hides it after a wake to the other screens.
    lv_obj_add_flag(objects.deep_sleep_clock, LV_OBJ_FLAG_HIDDEN);

    // Update UI one final time to show current state (the dark side
    // from the completed dusk, one last atomic flush)
    ui_tick();

    // Deep sleep turns the panel backlight OFF (the sleep screens
    // before it kept it on). A wake (touch, tilt or an alarm) restores
    // it (action_wake_screen / action_alarm_triggered).
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
 * @brief Wake handler: return from sleep (or boot) to the active flow.
 *
 * Called when the user interacts after idle (the idle-wake callback),
 * from the alarm screen, and at boot. Runs the on-demand WiFi + NTP
 * sync (the transient WiFi screen) when the device is not connected or
 * the RTC is due for a refresh; otherwise drops straight to the
 * airplane screen.
 */
void action_wake_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Wake: switching to the active flow");

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
        ESP_LOGI(TAG, "Dismissing alarm from modem sleep - returning to the airplane screen");
        g_alarm_active = false;
        action_switch_to_airplane_screen(e);
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

    // Already connected and the RTC is fresh: straight to the airplane
    // screen (the home screen).
    if (wifi_manager_is_connected() && !clock_sntp_needs_sync()) {
        g_wifi_deinited = false;
        action_switch_to_airplane_screen(e);
        return;
    }

    // Otherwise run the on-demand sync: the transient WiFi screen hosts
    // the handshake (display stays live; the UI tick drops to airplane
    // when it completes, on both outcomes).
    action_start_wifi_sync();
}

/**
 * @brief Switch to the airplane screen (the home screen; non-WiFi mode).
 *
 * Simple clock display with the modem in sleep (WiFi deinitialized).
 * The home screen: moon, clock, seconds arc, no indicator icons.
 */
void action_switch_to_airplane_screen(lv_event_t *e) {
    ESP_LOGI(TAG, "Switching to the airplane screen");

    // Deinitialize WiFi if it's running
    if (!g_wifi_deinited && wifi_manager_is_connected()) {
        wifi_manager_deinit();
        g_wifi_deinited = true;
        ESP_LOGI(TAG, "WiFi deinitialized for the airplane screen");
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

    // Load the airplane screen
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

    // Deinitialize WiFi/network to save power (airplane / modem sleep)
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
            clock_timer_enable(settings_deep_sleep_countdown_sec(), CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT);
        if (ret == ESP_OK)
            ESP_LOGI(TAG, "dusk unavailable; RTC countdown %u s to deep sleep armed immediately",
                     (unsigned)settings_deep_sleep_countdown_sec());
        else
            ESP_LOGE(TAG, "Failed to start RTC countdown timer: %s", esp_err_to_name(ret));
    }

    // Load light sleep screen using EEZ Studio function
    loadScreen(SCREEN_ID_LIGHT_SLEEP);

    // Light sleep dims the clock to ~20% opacity (the AMOLED has no
    // per-pixel alpha; a low-opacity container is the "barely
    // visible" the design asks for). The moon image is a sibling of
    // the clock container, so it stays at full contrast.
    lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_20, 0);

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
