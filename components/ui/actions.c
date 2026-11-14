#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bsp/display.h"

#include "actions.h"
#include "alarm_audio.h"
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
// keep the panel lit (the static dimmed frame with the clock shows
// there). The wake handler and the alarm trigger restore it.
static bool g_backlight_off = false;

// True while the device sits on a sleep screen (light/deep sleep) or on
// the alarm screen. The sleep transitions and the alarm trigger set it;
// the wake handler (action_wake_screen) consumes (clears) it
// to know that its call is a real wake event -- which keeps a stale
// alarm (one that rang while the user slept) from hijacking the wake
// flow. It no longer drives an animation: wakes (INCLUDING a dismissed
// alarm) show the current moon phase static -- the synodic animation
// family is gone (the boot sweep retired 2026-11-13).
static bool g_asleep = false;

/* The WiFi + NTP sync runs on a dedicated task, never on the LVGL task:
 * the wake handler (action_wake_screen) runs in the LVGL task (indev
 * event / moon tilt timer / the RTC minute tick), and the old blocking
 * wifi_manager_init() there parked the LVGL task for the whole ~1.25 s
 * WPA2 handshake -- no LVGL timer fired, so the moon tick and the clock
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
    /* Leak watch (task #29): the wake-sync hammer exhausted the
     * internal heap by the 10th init/deinit cycle. One line per cycle,
     * before and after the radio session -- the free-internal /
     * max-DMA-block pair must return to its pre-run values every
     * cycle or the deltas name the leaking stage. */
    ESP_LOGI(TAG, "heap pre-sync: free-int %u B, max-DMA %u B", (unsigned)esp_get_free_internal_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
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
    /* Airplane NOW, not at the next deinit call site: an ASSOCIATED
     * station keeps waking the modem for DTIM beacons (~300 ms), and
     * that RX churn steals QSPI DMA completions on the S3 AMOLED just
     * like the init storm does -- the wake-sync hammer's first cycle
     * lost the first post-release flush's completion ~4 s after
     * "sync complete" with the STA still connected (task #29). The
     * airplane screen means the modem is DOWN. Skip when init deferred
     * to a scan in flight (ESP_ERR_INVALID_STATE): the scan owns the
     * radio, and tearing it down here would resurrect the mid-scan
     * NO-APS-FOUND race (ce26a83). The hold's tail below then covers
     * exactly the driver teardown this deinit kicks off. */
    if (ret != ESP_ERR_INVALID_STATE) {
        wifi_manager_deinit();
        g_wifi_deinited = true;
    }
    ESP_LOGI(TAG, "heap post-sync: free-int %u B, max-DMA %u B", (unsigned)esp_get_free_internal_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    /* Storm over (either outcome): the tail drains, panel writes resume,
     * and the whole-screen repaint rides the next tick
     * (moon_ui_panel_repaint_pending). */
    moon_ui_panel_traffic_hold(false);
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
        moon_ui_panel_traffic_hold(false); /* no task will ever release it */
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
 * is panel-sized (466^2 RGB565 ~= 0.4 MB on the S3, 720^2 ~= 1.0 MB on
 * the P4) and comes from the C-library malloc (sdkconfig.defaults: clib
 * backend) -> PSRAM; the built-in 64 KB LVGL pool cannot hold it. UI_CLOCK_ROLL_SIGN flips the correction for a
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
/* The base orientation (LVGL units, positive = clockwise): the panel is
 * physically mounted off, so even the "no tilt" state (step 0) displays
 * pre-rotated. Per-board via Kconfig (UI_ORI_BASE_V10): P4 900 (task
 * #54 bench), S3 AMOLED 1800 (bench 2026-11-08 - its content sat 90 deg
 * CCW of true up at rest). The #ifndef seed only serves hosts without
 * the generated sdkconfig; on device the Kconfig value always wins. */
#ifndef CONFIG_UI_ORI_BASE_V10
#define CONFIG_UI_ORI_BASE_V10 900
#endif
#define ORI_BASE_V10 CONFIG_UI_ORI_BASE_V10

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
/* The 2 s press re-sync (action_register_orientation_touch): a press of
 * >= ORI_RESYNC_PRESS_MS on the display asks the pipeline to re-anchor
 * the home pose to the pose currently held. s_press_start_us is stamped
 * on PRESSED; RELEASED sets s_ori_resync_req if the press lasted long
 * enough. Both callbacks and the pipeline run on the LVGL task (the
 * indev events land in lv_timer_handler), so no atomics are needed.
 *
 * The request is only ACTIONED while the pose is stable (s_stable_count,
 * below) and, once an anchor exists, on the anchor's home axis -- see
 * the resync block in screen_orientation_apply. A palm on the screen
 * while the device is being flipped releases into a pose that is not
 * stable yet (and a 90-degree pose is off-axis), so it is dropped: the
 * un-gated version adopted the mid-flip pose as home and broke the
 * flips (the 2026-10-30 "flipping does not reliably work" report). */
#define ORI_RESYNC_PRESS_MS 2000
static int64_t s_press_start_us;
static bool s_in_press;
static bool s_ori_resync_req;
/* Pose stability: the 1 s filtered gravity must not move more than one
 * int4 level (1/8 g) for ORI_STABLE_SAMPLES (~1.5 s) before the pose may
 * anchor (the boot seed) or be re-anchored (the 2 s press). At rest the
 * filtered mean wanders far less than one level per 100 ms sample (the
 * at-rest drift is ~0.5 deg/s = ~0.005 level/s), so the count
 * accumulates; any deliberate move resets it. */
#define ORI_STABLE_SAMPLES 15
static int8_t s_stable_x, s_stable_y;
static int s_stable_count;

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
 * centre pivot) runs on the device without a physical tilt. With the
 * base orientation (+90 deg, ORI_BASE_V10) the applied values log as
 * +90 -> 0 -> -90 -> -180 -> +90 (step 0 now shows the base, not 0). */
#ifndef CONFIG_TEST_ROLL_TILT_SEC
#define CONFIG_TEST_ROLL_TILT_SEC 0
#endif
#ifndef CONFIG_UI_DEMO_MODE
#define CONFIG_UI_DEMO_MODE 0
#endif
/* The synthetic-gravity path compiles for the headless test hook AND
 * for the demo tour's roll phase (action_roll_test_begin/busy arm it
 * there instead of the boot-time wait). */
#if CONFIG_TEST_ROLL_TILT_SEC > 0 || CONFIG_UI_DEMO_MODE
#define ROLL_SCRIPT 1
#else
#define ROLL_SCRIPT 0
#endif
#if ROLL_SCRIPT
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
/* The test hook self-arms CONFIG_TEST_ROLL_TILT_SEC after boot; a
 * demo-only build (TEST=0) must NEVER self-arm -- the demo re-arms one
 * run per cycle through action_roll_test_begin(), so park the counter
 * out of reach until then. */
static uint32_t s_ori_test_wait_s = (CONFIG_TEST_ROLL_TILT_SEC > 0) ? CONFIG_TEST_ROLL_TILT_SEC : UINT32_MAX;
static uint32_t s_ori_test_elapsed_s;
static int8_t s_ori_test_gx, s_ori_test_gy;
#endif /* ROLL_SCRIPT */

/* Push one orientation to all 12 screen roots (LVGL transform units:
 * 1/10 deg, positive = clockwise). The pivot is the panel centre (LVGL's
 * default is the top-left, which swung the full-screen container off the
 * panel -- the old roll's first crash). A 90-degree step of the square
 * panel maps the panel onto itself, so one push is the whole change (the
 * old ramp's intermediate frames are gone). The rotated root's layer is
 * panel-sized (~0.4 MB RGB565 on the 466 panel, ~1.0 MB on the P4's 720)
 * and comes from the C-library malloc (clib backend, sdkconfig.defaults)
 * -> PSRAM; the built-in 64 KB LVGL pool cannot hold it. Every screen's widgets are children of its root, so the
 * one transform covers the clock, the seconds arcs, the settings and the
 * alarm screens alike; LVGL's hit test is inverse-mapped through the
 * transform (lv_indev_search_obj), so touch lands on the right widget in
 * any orientation. */
static void ori_apply_v10(int32_t v10) {
    lv_obj_t *roots[16] = {
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
        objects.settings_wifi_password,
        objects.settings_time,
        objects.settings_tz,
        objects.settings_alarm_sound,
    };
    for (int i = 0; i < 16; i++) {
        if (!roots[i])
            continue;
        lv_obj_set_style_transform_pivot_x(roots[i], LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(roots[i], LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(roots[i], v10, 0);
    }
}

/* Push the base orientation to every screen root (app_main, after
 * ui_init: the roots must exist). The IMU's first sample can arrive
 * well after the first frame is drawn, and the pipeline only pushes on
 * a CHANGE -- so the panel's 90 deg CW mount offset must be in place
 * from frame one. Seeds s_last_v10 so the pipeline's first step-0
 * evaluation dedupes (no double push). */
void action_orientation_init(void) {
    int32_t v10 = ORI_BASE_V10;
    if (CONFIG_UI_CLOCK_ROLL_SIGN < 0)
        v10 = -v10;
    s_last_v10 = v10;
    ori_apply_v10(v10);
}

/* Indev callback for the 2 s press re-sync: fires on every press
 * regardless of the hit-tested object (like the alarm dismiss callback
 * below it). The LVGL event queue drains it on the LVGL task inside
 * lv_timer_handler, so the shared state above is single-task. */
static void orientation_resync_indev_cb(lv_event_t *e) {
    lv_indev_state_t st = lv_indev_get_state(lv_event_get_target(e));
    if (st == LV_INDEV_STATE_PRESSED) {
        s_press_start_us = esp_timer_get_time();
        s_in_press = true;
    } else if (st == LV_INDEV_STATE_RELEASED && s_in_press) {
        s_in_press = false;
        if (esp_timer_get_time() - s_press_start_us >= (int64_t)ORI_RESYNC_PRESS_MS * 1000) {
            s_ori_resync_req = true;
            ESP_LOGI(TAG, "orientation resync requested (2 s press)");
        }
    }
}

/* Register the 2 s press re-sync on the (one) input device. The
 * pipeline's home-pose anchor latches from the first 1 s of IMU
 * samples after boot; if the device was flipped while that window
 * filled, the UI is upside down and no tilt recovers it (the T4 rule
 * re-tracks only on step 0). A deliberate 2 s press re-anchors the
 * home pose to the pose held while pressing, and the pipeline pushes
 * the base orientation on its next sample (~100 ms). */
esp_err_t action_register_orientation_touch(void) {
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) {
        ESP_LOGE(TAG, "no input device; orientation resync unavailable");
        return ESP_FAIL;
    }
    lv_indev_add_event_cb(indev, orientation_resync_indev_cb, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(indev, orientation_resync_indev_cb, LV_EVENT_RELEASED, NULL);
    ESP_LOGI(TAG, "Orientation resync (2 s press) registered");
    return ESP_OK;
}

/* Called from the UI tick (LVGL task): pick the whole-screen orientation
 * (a 90-degree step) from the screen-plane gravity and push it to every
 * screen root. No-op while the pose is unobservable (near-vertical or
 * flat: the in-plane angle is undefined, the last orientation is kept).
 * With the base offset the resting transform is +90 (not 0), so the
 * rotated root's panel-sized PSRAM layer (~0.4 MB / ~1.0 MB) is always
 * allocated; the pipeline
 * itself still only PUSHES on a change (the s_last_v10 dedupe).
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
#if ROLL_SCRIPT
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

    /* Pose stability (see s_stable_count): the 1 s filtered mean must
     * hold within one int4 level of the previous filter output for
     * ORI_STABLE_SAMPLES consecutive samples (~1.5 s) before the pose
     * may anchor or be re-anchored. At rest the filtered mean wanders
     * far less than one level per sample, so the count accumulates;
     * any deliberate move (or a palm pressing on the screen while the
     * device is being flipped) resets it. */
    if (s_stable_count > 0) {
        int dx = fx - s_stable_x, dy = fy - s_stable_y;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dx <= 1 && dy <= 1)
            s_stable_count++;
        else
            s_stable_count = 0;
    } else {
        s_stable_count = 1;
    }
    s_stable_x = fx;
    s_stable_y = fy;
    bool stable = s_stable_count >= ORI_STABLE_SAMPLES;

    /* A 2 s press re-syncs the home pose (the request is set by the
     * indev callback, action_register_orientation_touch). The anchor
     * latches from the first 1 s of samples after boot; if the device
     * was flipped while that window filled, the anchor is 180 deg off
     * and the UI stays upside down forever (the T4 rule re-tracks only
     * on step 0, so no tilt can self-heal it -- the user just sees a
     * flipped display). The press is the user's "this pose is the home
     * pose": re-anchor the reference to the current filtered gravity
     * and push the base orientation immediately. */
    if (s_ori_resync_req) {
        s_ori_resync_req = false;
#if ROLL_SCRIPT
        if (s_ori_test_on) {
            ESP_LOGW(TAG, "orientation resync ignored (test script active)");
            return;
        }
#endif
        if (!stable) {
            ESP_LOGW(TAG, "orientation resync dropped (pose not stable; the press ends while moving)");
            return;
        }
        if (s_have_ref) {
            /* The press is a home-pose declaration: it must sit on the
             * current anchor's home axis -- within ~35 degrees of
             * upright or upside-down (|dot|^2 >= 3 |cross|^2, integer
             * math, same quadrant test as the step decision below). A
             * palm released while the device lies on its side, or mid
             * flip, is off-axis and must NOT re-anchor home to that
             * pose: the un-gated version did exactly that and broke
             * the flips (the 2026-10-30 reliability report). */
            int32_t d = fx * s_ref_qx + fy * s_ref_qy;
            int32_t c = fx * s_ref_qy - fy * s_ref_qx;
            int32_t d2 = d < 0 ? -d : d;
            int32_t c2 = c < 0 ? -c : c;
            if (d2 * d2 < 3 * c2 * c2) {
                ESP_LOGW(TAG, "orientation resync dropped (pose off the home axis, f=(%d,%d) ref=(%d,%d))", fx, fy,
                         s_ref_qx, s_ref_qy);
                return;
            }
        }
        ESP_LOGI(TAG, "orientation resync: 2 s press re-anchors the home pose (ref (%d,%d) -> (%d,%d))", s_ref_qx,
                 s_ref_qy, fx, fy);
        s_ref_qx = fx;
        s_ref_qy = fy;
        s_have_ref = true;
        int32_t v10 = ORI_BASE_V10;
        if (CONFIG_UI_CLOCK_ROLL_SIGN < 0)
            v10 = -v10;
        s_pending_step = 0;
        s_pending_count = 0;
        if (v10 != s_last_v10) {
            int32_t old = s_last_v10;
            s_last_v10 = v10;
            ori_apply_v10(v10);
            ESP_LOGI(TAG, "screen orientation %+d -> %+d deg (resync)", old / 10, v10 / 10);
        }
        return;
    }

    /* Seed the settled-pose reference from the 1 s FILTERED mean, not
     * a single raw sample: the old first-sample seed anchored the home
     * pose on whatever the IMU reported in the first non-flat sample
     * (a transient while the device was being placed, picked up, or
     * woken -> the home pose was off by a 90-degree step "from time to
     * time"). The mean needs the full 1 s window, so the first ~1 s
     * after the pipeline enables is seed-building (orientation stays
     * at the base, unchanged) and the anchor is the settled pose. The
     * stability gate (stable, above) on top of the 1 s window means the
     * device must sit still for ~1.5 s before the anchor takes -- a
     * boot while the device is still being placed cannot latch a
     * transient pose as home. */
    if (!s_have_ref) {
        if (!stable)
            return; /* seed-building: wait for the settled pose */
        s_ref_qx = fx;
        s_ref_qy = fy;
        s_have_ref = true;
        return; /* the resting pose is the current pose: orientation 0 */
    }

    /* The departure: the direction of the filtered gravity from the
     * reference direction -- integer dot/cross only (no atan2). Screen
     * coords (y down): the gravity vector sweeps visually
     * counter-clockwise as the device tilts clockwise, so the quadrant
     * picks the CW-positive tilt step directly. The step is decided
     * BEFORE the reference re-tracks: a fast 180-degree flip settles into
     * the NEW pose, and re-tracking there would adopt it as the new home
     * (the departure would read 0 and the upside-down UI would be
     * confirmed). The reference re-tracks only on step 0 -- the device
     * returning to the home pose -- so a deliberate re-pose keeps its
     * absolute correction instead of being re-anchored. (Frozen while the
     * headless script is active: the settled-on-bench state would track
     * the synthetic pose and erase the driven departure.) */
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

#if ROLL_SCRIPT
    bool settled = moon_ui_tilt_settled() && !s_ori_test_on;
#else
    bool settled = moon_ui_tilt_settled();
#endif
    if (settled && step == 0) {
        s_ref_qx = fx;
        s_ref_qy = fy;
    }

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
     * UI keeps the resting-pose upright, plus the base orientation (the
     * panel's 90 deg CW mount offset; step 0 shows the base, not 0).
     * LVGL units: 1/10 deg, positive = clockwise. UI_CLOCK_ROLL_SIGN flips
     * the whole orientation for a mirrored board mount. */
    int32_t v10 = ORI_BASE_V10 - (int32_t)step * 10;
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

#if ROLL_SCRIPT
/* Headless tilt script (1 s LVGL timer, LVGL task). After
 * TEST_ROLL_TILT_SEC of boot it runs s_ori_script through the REAL
 * screen_orientation_apply pipeline -- the int4 quantization, the 1 s
 * running mean, the quadrant decision and the LVGL transform layer (the
 * PSRAM alloc + the centre pivot) all execute for real. Idle monitoring
 * is paused for the run (the script would otherwise idle-sleep mid-way
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
        moon_ui_panel_brightness_set(100); /* the panel-io-serialized backlight */
        clock_timer_disable();             /* the light-sleep countdown may still be armed */
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
        /* The demo owns the idle lifecycle (its per-second tick holds
         * the timer) and so does the boot replay while it presents
         * (its tick end leg re-arms) -- only the stand-alone hook on a
         * quiet UI re-arms monitoring. */
        if (!action_demo_active() && !moon_ui_replaying()) {
            clock_idle_set_enabled(true);
            clock_idle_reset_timer();
        }
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

/* Arm one script run (the demo tour's roll phase: a demo-only build
 * never self-arms, so this is the only starter there; in a test-hook
 * build it can re-run the script after the boot one). Call from the
 * LVGL task, then poll action_roll_test_busy() until false (~27 s). */
void action_roll_test_begin(void) {
    s_ori_test_done = false;
    s_ori_test_on = false;
    s_ori_test_wait_s = 1;
}

/* True from begin() until the script's last step has run. */
bool action_roll_test_busy(void) { return !s_ori_test_done; }
#endif /* ROLL_SCRIPT */

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
    /* The demo mode (UI_DEMO_MODE) runs until the next reset: a sync
     * completing under it (a boot sync that outlived the boot bake) must
     * not hand the display off to the airplane screen mid-recording.
     * The flag is left unclaimed -- the demo owns the display. */
    if (!s_wifi_sync_running && s_wifi_sync_done && !action_demo_active()) {
        s_wifi_sync_done = false;
        if (!clock_is_idle()) {
            if (ui_current_screen() == SCREEN_ID_WIFI) {
                loadScreen(SCREEN_ID_MODEM_SLEEP);
                ui_tick();
            }
            /* The idle monitoring re-arms right here UNLESS the boot
             * replay is still presenting (UI_MOON_BOOT_REPLAY -- this
             * very hand-off regularly overlaps boot, the P4's on-demand
             * sync runs ~30 s through the remote C6): the replay holds
             * the ladder itself and the tick's replay end leg re-arms.
             * (The old sweeps retired family-wide -- clocks 2026-11-13,
             * the knob 2026-11-14; the knob's boot presentation holds
             * its OWN ladder in knob_ui.) */
            if (!moon_ui_replaying()) {
                clock_idle_set_enabled(true);
                clock_idle_reset_timer();
            }
            ESP_LOGI(TAG, "WiFi sync %s -> airplane screen", s_wifi_sync_ok ? "complete" : "failed");
        } else {
            ESP_LOGI(TAG, "WiFi sync done but device is asleep; airplane hand-off deferred");
        }
    }
    /* The WiFi scan's panel traffic hold follows the SCAN, not the
     * settings screen that started it (task #30): leaving mid-scan keeps
     * the guard; this poll releases it when the radio session is really
     * over. Registered BEFORE the repaint poll below so the release's
     * stamp is seen by the same tick's expiry check. */
    screens_wifi_scan_hold_poll();
    /* A released panel traffic hold (the wake-sync storm) dropped flushes on
     * purpose: invalidate the active screen once so LVGL re-emits the
     * whole panel and the panel re-anchors. */
    if (moon_ui_panel_repaint_pending()) {
        ESP_LOGI(TAG, "panel repaint: whole-screen invalidate of the active screen");
        lv_obj_invalidate(lv_screen_active());
    }
    ui_tick();
}

/* ---------------- on-demand WiFi + NTP sync (the transient WiFi screen) ---
 *
 * The device lives on the airplane (modem-sleep) screen; WiFi is
 * activated on demand (boot, wake, the hourly tick, the settings hub's
 * exit button) purely to refresh the RTC, then the sync flow drops
 * straight back to airplane. The WiFi screen hosts the handshake; it
 * and the airplane screen share the one
 * moon image, so the hand-off is the atomic whole-panel flush with no
 * visible seam.
 */

/* The transient WiFi screen is only shown while a WiFi + NTP sync is in
 * flight (wake / manual / hourly / boot-NTP-failed). The boot hand-off
 * poller is gone: at boot the clock is acquired by the blocking boot
 * sync BEFORE the UI appears, so the success path lands straight on the
 * home (airplane) screen, where the boot bake shows the static phase
 * (both screens share the one moon image, s_rgb). The NTP-failed boot path shows the
 * WiFi screen via action_start_wifi_sync (above) and drops to airplane
 * through the s_wifi_sync_done pickup in ui_tick_timer_cb -- no poller. */

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

    // Un-dim the panel (the sleep -> WiFi wake switch is a BOTH-MOON
    // switch: no fade-in would restore the brightness). No-op when the
    // panel was never dimmed (boot / hourly / settings-hub syncs).
    moon_ui_panel_dim_ramp(100);

    ESP_LOGI(TAG, "WiFi screen loaded (on-demand sync)");
    /* Freeze panel io for the sync task's radio storm: esp_wifi_init's
     * firmware load + RF calibration takes in-flight QSPI DMA completions
     * (the S3 AMOLED's wake+~6.3 s panel-io watchdog restart, task #29).
     * Arm right before the kickoff spawns that task -- the screen load and
     * the dim ramp above ride the bus normally, BEFORE the storm. */
    moon_ui_panel_traffic_hold(true);
    wifi_sync_kickoff();
}

/* Claim the finished sync (the demo's SYNC phase). The UI tick hands
 * the finished sync off to the airplane screen only while the demo is
 * NOT running (it leaves the flag unclaimed under the demo), so this
 * consumes what the tick would have taken; returns true once per
 * completion. */
bool action_sync_demo_done(bool *ok) {
    if (!s_wifi_sync_done)
        return false;
    s_wifi_sync_done = false;
    if (ok)
        *ok = s_wifi_sync_ok;
    return true;
}

// Global reference to UI tick timer for dynamic adjustment
static lv_timer_t *g_ui_tick_timer = NULL;

// Global reference to seconds increment timer
static lv_timer_t *g_seconds_timer = NULL;

#ifdef CONFIG_BOARD_KNOB
/* The knob's boot-time stand-in for the wake flow's timer bootstrap.
 * On the clocks the 10 ms UI-tick timer and the 1 s seconds-increment
 * timer arrive with action_wake_screen / action_switch_to_airplane_screen
 * (the wake/idle pair the knob deliberately does not run -- knob_power
 * owns its ladder); without this pair the shared home's seconds var
 * only moved at the minute-tick write and the arc sat between minutes.
 * This is the bare create/resume from those handlers WITHOUT their
 * wake side work (no backlight restore, no idle re-enable, no alarm
 * bookkeeping) -- lv_timer_handler then pumps ui_tick() and the
 * seconds increment exactly like the clocks'. Called in the knob's
 * single-threaded creation phase before lvgl_task starts: no locking,
 * same posture as create_screens. Guarded knob-only: the clocks never
 * compile it (their binaries stay identical). */
void action_ensure_ui_timers(void) {
    if (!g_ui_tick_timer) {
        g_ui_tick_timer = lv_timer_create(ui_tick_timer_cb, 10, NULL);
    } else {
        lv_timer_resume(g_ui_tick_timer);
        lv_timer_set_period(g_ui_tick_timer, 10);
    }
    if (!g_seconds_timer) {
        g_seconds_timer = lv_timer_create(seconds_increment_timer_cb, 1000, NULL);
    } else {
        lv_timer_resume(g_seconds_timer);
    }
    ESP_LOGI(TAG, "UI + seconds lv_timers up (knob boot bootstrap)");
}
#endif /* CONFIG_BOARD_KNOB */

// Configuration with fallback defaults if Kconfig not available
#ifndef CONFIG_UI_LIGHT_SLEEP_TICK_MS
#define CONFIG_UI_LIGHT_SLEEP_TICK_MS 100
#endif

#ifndef CONFIG_UI_LIGHT_SLEEP_DIM_PCT
#define CONFIG_UI_LIGHT_SLEEP_DIM_PCT 25
#endif

/* The shared clock widget's light-sleep opacity (percent). Kconfig
 * UI_CLOCK_ALPHA_PCT (default 20): the light-sleep clock's "alpha" -- the
 * panel has no true alpha channel, so the clock's own opacity IS the
 * alpha over the already hardware-dimmed (UI_LIGHT_SLEEP_DIM_PCT) panel.
 * 20 % is the original "barely visible" look; raise it for a brighter
 * clock. Deep sleep keeps the dead display (backlight off, clock hidden),
 * so this is light-sleep only. */
#ifndef CONFIG_UI_CLOCK_ALPHA_PCT
#define CONFIG_UI_CLOCK_ALPHA_PCT 20
#endif

/* Refresh the alarm label + weekday prefix (defined below, after the
 * clock display: called from the minute tick too). */
static void refresh_alarm_display(const clock_time_t *now);

/* ---- demo mode (UI_DEMO_MODE) --------------------------------------
 *
 * The scripted tour lives in demo.c (the phase engine, the MM:SS
 * odometer second, the virtual-pointer touch injection and the serial
 * phase markers; docs/verification/demo-mode). actions.c keeps only
 * the hooks the demo needs from the action layer: the clock label's
 * MM:SS/HH:MM choice, the sleep/alarm suppression guards below, the
 * roll-script entry points and the sync-done poll. action_demo_active
 * is DEFINED in demo.c (both the on and the off build).
 */

/* Refresh the shared clock widget's label from the wall-clock digits:
 * HH:MM normally, MM:SS in the demo mode. The minute tick, the
 * screen-switch attach and the demo's per-second tick all go through
 * this, so the format follows the mode at every write point (no
 * HH:MM write can fight the demo's MM:SS). */
void action_clock_widget_label(int hour, int minute, int second) {
    char s[8];
    if (action_demo_active())
        snprintf(s, sizeof(s), "%02d:%02d", minute, second);
    else
        snprintf(s, sizeof(s), "%02d:%02d", hour, minute);
    set_var_clock_label(s);
}

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
     * wake re-syncs), no interference with a boot animation (none
     * exists family-wide since 2026-11-14), no yanking
     * a settings session away. */
    if (ret == ESP_OK && current_time.minute == 0 && !clock_is_idle() && !action_demo_active() &&
        !s_wifi_sync_running && !wifi_manager_is_connected() && clock_sntp_needs_sync() &&
        ui_current_screen() == SCREEN_ID_MODEM_SLEEP) {
        ESP_LOGI(TAG, "Hourly sync due at the top of the minute; activating WiFi");
        action_start_wifi_sync();
        return;
    }

    if (ret == ESP_OK) {
        /* HH:MM -- or MM:SS while the demo runs (action_clock_widget_label
         * picks the format from the mode; the demo's per-second tick owns
         * the label then, but writing the same value is a no-op for the
         * slot row). The seconds var feeds the arc (both formats share it
         * via the widget's format, not the var). */
        action_clock_widget_label(current_time.hour, current_time.minute, current_time.second);
        set_var_clock_seconds(current_time.second);
        /* The alarm's weekday prefix flips exactly at the 24 h boundary
         * and the time after a fire/re-arm, so refresh it here, at the
         * minute tick (the vars change lands via ui_tick below). */
        refresh_alarm_display(&current_time);
    } else {
        ESP_LOGE(TAG, "Failed to get time from RTC: %s", esp_err_to_name(ret));
        set_var_clock_label("--:--");
        set_var_clock_seconds(0);
    }

    // Re-bake the moon once per calendar day (cheap probe: compares day numbers).
    moon_ui_day_tick();

    // Trigger UI update immediately to reflect the change
    ui_tick();

    // The ONE shared clock widget is ticked HERE from the vars on every
    // minute tick -- regardless of the active screen (the per-screen
    // tick functions no longer tick it): a minute flip redraws the
    // digits wherever the widget is attached (a settings screen keeps
    // it computed-but-unrendered on the last moon screen).
    /* 17 = the shared flex widget's objects_t slot (shifted +1 when
     * settings_alarm_sound joined the screen roots); +1 inside the tick
     * is the HH:MM slot row. */
    tick_user_widget_clock(17);
}

