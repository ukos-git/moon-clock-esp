/**
 * @file knob_dial.c
 * @brief P2 dial input: encoder deltas -> screens_dial_step, background
 *        tap -> screens_dial_commit, every turn -> ladder activity.
 *
 * The encoder (knob_board_encoder.c, vendor pulse-pair scheme) counts
 * debounced detents (+1 CW / -1 CCW); this task consumes the count one
 * detent at a time. Each step takes the LVGL mutex (the same one the
 * LVGL task holds across lv_timer_handler); activity is reported
 * BEFORE the lock so a contended dial never delays the un-dim.
 *
 * Tap-to-commit: LVGL delivers the CLICKED of a press that hit no
 * clickable object to the screen root (the same fallback the home
 * tap -> hub gesture rides), so the commit listener lives on each
 * settings root. It runs inside the LVGL task's lock already (indev
 * callbacks fire under lv_timer_handler) — screens_dial_commit does
 * not lock; the callers own the contract (screens.c header comment).
 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_dial.h"
#include "knob_power.h"
#include "knob_ui.h"
#include "lvgl.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"

static const char *TAG = "knob_dial";

/* Background tap (no clickable target): commit the focused widget.
 * LVGL-task context: the mutex is held by lv_timer_handler around us. */
static void dial_tap_commit_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "root CLICKED -> dial commit"); /* bench: the background
                                                       * tap reached the root */
        screens_dial_commit();
    }
}

/* Home dial = brightness control (user 2026-11-10: "when turning the
 * knob on the modem-sleep (home) screen, it should dim the display"):
 * CW brightens, CCW dims, 5 % detents, floored at the ladder's dim rung
 * (the dimmed look the family uses — never darker than the ladder's
 * own choice). A turn on home is NOT ladder activity: the idle ladder
 * dims and sleeps on schedule regardless of dial fiddling; only
 * BRIGHTENING from a dark panel counts (wake intent — the supervisor's
 * un-dim also restores the dimmed clock widget). The mirror in
 * knob_board_backlight_get() is the read truth: every backlight writer
 * (ladder rungs, seam ramps, this) goes through knob_board_backlight_set. */
#ifndef CONFIG_KNOB_DIM_DUTY
#define CONFIG_KNOB_DIM_DUTY 64
#endif
#define DIAL_DIM_MIN_PCT (((int)CONFIG_KNOB_DIM_DUTY * 100 + 127) / 255)
#define DIAL_DIM_STEP_PCT 5

static void dial_home_dim(int dir) {
    const int pct = ((int)knob_board_backlight_get() * 100 + 127) / 255;
    int next = pct + (dir >= 0 ? DIAL_DIM_STEP_PCT : -DIAL_DIM_STEP_PCT);
    if (next > 100) {
        next = 100;
    }
    if (next < DIAL_DIM_MIN_PCT) {
        next = DIAL_DIM_MIN_PCT;
    }
    if (next == pct) {
        return;
    }
    (void)moon_ui_panel_brightness_set(next);
    if (dir > 0 && pct < 100) {
        knob_power_notify_activity(); /* brightening = wake intent */
    }
    ESP_LOGI(TAG, "home dial %d%% -> %d%%", pct, next);
}

static void dial_task(void *arg) {
    (void)arg;
    int32_t last = knob_board_encoder_count();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        const int32_t c = knob_board_encoder_count();
        if (c == last) {
            continue;
        }
        ESP_LOGI(TAG, "dial %d -> %d", (int)last, (int)c); /* #31 probe line kept */
        const bool home = (ui_current_screen() == SCREEN_ID_MODEM_SLEEP);
        while (last != c) {
            const int step = (c > last) ? 1 : -1;
            last += step;
            if (home) {
                dial_home_dim(step); /* brightness, not input, not activity */
                continue;
            }
            knob_power_notify_activity();
            knob_ui_lvgl_lock();
            screens_dial_step(step);
            knob_ui_lvgl_unlock();
        }
    }
}

void knob_dial_start(void) {
    /* Tap-to-commit on every settings root (objects set by ui_init).
     * Moon screens are deliberately absent: their tap opens the hub
     * (native home-tap handler), the dial only un-dims there. A plain
     * local array: objects.* are runtime globals, no static initializer. */
    lv_obj_t *const roots[] = {objects.settings_hub,        objects.settings_alarm,         objects.settings_pomodoro,
                               objects.settings_alarm_time, objects.settings_light_sleep,   objects.settings_deep_sleep,
                               objects.settings_wifi,       objects.settings_wifi_password, objects.settings_time,
                               objects.settings_tz,         objects.settings_alarm_sound};
    int armed = 0;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (roots[i]) {
            armed++;
            /* The settings creators deliberately clear the root's
             * CLICKABLE (only the explicit widgets take touches) — but
             * LVGL 9 hit-testing requires CLICKABLE to make a press
             * TARGET at all, so a background press would resolve to no
             * object and no CLICKED would ever reach this listener
             * (the home screens keep a clickable root, which is why
             * the home-tap gesture works). Restore it knob-side only:
             * clicks that hit no clickable child now land on the root
             * — the same target policy the moon screens use. No
             * scroll side effect: the screens are exactly panel-sized. */
            lv_obj_add_flag(roots[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(roots[i], dial_tap_commit_cb, LV_EVENT_CLICKED, NULL);
            /* Bench line: correlate the root pointers with the touch
             * press log when tap routing is in question. */
            ESP_LOGI(TAG, "root %u @%p clickable=%d", (unsigned)i, (void *)roots[i],
                     (int)lv_obj_is_clickable(roots[i]));
        }
    }
    screens_dial_init();
    ESP_LOGI(TAG, "tap-commit armed on %d roots (CLICKABLE restored)", armed);
    xTaskCreate(dial_task, "knob_dial", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "dial input up (step + tap-commit, touch untouched)");
}
