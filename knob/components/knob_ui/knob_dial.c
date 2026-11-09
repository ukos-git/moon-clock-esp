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
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_dial.h"
#include "knob_power.h"
#include "knob_ui.h"
#include "lvgl.h"
#include "screens.h"
#include "ui.h"

static const char *TAG = "knob_dial";

/* Background tap (no clickable target): commit the focused widget.
 * LVGL-task context: the mutex is held by lv_timer_handler around us. */
static void dial_tap_commit_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "root CLICKED -> dial commit"); /* bench forensics: the
                                                       * background tap reached the root */
        screens_dial_commit();
    }
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
        while (last != c) {
            const int step = (c > last) ? 1 : -1;
            last += step;
            knob_power_notify_activity(); /* a turn is activity, always */
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
            /* #tap-forensics: the pointer correlates the indev log's
             * target= field (temporary, with the indev probe). */
            ESP_LOGI(TAG, "root %u @%p clickable=%d", (unsigned)i, (void *)roots[i],
                     (int)lv_obj_is_clickable(roots[i]));
        }
    }
    screens_dial_init();
    ESP_LOGI(TAG, "tap-commit armed on %d roots (CLICKABLE restored)", armed);
    xTaskCreate(dial_task, "knob_dial", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "dial input up (step + tap-commit, touch untouched)");
}