/* Two-letter weekday of the next fire, for the main-screen alarm prefix
 * (0=Sunday .. 6=Saturday -- the clock component's rule-mask convention).
 * Zeller's congruence; the alarm scheduler carries the same static copy. */
static const char *const s_day_abbr[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

static int day_of_week_civil(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year--;
    }
    int q = day, m = month, k = year % 100, j = year / 100;
    int h = (q + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;
    return (h + 6) % 7;
}

/* Days since 1970-01-01 (proleptic Gregorian, civil calendar) -- the
 * 24-hour window arithmetic for the prefix rule, minute precision. */
static int64_t days_from_civil(int y, int m, int d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400; /* [0, 399] */
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

/**
 * @brief Refresh the main-screen alarm label + weekday prefix.
 *
 * Shows the NEXT scheduled fire (scheduler cache -- no I2C). The prefix
 * rule: a fire within the next 24 h needs no day prefix; beyond that the
 * two-letter weekday of the fire day is shown (e.g. a Friday-only 20:00
 * rule reads "Su 20:00" on Saturday, but plain "20:00" from Friday
 * 20:00 onwards). No scheduled rule -> "--:--", no prefix.
 */
static void refresh_alarm_display(const clock_time_t *now) {
    char alarm_str[6];
    char prefix[4] = {0};
    clock_time_t next;

    if (clock_alarm_get_next_scheduled(&next) == ESP_OK) {
        snprintf(alarm_str, sizeof(alarm_str), "%02d:%02d", next.hour, next.minute);
        const int64_t now_s =
            days_from_civil(now->year, now->month, now->day) * 86400 + (int64_t)now->hour * 3600 + now->minute * 60;
        const int64_t next_s =
            days_from_civil(next.year, next.month, next.day) * 86400 + (int64_t)next.hour * 3600 + next.minute * 60;
        if (next_s - now_s > 86400)
            snprintf(prefix, sizeof(prefix), "%s", s_day_abbr[day_of_week_civil(next.year, next.month, next.day)]);
    } else {
        snprintf(alarm_str, sizeof(alarm_str), "--:--");
    }
    set_var_alarm_label(alarm_str);
    set_var_alarm_prefix(prefix);
}

/**
 * @brief Update the alarm display (boot + after every rule write)
 */
void action_update_alarm_display(lv_event_t *e) {
    (void)e;
    clock_time_t alarm_time;
    esp_err_t ret = clock_get_time(&alarm_time);
    if (ret == ESP_OK) {
        refresh_alarm_display(&alarm_time);
        ESP_LOGI(TAG, "Alarm display: %s %s", get_var_alarm_prefix(), get_var_alarm_label());
    } else {
        ESP_LOGW(TAG, "Failed to read time for the alarm display: %s", esp_err_to_name(ret));
        set_var_alarm_label("--:--");
        set_var_alarm_prefix("");
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
#ifdef CONFIG_UI_DEMO_MODE
/* The demo's ALARM phase deliberately rings the alarm (screen + audio)
 * for the camera; every OTHER fire (a scheduled RTC/one-shot alarm at
 * the wall-clock time) belongs to real use, not the tour, and would
 * hijack the recording. action_alarm_triggered only runs while this
 * flag is set when the demo itself fires it. */
static bool s_demo_alarm_allow = false;
void action_demo_allow_alarm(bool allow) { s_demo_alarm_allow = allow; }
#endif

void action_alarm_triggered(lv_event_t *e) {
    ESP_LOGI(TAG, "Alarm triggered!");

#ifdef CONFIG_UI_DEMO_MODE
    if (action_demo_active() && !s_demo_alarm_allow) {
        ESP_LOGI(TAG, "Scheduled alarm suppressed (demo mode owns the alarm screen)");
        return;
    }
#endif

    // An alarm can fire from DEEP SLEEP (the panel backlight is off
    // there): light the panel before the alarm screen is shown.
    if (g_backlight_off) {
        if (moon_ui_panel_brightness_set(100) == ESP_OK) {
            g_backlight_off = false;
            ESP_LOGI(TAG, "Alarm: panel backlight restored");
        } else {
            ESP_LOGW(TAG, "Failed to restore the panel backlight for the alarm screen");
        }
    }

    // Mark alarm as active so wake callback knows we're dismissing alarm
    g_alarm_active = true;

    // The ring (both clocks: the board's ES8311 speaker with the gradual
    // volume ramp -- the S3 rings too, bench-verified with the Two-Tone
    // loop; only the knob is the codec-less silent board behind the stub).
    // The touch dismiss stops it in action_wake_screen (every dismiss path
    // runs through there).
    alarm_audio_ring_start();

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

    /* Demo mode: no sleep transitions during the tour. The idle
     * monitor is switched off at the demo's arm, but a countdown armed
     * BEFORE the demo took over (or armed by a leftover light-sleep
     * entry) can still expire -- a dead-black panel
     * mid-recording would ruin the take, so refuse the transition and
     * disarm the source. */
    if (action_demo_active()) {
        ESP_LOGI(TAG, "Deep-sleep transition suppressed (demo mode)");
        clock_timer_disable();
        return;
    }

#if !CONFIG_CLOCK_USE_PCF85063A
    // P4: no RTC ALM wake. The device's "deep sleep" is app-level (no
    // esp_deep_sleep -- the crystal clock keeps running through it), so
    // the alarm could still fire from the dark screen; but the user's
    // power decision stands: while an alarm is pending the device stays
    // in LIGHT sleep (dim panel, static frame) instead of the deep
    // state (backlight off, clock hidden). The countdown already expired
    // (that's why we're here), so simply do not transition -- the
    // light-sleep screen stays up and the software one-shot keeps its
    // armed state.
    {
        clock_time_t pending;
        if (clock_alarm_get_next_scheduled(&pending) == ESP_OK) {
            ESP_LOGI(TAG, "Alarm pending (%02d:%02d) -- staying in light sleep (no deep-sleep wake on the P4)",
                     pending.hour, pending.minute);
            return;
        }
    }
#endif

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

    // Update UI one final time to show current state (the static dimmed
    // frame, one last atomic flush)
    ui_tick();

    // Deep sleep turns the panel backlight OFF (the sleep screens
    // before it kept it on). A wake (touch, tilt or an alarm) restores
    // it (action_wake_screen / action_alarm_triggered).
    if (!g_backlight_off) {
        if (moon_ui_panel_brightness_set(0) == ESP_OK) {
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

    // The touch dismiss (snooze) kills the ring if one is sounding: every
    // dismiss path (the awake touch, the asleep idle-wake, and the airplane
    // branch below) runs through here.
    alarm_audio_ring_stop();

    // A wake from deep sleep lights the panel again (the deep-sleep
    // transition turned the backlight off).
    if (g_backlight_off) {
        if (moon_ui_panel_brightness_set(100) == ESP_OK) {
            g_backlight_off = false;
            ESP_LOGI(TAG, "Wake: panel backlight restored");
        } else {
            ESP_LOGW(TAG, "Failed to restore the panel backlight");
        }
    }

    // Restore the shared clock widget's full opacity: the light-sleep
    // entry dimmed the light_sleep_clock container to UI_CLOCK_ALPHA_PCT
    // (the panel dim ramp below / to 100 is the readability; the clock
    // itself is no longer alpha-dimmed). The widget reparents to the new
    // screen's container on the switch, so this also keeps the container
    // clean for the next light-sleep entry.
    lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_COVER, 0);

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

    // Re-enable and restart idle monitoring for automatic sleep
    // transitions -- UNLESS the boot replay is still presenting (a touch
    // during boot wakes the screen mid-animation): the replay holds the
    // ladder and its tick end leg re-arms when the presentation is over.
    if (!moon_ui_replaying()) {
        clock_idle_set_enabled(true);
        clock_idle_reset_timer();
    }
    g_idle_timer_active = true;
    ESP_LOGI(TAG, "Idle monitoring enabled - will transition to light sleep after %d seconds",
             CONFIG_CLOCK_IDLE_TIMEOUT_SEC);

    // Normal (active) mode: the moon re-bakes if the
    // calendar day rolled over while we slept. Wakes show the current
    // phase STATIC -- no animation on wake (the whole sweep/dusk family
    // retired 2026-11-13; moon_ui_pause(false) only defers a re-bake
    // when the sleep entry joined a mid-flight bake).
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

    // Deinitialize WiFi if it's running.
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

    // Enable idle monitoring for automatic sleep transitions -- unless
    // the boot replay is still presenting (its end leg re-arms; see the
    // wake-screen site).
    if (!moon_ui_replaying()) {
        clock_idle_set_enabled(true);
        clock_idle_reset_timer();
    }
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

    // Un-dim the panel: the sleep screens are a BOTH-MOON switch, so the
    // wake's screen switch takes no fade (no fade-in to restore from) --
    // this ramp back to full brightness is the restore. No-op when the
    // panel was never dimmed (wake from the TEST hooks / boot / the
    // already-lit alarm dismiss).
    moon_ui_panel_dim_ramp(100);

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

    // Deinitialize WiFi/network to save power (airplane / modem sleep).
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

    // Sleep mode: the moon stays STATIC under the sleep screens. The
    // light-sleep "dusk" pole spin was removed 2026-11-13 (the dim
    // below is the whole sleep transition), so the RTC countdown for
    // the deep-sleep transition is armed right at the entry -- sleep
    // depth no longer follows an animation.
    moon_ui_pause(true);
    esp_err_t ret =
        clock_timer_enable(settings_deep_sleep_countdown_sec(), CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT);
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "RTC countdown %u s to deep sleep armed", (unsigned)settings_deep_sleep_countdown_sec());
    else
        ESP_LOGE(TAG, "Failed to start RTC countdown timer: %s", esp_err_to_name(ret));

    // Load light sleep screen using EEZ Studio function
    loadScreen(SCREEN_ID_LIGHT_SLEEP);

    // Light sleep dims the clock to UI_CLOCK_ALPHA_PCT (the shared clock
    // widget's container; the style cascades to the attached widget). This
    // IS the clock's "alpha": the panel has no true alpha channel, so the
    // clock's own opacity is the alpha over the already hardware-dimmed
    // (UI_LIGHT_SLEEP_DIM_PCT) panel. The default 20 % is the original
    // "barely visible" look; raise it for a brighter clock. The moon image
    // is a sibling of the clock container, so it stays at full contrast
    // relative to the clock.
    lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_MAX * CONFIG_UI_CLOCK_ALPHA_PCT / 100, 0);

    // And the WHOLE display glows down: the panel's own hardware
    // brightness ramps from its current level (100 at the first sleep,
    // or the already-dimmed level on a re-entry) to the configured
    // sleep level, paced like the screen-switch fade. This is a
    // both-moon screen (no fade switch), so nothing else would dim it.
    // The frame is static -- nothing displays behind the ramp (the
    // ramp's 0x51 steps hold off the other brightness writers). The
    // wake ramps back to 100 (no-op when the panel was never dimmed --
    // e.g. the TEST hook wakes).
    moon_ui_panel_dim_ramp(CONFIG_UI_LIGHT_SLEEP_DIM_PCT);

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
