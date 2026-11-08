/**
 * @file knob_ui.c
 * @brief The knob's moon display (K7b, task #24): one physical disk +
 *        a caption row.
 *
 * Frame path = the plan's D7: ONE lv_image carrying a PSRAM RGB565
 * square through the EXISTING partial flush. The descriptor declares
 * plain LV_COLOR_FORMAT_RGB565 and the draw unit byte-swaps it into the
 * SWAPPED canvas -- the same route the K4 screens took after the
 * purple-wash fix (the bench acceptance explicitly looks for a purple
 * wash; the F1 single-window direct draw is NOT built unless seam lines
 * show on glass). The bake itself lives in knob_moon (the verbatim
 * ported clock core); everything here is glue. No burn-in machinery:
 * the SH8601 is a backlit LCD, not an AMOLED.
 *
 * The companion mirror views (KEYBOARD/STEPPER, K4) were retired
 * 2026-11-08 with the companion approach: the moon is the SOLE view and
 * nothing here runs a poll timer — the dial and touch stay initialized
 * and idle until K7c (sweep) and K7e (settings/power) give them jobs.
 */
#include "knob_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "knob_board.h"
#include "knob_moon.h"
#include "lvgl.h"

static const char *TAG = "knob_ui";

/* The disk: the full 360 px square (the round window crops the corners
 * itself), disk r 150 at cy 168 — the 12 px below centre stays free of
 * lunar limb for the caption row at the bottom edge. */
#define MOON_SIZE KNOB_LCD_H_RES
#define MOON_R 150.0f
#define MOON_CX (KNOB_LCD_H_RES / 2)
#define MOON_CY 168

static lv_obj_t *s_moon_img;
static lv_obj_t *s_caption;
static lv_image_dsc_t s_moon_dsc;
static uint16_t *s_framebuf; /* PSRAM, MOON_SIZE*MOON_SIZE RGB565 */

void knob_ui_create(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_moon_img = lv_image_create(scr);
    lv_obj_set_pos(s_moon_img, 0, 0);
    lv_obj_set_clickable(s_moon_img, false);
    /* Hidden until the first bake lands: an unset source would draw
     * nothing anyway, but the flag keeps the "is a frame up?" question
     * answerable from the object tree. */
    lv_obj_set_hidden(s_moon_img, true);

    s_caption = lv_label_create(scr);
    lv_label_set_text(s_caption, "moon unavailable");
    lv_obj_align(s_caption, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_clickable(s_caption, false);

    ESP_LOGI(TAG, "knob UI up (moon view, %d px disk r %.0f)", MOON_SIZE, (double)MOON_R);
}

bool knob_ui_show_moon(int64_t utc_epoch) {
    if (!s_framebuf) {
        s_framebuf = heap_caps_malloc((size_t)MOON_SIZE * MOON_SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_framebuf) {
            ESP_LOGE(TAG, "PSRAM frame buffer (%d KB) failed",
                     (int)((size_t)MOON_SIZE * MOON_SIZE * sizeof(uint16_t) / 1024));
            return false;
        }
        s_moon_dsc.header.cf = LV_COLOR_FORMAT_RGB565; /* LVGL swaps into the canvas */
        s_moon_dsc.header.w = MOON_SIZE;
        s_moon_dsc.header.h = MOON_SIZE;
        s_moon_dsc.header.stride = MOON_SIZE * sizeof(uint16_t);
        s_moon_dsc.data = (const uint8_t *)s_framebuf;
        s_moon_dsc.data_size = (uint32_t)((size_t)MOON_SIZE * MOON_SIZE * sizeof(uint16_t));
        lv_image_set_src(s_moon_img, &s_moon_dsc);
    }
    if (!knob_moon_render(s_framebuf, MOON_SIZE, MOON_R, MOON_CX, MOON_CY, utc_epoch)) {
        lv_label_set_text(s_caption, "moon unavailable");
        return false;
    }
    /* The caption names the baked instant, so the bench can compare the
     * disk against that day's published phase (K7d swaps this row for
     * the live HH:MM + "no time" posture). */
    time_t t = (time_t)utc_epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d UTC", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min);
    lv_label_set_text(s_caption, buf);
    lv_obj_set_hidden(s_moon_img, false);
    lv_obj_invalidate(s_moon_img);
    return true;
}
