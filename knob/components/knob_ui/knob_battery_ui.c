/**
 * @file knob_battery_ui.c
 * @brief Knob battery status: an iPhone-style battery icon (capsule
 *        outline + nub + proportional fill + charging bolt) at the
 *        bottom centre of the shared home screen, refreshed once a
 *        minute (the cell moves on the minute scale, not the second).
 *
 * The look follows the iOS status-bar battery (grey capsule, white
 * fill, red at low, green + bolt on the dock) — the geometry and
 * palette are in-house LVGL-9 primitives, no third-party widget or
 * image/font assets. The original foot-of-panel "NN %" label (5456799,
 * font fix 174a668) retired with this pass: the icon shows the level
 * as a fill, not as digits, so the SSID font is not needed here.
 *
 * Knob-side overlay by design: the shared home belongs to the clocks
 * (byte-identical rule), so the gauge is added to the modem_sleep
 * root here — created after everything else, so it sits on top, and
 * click-transparent so the home tap -> hub gesture still owns every
 * tap. It sits bottom-centre at the foot of the panel — the proven
 * spot of the retired label (under the alarm row, a detent above the
 * rim; the round chord there is far wider than the icon) — and clear
 * of the sweep progress row's top band (28..46). The foot offset
 * below is the single calibration constant if the bench wants it
 * nudged.
 * The charging bolt is the documented heuristic (knob_battery_charging:
 * cell read >= KNOB_BATTERY_CHARGING_MV, the USB-rail signature), and
 * the calibration is the placeholder pair until the multimeter ritual
 * lands real numbers.
 */

#include "knob_battery_ui.h"
#include "esp_log.h"
#include "knob_battery.h"
#include "lvgl.h"
#include "screens.h" /* objects.modem_sleep, the home root */
#include "ui_px.h"

static const char *TAG = "knob_battery_ui";

/* #ifndef-seeded (repo invariant): the red-fill threshold lives in the
 * knob_battery Kconfig but is consumed by the view. */
#ifndef CONFIG_KNOB_BATTERY_LOW_PCT
#define CONFIG_KNOB_BATTERY_LOW_PCT 20
#endif

/* ---- geometry (design px at 466, UI_PX-scaled to the panel) --------
 * At the knob's 360 px panel: body 40x18, nub 3x7, whole icon 44x18 —
 * about 12 % of the panel width, the "small" the iOS status bar does
 * it with. Bottom-centre keeps the icon on the panel's vertical axis
 * under the clock column; the chord there (foot band 20..38 from the
 * rim) is ~170 px wide, far wider than the 44 px icon. */
#define ICON_BODY_W UI_PX(52) /* 40 */
#define ICON_BODY_H UI_PX(24) /* 18 */
#define ICON_BORDER UI_PX(3)  /*  2 */
#define ICON_NUB_W UI_PX(5)   /*  3 */
#define ICON_NUB_H UI_PX(10)  /*  7 */
#define ICON_GAP UI_PX(2)     /*  1: body<->nub gap AND border<->fill inset */
#define ICON_INSET (ICON_BORDER + ICON_GAP)
#define ICON_FOOT UI_PX(26) /* 20: BOTTOM_MID lift off the rim (the old label's spot) */

/* ---- iOS palette (the reference look, hand-adopted) ----------------*/
#define COL_GREY 0x808080u      /* capsule outline + nub */
#define COL_GREY_DEAD 0x787878u /* unknown state outline */
#define COL_FILL 0xFFFFFFu      /* normal fill */
#define COL_CHARGING 0x37C557u  /* fill while docked */
#define COL_LOW 0xFD3B2Fu       /* fill at <= LOW_PCT */
#define COL_BOLT 0xFFFFFFu      /* bolt recolour over the green fill */

/* ---- charging bolt: 10x14 A8 alpha mask, authored here (even-odd
 * polygon [(6,0)(1.4,7.6)(4.3,7.6)(3,14)(8.6,6.2)(5.7,6.2)] rasterised
 * at 4x supersampling; stride padded to 12) --------------------------*/
#define BOLT_W 10
#define BOLT_H 14
#define BOLT_STRIDE 12
static const uint8_t s_bolt_a8[BOLT_H * BOLT_STRIDE] = {
    0, 0,  0,   0,   0,   80,  0,   0,   0,  0, 0, 0, /* y= 0 */
    0, 0,  0,   0,   16,  223, 0,   0,   0,  0, 0, 0, /* y= 1 */
    0, 0,  0,   0,   128, 223, 0,   0,   0,  0, 0, 0, /* y= 2 */
    0, 0,  0,   32,  255, 191, 0,   0,   0,  0, 0, 0, /* y= 3 */
    0, 0,  0,   191, 255, 191, 0,   0,   0,  0, 0, 0, /* y= 4 */
    0, 0,  80,  255, 255, 191, 0,   0,   0,  0, 0, 0, /* y= 5 */
    0, 16, 223, 255, 255, 239, 191, 191, 48, 0, 0, 0, /* y= 6 */
    0, 48, 128, 128, 223, 255, 255, 175, 0,  0, 0, 0, /* y= 7 */
    0, 0,  0,   0,   223, 255, 223, 16,  0,  0, 0, 0, /* y= 8 */
    0, 0,  0,   16,  255, 255, 64,  0,   0,  0, 0, 0, /* y= 9 */
    0, 0,  0,   64,  255, 128, 0,   0,   0,  0, 0, 0, /* y=10 */
    0, 0,  0,   128, 207, 0,   0,   0,   0,  0, 0, 0, /* y=11 */
    0, 0,  0,   159, 32,  0,   0,   0,   0,  0, 0, 0, /* y=12 */
    0, 0,  0,   64,  0,   0,   0,   0,   0,  0, 0, 0, /* y=13 */
};
static const lv_image_dsc_t s_bolt_dsc = {
    .header =
        {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_A8,
            .flags = 0,
            .w = BOLT_W,
            .h = BOLT_H,
            .stride = BOLT_STRIDE,
        },
    .data_size = sizeof(s_bolt_a8),
    .data = s_bolt_a8,
};

