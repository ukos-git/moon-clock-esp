/**
 * @file knob_battery_ui.c
 * @brief P4 battery gauge: a small "NN %" label at the foot of the
 *        shared home screen, refreshed once a minute (the cell moves
 *        on the minute scale, not the second).
 *
 * Knob-side overlay by design: the shared home belongs to the clocks
 * (byte-identical rule), so the gauge is added to the modem_sleep
 * root here — created after everything else, so it sits on top, and
 * click-transparent so the home tap -> hub gesture still owns every
 * tap. The bottom-centre sits under the alarm row (which ends well
 * above the panel rim on the 360 panel); the calibration is the
 * placeholder pair until the multimeter ritual lands real numbers.
 */
#include <stdio.h>

#include "esp_log.h"
#include "fonts.h"
#include "knob_battery.h"
#include "knob_battery_ui.h"
#include "lvgl.h"
#include "screens.h" /* objects.modem_sleep, the home root */
#include "ui_px.h"

static const char *TAG = "knob_battery_ui";

static lv_obj_t *s_label;

static void battery_timer_cb(lv_timer_t *t) {
    (void)t;
    if (!s_label) {
        return;
    }
    const int pct = knob_battery_percent();
    char buf[12];
    if (pct < 0) {
        lv_label_set_text(s_label, "--%");
        lv_obj_set_style_text_color(s_label, lv_color_hex(0x787878), LV_PART_MAIN | LV_STATE_DEFAULT);
        return;
    }
    snprintf(buf, sizeof buf, "%d%%", pct);
    lv_label_set_text(s_label, buf);
    /* Coarse traffic-light tinting on the placeholder scale (the
     * multimeter verdict may re-grade these thresholds). */
    const uint32_t c = (pct >= 40) ? 0xffffffu : (pct >= 15 ? 0xffd060u : 0xff7070u);
    lv_obj_set_style_text_color(s_label, lv_color_hex(c), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void knob_battery_ui_attach(void) {
    if (!knob_battery_available()) {
        ESP_LOGI(TAG, "gauge off (KNOB_BATTERY_GAUGE=n or ADC init failed)");
        return;
    }
    if (!objects.modem_sleep) {
        return;
    }
    s_label = lv_label_create(objects.modem_sleep);
    lv_obj_set_style_text_font(s_label, &ui_font_montserrat_regular_32_range, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_label, "--%");
    lv_obj_set_style_text_color(s_label, lv_color_hex(0x787878), LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Foot of the panel, under the alarm row; a detent above the rim. */
    lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -UI_PX(26));
    /* Click-transparent: the home tap -> hub gesture owns the glass. */
    lv_obj_clear_flag(s_label, LV_OBJ_FLAG_CLICKABLE);
    lv_timer_create(battery_timer_cb, 60000, NULL);
    battery_timer_cb(NULL); /* the first fill (also the first ADC read) */
    ESP_LOGI(TAG, "battery gauge attached (placeholder calibration)");
}
