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

/* ---------------- tilt -> screen orientation (90-degree steps) ---------
 *
 * The screen-plane gravity vector (the IMU x/y gravity; screen coords,
 * x right / y down) picks the WHOLE-SCREEN orientation: one of the four
 * 90-degree multiples that keeps the UI upright relative to the resting
 * pose. Every screen rotates (the clock, the seconds arcs, the settings
 * and the alarm screens -- all widgets are children of their screen root,
 * so the one root transform covers them all). With the orbit
 * (UI_MOON_ORBIT_ENABLE) off on the S3 this is the tilt's only UI
 * coupling.
 *
 * Math per spec: the gravity x/y is sampled at 10 Hz (a 100 ms gate on
 * the UI tick), a 1 s windowed running mean (10 samples per axis) is the
 * 1 Hz low-pass, and the filtered axis values are quantized to int4
 * (16 levels of ~0.125 g). The orientation is the DEPARTURE of the
 * filtered gravity vector from the settled-pose reference vector,
 * quantized to the nearest 90 degrees with integer dot/cross only (no
 * atan2, no floats in the decision). The reference tracks only while the
 * tilt monitor reports settled (moon_ui_tilt_settled), so at-rest drift
 * cannot walk the orientation around; near-vertical or flat poses
 * (gravity with almost no screen-plane component) keep the last
 * orientation (the in-plane angle is undefined there, like the old roll's
 * flat gate).
 *
 * The change is INSTANT (no ramp, no intermediate frames): a 90-degree
 * rotation of the 466x466 square panel about its centre maps the panel
 * onto itself, so one transform push per step is the whole animation.
 * LVGL's hit test is inverse-mapped through the transform
 * (lv_indev_search_obj), so touch lands on the right widget in any
 * orientation. At the resting orientation (0) the transform is 0 and
 * LVGL takes its direct draw path: zero cost. The rotated root's layer
 * is panel-sized (466x466 RGB565 ~= 0.4 MB) and comes from the C-library
 * malloc (sdkconfig.defaults: clib backend) -> PSRAM; the built-in 64 KB
 * LVGL pool cannot hold it. UI_CLOCK_ROLL_SIGN flips the correction for a
 * mirrored board mount (if the UI rotates the wrong way, set it to -1).
 */
#ifndef CONFIG_UI_IMU_ENABLE
#define CONFIG_UI_IMU_ENABLE 1
#endif
#ifndef CONFIG_UI_CLOCK_ROLL_SIGN
#define CONFIG_UI_CLOCK_ROLL_SIGN 1
#endif
#define ORI_SAMPLE_MS 100 /* one gravity sample per 100 ms (10 Hz) */
#define ORI_WINDOW 10     /* the 1 Hz low-pass: a 1 s running mean (10 samples) */
#define ORI_Q_SCALE 8     /* int4 quantization: 1 level = 1/8 g (clamped +-1 g) */
#define ORI_FLAT_GATE2 4  /* squared flat gate: below |g_xy| = 0.25 g the pose is undefined */

static int32_t s_win_x[ORI_WINDOW]; /* the sample window (int4-quantized) */
static int32_t s_win_y[ORI_WINDOW];
static int32_t s_win_head;
static int32_t s_win_count;
static int8_t s_ref_qx, s_ref_qy; /* the settled-pose reference, int4 scale */
static bool s_have_ref;
static int32_t s_last_v10; /* the last value pushed to LVGL, 1/10 deg (dedupe) */
static int64_t s_ori_last_ms;
/* Sustained confirmation (same idiom as the tilt monitor's streak): a
 * step change applies only when ~1 s of consecutive samples agree on it
 * (ORI_WINDOW samples at the 10 Hz pipeline). A single transient sample,
 * or a short I2C glitch during a heavy bake (seen once under the sweep:
 * the UI walked to -270 deg while the device sat flat on the bench), is
 * diluted out by the 1 s running mean and cannot stick for the full
 * second. A deliberate 90-degree move is held far longer, so the
 * confirmation costs at most ~1 s of latency on a real tilt. */
static int32_t s_pending_step;  /* the step the last sample produced */
static int32_t s_pending_count; /* consecutive samples producing it */

/* Clamp an int to +-limit (the int4 quantizer). */
static int8_t ori_clamp(int v, int limit) {
    if (v > limit)
        return (int8_t)limit;
    if (v < -limit)
        return (int8_t)(-limit);
    return (int8_t)v;
}