static lv_obj_t *s_root; /* whole icon (body + nub + bolt) */
static lv_obj_t *s_body; /* capsule: transparent bg, grey border */
static lv_obj_t *s_nub;  /* the cap on the right */
static lv_obj_t *s_fill; /* the proportional bar inside the capsule */
static lv_obj_t *s_bolt; /* centred in the body; hidden unless charging */

/* Plain decorative obj: never intercepts a tap, never scrolls. */
static lv_obj_t *icon_obj_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_clickable(o, false); /* the dedicated setters: flag API is
                                     * deprecated in LVGL 9.6 */
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    return o;
}

static void battery_timer_cb(lv_timer_t *t) {
    (void)t;
    if (!s_fill) {
        return;
    }
    const int pct = knob_battery_percent();
    const bool charging = knob_battery_charging();
    if (pct < 0) { /* no reading: empty capsule, dead-grey chrome */
        const lv_color_t dead = lv_color_hex(COL_GREY_DEAD);
        lv_obj_set_style_border_color(s_body, dead, 0);
        lv_obj_set_style_bg_color(s_nub, dead, 0);
        lv_obj_set_width(s_fill, 0);
        lv_obj_set_hidden(s_bolt, true);
        return;
    }
    const lv_color_t grey = lv_color_hex(COL_GREY);
    lv_obj_set_style_border_color(s_body, grey, 0);
    lv_obj_set_style_bg_color(s_nub, grey, 0);

    /* iOS priority: docked -> green, low -> red, else white. */
    uint32_t c = COL_FILL;
    if (charging) {
        c = COL_CHARGING;
    } else if (pct <= CONFIG_KNOB_BATTERY_LOW_PCT) {
        c = COL_LOW;
    }
    const int inner_w = ICON_BODY_W - 2 * ICON_INSET;
    lv_obj_set_width(s_fill, (inner_w * pct) / 100);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(c), 0);
    lv_obj_set_hidden(s_bolt, !charging);
}

void knob_battery_ui_attach(void) {
    if (!knob_battery_available()) {
        ESP_LOGI(TAG, "gauge off (KNOB_BATTERY_GAUGE=n or ADC init failed)");
        return;
    }
    if (!objects.modem_sleep) {
        return;
    }

    s_root = icon_obj_create(objects.modem_sleep);
    lv_obj_set_size(s_root, ICON_BODY_W + ICON_GAP + ICON_NUB_W, ICON_BODY_H);
    lv_obj_align(s_root, LV_ALIGN_BOTTOM_MID, 0, -ICON_FOOT);

    s_body = icon_obj_create(s_root);
    lv_obj_set_size(s_body, ICON_BODY_W, ICON_BODY_H);
    lv_obj_set_pos(s_body, 0, 0);
    lv_obj_set_style_border_width(s_body, ICON_BORDER, 0);
    lv_obj_set_style_border_color(s_body, lv_color_hex(COL_GREY), 0);
    lv_obj_set_style_radius(s_body, LV_RADIUS_CIRCLE, 0);

    s_nub = icon_obj_create(s_root);
    lv_obj_set_size(s_nub, ICON_NUB_W, ICON_NUB_H);
    lv_obj_set_style_bg_opa(s_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_nub, lv_color_hex(COL_GREY), 0);
    lv_obj_set_style_radius(s_nub, ICON_NUB_W / 2, 0);
    lv_obj_align(s_nub, LV_ALIGN_RIGHT_MID, 0, 0);

    s_fill = icon_obj_create(s_body);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_fill, lv_color_hex(COL_FILL), 0);
    lv_obj_set_style_radius(s_fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(s_fill, 0, ICON_BODY_H - 2 * ICON_INSET);
    lv_obj_align(s_fill, LV_ALIGN_LEFT_MID, ICON_INSET, 0);

    s_bolt = lv_image_create(s_root);
    lv_image_set_src(s_bolt, &s_bolt_dsc);
    lv_obj_set_style_image_recolor(s_bolt, lv_color_hex(COL_BOLT), 0);
    lv_obj_set_style_image_recolor_opa(s_bolt, LV_OPA_COVER, 0);
    lv_obj_center(s_bolt);
    lv_obj_set_hidden(s_bolt, true);

    lv_timer_create(battery_timer_cb, 60000, NULL);
    battery_timer_cb(NULL); /* the first fill (also the first ADC read) */
    /* #tap-forensics: the icon root pointer joins the dial's home-tree line. */
    ESP_LOGI(TAG, "battery icon attached @%p (placeholder calibration)", (void *)s_root);
}