/* Test hook: the headless tilt script (TEST_ROLL_TILT_SEC). After N s of
 * boot it feeds the orientation pipeline synthetic in-plane gravity
 * instead of the IMU, so the full transform path (the PSRAM layer, the
 * centre pivot) runs on the device without a physical tilt. */
#ifndef CONFIG_TEST_ROLL_TILT_SEC
#define CONFIG_TEST_ROLL_TILT_SEC 0
#endif
#if CONFIG_TEST_ROLL_TILT_SEC > 0
/* The script: the synthetic screen-plane gravity (quantized unit
 * vectors: |g| = 1 g, well above the flat gate), by second: the
 * resting pose (seeds the reference) -> 90 deg CW tilt -> 180 ->
 * 90 deg CCW -> back to rest, so the steps 0 -> -90 -> -180 -> -270
 * -> 0 each run through the full window + decision + transform path
 * (the 1 s window lags the step changes by ~1 s, and the sustained
 * confirmation needs a second ~1 s on top -- the phases run 5 s so
 * every step clears both). */
struct ori_script_step {
    uint32_t t_s;
    int8_t gx, gy;
};
static const struct ori_script_step s_ori_script[] = {
    {0, 0, 8}, {5, 8, 0}, {10, 0, -8}, {15, -8, 0}, {20, 0, 8},
};
#define ORI_SCRIPT_END_S 25

static bool s_ori_test_on;   /* the synthetic gravity is feeding the pipeline */
static bool s_ori_test_done; /* the script runs once, not on every wait elapse */
static uint32_t s_ori_test_wait_s = CONFIG_TEST_ROLL_TILT_SEC;
static uint32_t s_ori_test_elapsed_s;
static int8_t s_ori_test_gx, s_ori_test_gy;
#endif /* CONFIG_TEST_ROLL_TILT_SEC > 0 */

/* Push one orientation to all 12 screen roots (LVGL transform units:
 * 1/10 deg, positive = clockwise). The pivot is the panel centre (LVGL's
 * default is the top-left, which swung the full-screen container off the
 * panel -- the old roll's first crash). A 90-degree step of the 466x466
 * panel maps the panel onto itself, so one push is the whole change (the
 * old ramp's intermediate frames are gone). The rotated root's layer is
 * panel-sized (~0.4 MB RGB565) and comes from the C-library malloc (clib
 * backend, sdkconfig.defaults) -> PSRAM; the built-in 64 KB LVGL pool
 * cannot hold it. Every screen's widgets are children of its root, so the
 * one transform covers the clock, the seconds arcs, the settings and the
 * alarm screens alike; LVGL's hit test is inverse-mapped through the
 * transform (lv_indev_search_obj), so touch lands on the right widget in
 * any orientation. */
static void ori_apply_v10(int32_t v10) {
    lv_obj_t *roots[12] = {
        objects.modem_sleep,
        objects.wifi,
        objects.light_sleep,
        objects.deep_sleep,
        objects.alarm,
        objects.settings_hub,
        objects.settings_alarm,
        objects.settings_pomodoro,
        objects.settings_alarm_time,
        objects.settings_light_sleep,
        objects.settings_deep_sleep,
        objects.settings_wifi,
    };
    for (int i = 0; i < 12; i++) {
        if (!roots[i])
            continue;
        lv_obj_set_style_transform_pivot_x(roots[i], LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(roots[i], LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(roots[i], v10, 0);
    }
}

/* Called from the UI tick (LVGL task): pick the whole-screen orientation
 * (a 90-degree step) from the screen-plane gravity and push it to every
 * screen root. No-op at the resting orientation (transform 0 -> LVGL's
 * direct draw path) and while the pose is unobservable (near-vertical or
 * flat: the in-plane angle is undefined, the last orientation is kept).
 *
 * 100 ms sampling: the gate below (the UI tick is 10 ms awake, 100 ms in
 * light sleep, paused in deep sleep). esp_timer_get_time() is in
 * MICROSECONDS -- the gate compares against us, so no unit conversion is
 * needed (the old roll's us/ms bug made the "calm ramp" run at 500 deg/s;
 * found via the TEST_ROLL_TILT_SEC capture). The decision is integer-only
 * (quantized dot/cross, no atan2, no floats). */
static void screen_orientation_apply(void) {
    if (!CONFIG_UI_IMU_ENABLE)
        return;
    int64_t now_us = esp_timer_get_time();
    int64_t dt_us = now_us - s_ori_last_ms;
    if (dt_us < (int64_t)ORI_SAMPLE_MS * 1000)
        return;
    s_ori_last_ms = now_us;

    int8_t gx_q = 0, gy_q = 0;
    bool have = false;
#if CONFIG_TEST_ROLL_TILT_SEC > 0
    if (s_ori_test_on) {
        /* The script feeds quantized unit vectors directly (|g| = 1 g). */
        gx_q = s_ori_test_gx;
        gy_q = s_ori_test_gy;
        have = true;
    } else
#endif
    {
        float g[3];
        if (imu_is_ready() && imu_read_gravity(g)) {
            /* int4 quantization of each axis (1 level = 1/8 g). */
            gx_q = ori_clamp(lrintf(g[0] * (float)ORI_Q_SCALE), ORI_Q_SCALE);
            gy_q = ori_clamp(lrintf(g[1] * (float)ORI_Q_SCALE), ORI_Q_SCALE);
            have = true;
        }
    }
    if (!have)
        return;
    /* Flat gate (squared, quantized units): with almost no screen-plane
     * gravity the in-plane pose is undefined -- keep the last orientation
     * (the window and the reference are frozen too). */
    if (gx_q * gx_q + gy_q * gy_q < ORI_FLAT_GATE2)
        return;

    if (!s_have_ref) {
        s_ref_qx = gx_q;
        s_ref_qy = gy_q;
        s_have_ref = true;
        return; /* the resting pose is the current pose: orientation 0 */
    }

    /* Push the sample into the 1 s window. */
    s_win_x[s_win_head] = gx_q;
    s_win_y[s_win_head] = gy_q;
    s_win_head = (s_win_head + 1) % ORI_WINDOW;
    if (s_win_count < ORI_WINDOW)
        s_win_count++;
    if (s_win_count < ORI_WINDOW)
        return; /* the running mean needs the full 1 s window */
    int32_t sx = 0, sy = 0;
    for (int i = 0; i < ORI_WINDOW; i++) {
        sx += s_win_x[i];
        sy += s_win_y[i];
    }
    /* The 1 Hz running mean (rounded, int4 scale). */
    int8_t fx = ori_clamp(lrintf((float)sx / (float)ORI_WINDOW), ORI_Q_SCALE);
    int8_t fy = ori_clamp(lrintf((float)sy / (float)ORI_WINDOW), ORI_Q_SCALE);

    /* The settled-pose reference tracks only while settled, so at-rest
     * drift cannot walk the orientation around. Frozen while the headless
     * script is active (otherwise the settled-on-bench state would track
     * the synthetic pose and erase the driven departure). */
#if CONFIG_TEST_ROLL_TILT_SEC > 0
    bool settled = moon_ui_tilt_settled() && !s_ori_test_on;
#else
    bool settled = moon_ui_tilt_settled();
#endif
    if (settled) {
        s_ref_qx = fx;
        s_ref_qy = fy;
    }

    /* The departure: the direction of the filtered gravity from the
     * reference direction -- integer dot/cross only (no atan2). Screen
     * coords (y down): the gravity vector sweeps visually
     * counter-clockwise as the device tilts clockwise, so the quadrant
     * picks the CW-positive tilt step directly. */
    int32_t dot = fx * s_ref_qx + fy * s_ref_qy;
    int32_t cross = fx * s_ref_qy - fy * s_ref_qx;
    if (dot == 0 && cross == 0)
        return;   /* degenerate reference: keep the last orientation */
    int32_t step; /* the device tilt, CW-positive, a 90-degree step */
    /* |dot| >= |cross|: the 0/180 axis (the dot's sign picks the end; the
     * tie goes to the axis). |cross| > |dot|: the 90/270 axis (the
     * cross's sign picks the end). */
    if (dot >= cross && dot >= -cross)
        step = 0;
    else if (dot <= cross && dot <= -cross)
        step = 180;
    else
        step = cross > 0 ? 90 : 270;

    /* Confirm the step on a sustained window of samples (see
     * s_pending_step). The pipeline runs at 10 Hz, so requiring
     * ORI_WINDOW (10) consecutive samples agreeing = ~1 s of sustained
     * agreement: a single transient sample (or a short I2C glitch) is
     * diluted out by the 1 s running mean above, and even a brief
     * spurious step cannot stick for the full second. A deliberate 90
     * degree move is held far longer, so the extra confirmation costs
     * at most ~1 s of latency on a real tilt. */
    if (step == s_pending_step)
        s_pending_count++;
    else {
        s_pending_step = step;
        s_pending_count = 1;
    }
    if (s_pending_count < ORI_WINDOW)
        return;

    /* The correction: counter-rotate the content by the departure so the
     * UI keeps the resting-pose upright (LVGL units: 1/10 deg, positive =
     * clockwise). UI_CLOCK_ROLL_SIGN flips the correction for a mirrored
     * board mount. */
    int32_t v10 = -(int32_t)step * 10;
    if (CONFIG_UI_CLOCK_ROLL_SIGN < 0)
        v10 = -v10;
    if (v10 == s_last_v10)
        return; /* nothing new to draw */
    int32_t old = s_last_v10;
    s_last_v10 = v10;
    ori_apply_v10(v10);
    /* f = the 1 s filtered gravity, ref = the settled reference (int4):
     * the two vectors that produced the step, for diagnosing spurious
     * steps (the filter output should mirror ref while the pose is held). */
    ESP_LOGI(TAG, "screen orientation %+d -> %+d deg (sign %d, f=(%d,%d) ref=(%d,%d))", old / 10, v10 / 10,
             CONFIG_UI_CLOCK_ROLL_SIGN, fx, fy, s_ref_qx, s_ref_qy);
}

#if CONFIG_TEST_ROLL_TILT_SEC > 0
/* Headless tilt script (1 s LVGL timer, LVGL task). After
 * TEST_ROLL_TILT_SEC of boot it runs s_ori_script through the REAL
 * screen_orientation_apply pipeline -- the int4 quantization, the 1 s
 * running mean, the quadrant decision and the LVGL transform layer (the
 * PSRAM alloc + the centre pivot) all execute for real. Idle monitoring
 * is paused for the run (the script would otherwise dusk-sleep mid-way
 * and the clock would be hidden). */
void action_roll_test_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ori_test_on) {
        if (s_ori_test_done)
            return; /* the wait counter stays 0 after the first run */
        if (s_ori_test_wait_s > 0) {
            s_ori_test_wait_s--;
            return;
        }
        /* By the time the wait elapses the device is whatever state its
         * boot flow left it in (usually asleep -- the five-minute alarm
         * rule keeps re-firing the alarm screen, the clock still ticks).
         * If idle, force the real idle-wake path (clears the idle flag
         * BEFORE running the wake callback, exactly like
         * TEST_AUTO_WAKE_SEC); a wake is a static current phase + the
         * on-demand WiFi sync, then the airplane screen below. */
        if (clock_is_idle())
            clock_idle_notify_tilt_activity();
        ESP_LOGW(TAG, "TEST roll tilt: starting (idle monitoring off for %u s)", ORI_SCRIPT_END_S);
        bsp_display_backlight_on();
        clock_timer_disable(); /* the light-sleep countdown may still be armed */
        clock_idle_set_enabled(false);
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        s_ori_test_on = true;
        s_ori_test_elapsed_s = 0;
        s_ori_test_gx = s_ori_script[0].gx;
        s_ori_test_gy = s_ori_script[0].gy;
        return;
    }
    s_ori_test_elapsed_s++;
    if (s_ori_test_elapsed_s > ORI_SCRIPT_END_S) {
        ESP_LOGW(TAG, "TEST roll tilt: done (back at 0? check the 'screen orientation' lines)");
        s_ori_test_on = false;
        s_ori_test_done = true;
        clock_idle_set_enabled(true);
        clock_idle_reset_timer();
        return;
    }
    for (const struct ori_script_step *row = s_ori_script; row->t_s <= ORI_SCRIPT_END_S; row++) {
        if (row->t_s <= s_ori_test_elapsed_s) {
            s_ori_test_gx = row->gx;
            s_ori_test_gy = row->gy;
        }
    }
    ESP_LOGW(TAG, "TEST roll tilt: t+%us g (%d, %d) / 8", s_ori_test_elapsed_s, s_ori_test_gx, s_ori_test_gy);
}
#endif

// Callback for UI tick timer
static void ui_tick_timer_cb(lv_timer_t *timer) {
    // Keep the UI upright: whole-screen 90-degree-step orientation from the
    // screen-plane gravity (no-op at the resting orientation: transform 0
    // -> LVGL direct draw path).
    screen_orientation_apply();
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
