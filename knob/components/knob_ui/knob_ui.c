/**
 * @file knob_ui.c
 * @brief KEYBOARD + STEPPER mirror views for the knob (K4, task #16).
 *
 * Everything here runs in the LVGL task (zone events + the 30 ms poll
 * timer); the encoder count is read through its portMUX-guarded getter.
 * The knob never keeps authoritative state: the ring/field list is a
 * MIRROR, the clock's screen gate decides what lands.
 *
 * KEYBOARD: dial moves the lit slot (KEY_HOT carries the absolute slot),
 * a COMPANION_DWELL_MS dwell on a settled slot commits it (KEY_ADD) — the plan's
 * "nothing commits on a detent crossing": a new detent restarts the
 * dwell. Zones: SW view / DEL backspace / ENT commit password / X
 * discard-exit / ab cycle register (KEY_MODE).
 *
 * STEPPER: dial sends STEP(field, ±detents) batched per poll tick; NEXT
 * cycles the field locally; ENT sends COMMIT, X sends ABORT. The clock
 * drops frames whose field belongs to another screen — the status line
 * names the field so the user puts the clock on the right screen.
 */
#include "knob_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "companion_proto.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_link.h"
#include "lvgl.h"

static const char *TAG = "knob_ui";

#define KNOB_CX 180
#define KNOB_CY 172
#define RING_R 125
#define RING_SLOTS                                                                                                     \
    26 /* ring geometry: always 26 slots, the 123/#! sets                                                              \
        * are 25 chars so one slot stands blank (the                                                                   \
        * clock's wheel fills its sectors in the same                                                                  \
        * index order) */
#define ZONE_Y 300
#define ZONE_H 46
#define DWELL_MS                                                                                                       \
    COMPANION_DWELL_MS /* proto constant: the clock mirrors it as the wheel's                                          \
                        * dwell countdown ring (they must agree) */
#define FB_MS 400

typedef enum { VIEW_KEYBOARD, VIEW_STEPPER } knob_view_t;

static knob_view_t s_view = VIEW_KEYBOARD;
static int32_t s_last_enc;
static uint32_t s_ui_tick; /* lv_tick_get() of the last timer run */

/* keyboard state — the register mirror. s_set/s_n index the shared
 * COMPANION_WHEEL_SETS, so dial slot i means the clock's
 * s_wheel_chars[i] in the same register. The clock resets its wheel to
 * abc whenever the screen opens; the knob's s_mode is sticky, so after
 * re-opening the screen tap ab until the labels agree (a HELLO carries
 * no mode back - the knob is a mirror, not a master). */
static int s_hot;         /* 0..s_n-1 (lit slot) */
static uint32_t s_hot_ms; /* last dial settle */
static bool s_dwell_sent; /* one shot per settle */
static uint32_t s_fb_ms;  /* feedback window start (0 = none) */
static int s_mode;        /* register mirror 0..3 */
static const char *s_set = COMPANION_WHEEL_SETS[0];
static int s_n = 26; /* strlen(s_set) */
static const char *const s_mode_name[4] = {"abc", "ABC", "123", "#!"};

/* stepper state */
static int s_field_idx;
static const struct {
    uint8_t field;
    const char *name;
} s_fields[] = {
    {COMPANION_FIELD_HOUR, "HOUR"},
    {COMPANION_FIELD_MINUTE, "MIN"},
    {COMPANION_FIELD_DAY, "DAY"},
    {COMPANION_FIELD_MONTH, "MON"},
    {COMPANION_FIELD_YEAR, "YEAR"},
    {COMPANION_FIELD_ALARM_HOUR, "ALM H"},
    {COMPANION_FIELD_ALARM_MINUTE, "ALM M"},
    {COMPANION_FIELD_ALARM_DAYS, "DAYS"},
    {COMPANION_FIELD_POM_HOUR, "POM H"},
    {COMPANION_FIELD_POM_MINUTE, "POM M"},
    {COMPANION_FIELD_TZ, "TZ"},
};

/* widgets */
static lv_obj_t *s_status;
static lv_obj_t *s_kb_view;
static lv_obj_t *s_st_view;
static lv_obj_t *s_ring[RING_SLOTS];
static lv_obj_t *s_center;
static lv_obj_t *s_sub;
static lv_obj_t *s_field_label;
static lv_obj_t *s_step_label;

/* ---- styling helpers --------------------------------------------------- */

static void zone_create(lv_obj_t *parent, int x, int w, const char *text, lv_event_cb_t cb, void *ud) {
    lv_obj_t *z = lv_obj_create(parent);
    lv_obj_set_size(z, w, ZONE_H);
    lv_obj_set_pos(z, x, ZONE_Y);
    lv_obj_set_style_bg_color(z, lv_color_hex(0x1E1E24), 0);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(z, 1, 0);
    lv_obj_set_style_border_color(z, lv_color_hex(0x4A4A52), 0);
    lv_obj_set_style_radius(z, 10, 0);
    lv_obj_set_style_pad_all(z, 0, 0);
    lv_obj_set_scrollable(z, false);
    lv_obj_add_event_cb(z, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(z);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_set_clickable(l, false);
}

/* Paint the ring from the current register (slots past the set's end
 * stand blank - the 123/#! sets are 25 chars) and preview the hot char
 * in the centre. Cheap enough to run on every dial tick. */
static void apply_hot(void) {
    for (int i = 0; i < RING_SLOTS; i++) {
        const bool lit = (i == s_hot);
        char one[2] = {i < s_n ? s_set[i] : ' ', 0};
        lv_label_set_text(s_ring[i], one);
        lv_obj_set_style_text_color(s_ring[i], lit ? lv_color_hex(0x101014) : lv_color_hex(0xC9CCD3), 0);
        lv_obj_set_style_bg_color(s_ring[i], lit ? lv_color_hex(0xF2F3F5) : lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_ring[i], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_ring[i], lit ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_ring[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(s_ring[i], 8, 0);
    }
    char one[2] = {s_set[s_hot], 0};
    lv_label_set_text(s_center, one);
}

/* Adopt a register (0..3): re-skin the ring and park the highlight. */
static void kb_set_register(int m) {
    s_mode = m & 3;
    s_set = COMPANION_WHEEL_SETS[s_mode];
    s_n = (int)strlen(s_set);
    s_hot = 0;
    s_dwell_sent = true; /* no phantom commit from the repaint */
    lv_label_set_text(s_sub, s_mode_name[s_mode]);
    apply_hot();
}

/* ---- keyboard input ---------------------------------------------------- */

static void kb_dial(int32_t d) {
    s_hot = (int)(((s_hot + d) % s_n + s_n) % s_n);
    s_hot_ms = lv_tick_get();
    s_dwell_sent = false;
    s_fb_ms = 0;
    apply_hot();
    knob_link_send(COMPANION_FRAME_KEY_HOT, COMPANION_FIELD_NONE, (int8_t)s_hot);
}

static void kb_zone_cb(lv_event_t *e) {
    const uintptr_t which = (uintptr_t)lv_event_get_user_data(e);
    switch (which) {
    case 0: /* SW: jump to STEPPER, announce ourselves */
        s_view = VIEW_STEPPER;
        lv_obj_set_flag(s_kb_view, LV_OBJ_FLAG_HIDDEN, true);
        lv_obj_set_flag(s_st_view, LV_OBJ_FLAG_HIDDEN, false);
        knob_link_hello();
        break;
    case 1:
        knob_link_send(COMPANION_FRAME_KEY_BACK, COMPANION_FIELD_NONE, 0);
        break;
    case 2:
        knob_link_send(COMPANION_FRAME_KEY_COMMIT, COMPANION_FIELD_NONE, 0);
        break;
    case 3:
        knob_link_send(COMPANION_FRAME_ABORT, COMPANION_FIELD_NONE, 0);
        break;
    default: /* ab: cycle the clock's register (and our mirror of it) */
        kb_set_register(s_mode + 1);
        knob_link_send(COMPANION_FRAME_KEY_MODE, COMPANION_FIELD_NONE, (int8_t)s_mode);
        break;
    }
}

/* ---- stepper input ----------------------------------------------------- */

static void st_zone_cb(lv_event_t *e) {
    const uintptr_t which = (uintptr_t)lv_event_get_user_data(e);
    switch (which) {
    case 0: /* SW back to KEYBOARD */
        s_view = VIEW_KEYBOARD;
        lv_obj_set_flag(s_st_view, LV_OBJ_FLAG_HIDDEN, true);
        lv_obj_set_flag(s_kb_view, LV_OBJ_FLAG_HIDDEN, false);
        knob_link_hello();
        break;
    case 1: /* NEXT field (local) */
        s_field_idx = (s_field_idx + 1) % (int)(sizeof(s_fields) / sizeof(s_fields[0]));
        lv_label_set_text(s_field_label, s_fields[s_field_idx].name);
        lv_label_set_text(s_step_label, "STEP");
        break;
    case 2:
        knob_link_send(COMPANION_FRAME_COMMIT, COMPANION_FIELD_NONE, 0);
        break;
    default:
        knob_link_send(COMPANION_FRAME_ABORT, COMPANION_FIELD_NONE, 0);
        break;
    }
}

/* ---- poll timer -------------------------------------------------------- */

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    const uint32_t now = lv_tick_get();

    const int32_t enc = knob_board_encoder_count();
    const int32_t d = enc - s_last_enc;
    if (d != 0) {
        s_last_enc = enc;
        if (s_view == VIEW_KEYBOARD) {
            kb_dial(d);
        } else {
            /* Batch the detents accumulated since the last tick into ONE
             * frame (the clock clamps the result itself). */
            knob_link_send(COMPANION_FRAME_STEP, s_fields[s_field_idx].field, (int8_t)d);
            char buf[8];
            snprintf(buf, sizeof(buf), "%s%d", d > 0 ? "+" : "", (int)d);
            lv_label_set_text(s_step_label, buf);
        }
    }

    if (s_view == VIEW_KEYBOARD) {
        if (!s_dwell_sent && s_hot >= 0 && lv_tick_elaps(s_hot_ms) >= DWELL_MS) {
            s_dwell_sent = true;
            knob_link_send(COMPANION_FRAME_KEY_ADD, COMPANION_FIELD_NONE, (int8_t)s_hot);
            s_fb_ms = now;
            char buf[24];
            snprintf(buf, sizeof(buf), "%s  ·  sent '%c'", s_mode_name[s_mode], s_set[s_hot]);
            lv_label_set_text(s_sub, buf);
        } else if (s_fb_ms && lv_tick_elaps(s_fb_ms) >= FB_MS) {
            s_fb_ms = 0;
            lv_label_set_text(s_sub, s_mode_name[s_mode]);
        }
    }

    /* status line, ~1 Hz */
    if (lv_tick_elaps(s_ui_tick) >= 1000) {
        s_ui_tick = now;
        if (!knob_link_wifi_is_connected()) {
            lv_label_set_text(s_status, "wifi ...");
        } else if (!knob_link_clock_ready()) {
            lv_label_set_text(s_status, "clock?");
        } else {
            char buf[40];
            snprintf(buf, sizeof(buf), "clock %s", knob_link_clock_ip_str());
            lv_label_set_text(s_status, buf);
        }
    }
}

/* ---- build ------------------------------------------------------------- */

void knob_ui_create(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "wifi ...");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_clickable(s_status, false);

    /* ---- KEYBOARD view ---- */
    s_kb_view = lv_obj_create(scr);
    lv_obj_set_size(s_kb_view, KNOB_LCD_H_RES, KNOB_LCD_V_RES);
    lv_obj_set_pos(s_kb_view, 0, 0);
    lv_obj_set_style_bg_opa(s_kb_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_kb_view, 0, 0);
    lv_obj_set_style_pad_all(s_kb_view, 0, 0);
    lv_obj_set_scrollable(s_kb_view, false);

    for (int i = 0; i < RING_SLOTS; i++) {
        const double th = (2.0 * M_PI * i) / RING_SLOTS; /* 0 = top, CW */
        lv_obj_t *cell = lv_label_create(s_kb_view);
        lv_label_set_text(cell, " ");
        lv_obj_set_size(cell, 30, 38);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(cell, KNOB_CX + (int)(RING_R * sin(th)) - 15, KNOB_CY - (int)(RING_R * cos(th)) - 19);
        lv_obj_set_clickable(cell, false);
        s_ring[i] = cell;
    }
    apply_hot();

    s_center = lv_label_create(s_kb_view);
    lv_label_set_text(s_center, "a");
    lv_obj_set_style_text_font(s_center, &lv_font_montserrat_48, 0);
    lv_obj_align(s_center, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_clickable(s_center, false);

    s_sub = lv_label_create(s_kb_view);
    lv_label_set_text(s_sub, s_mode_name[0]);
    lv_obj_align(s_sub, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_clickable(s_sub, false);

    static const char *const kb_zones[5] = {"SW", "DEL", "ENT", "X", "ab"};
    for (int i = 0; i < 5; i++) {
        zone_create(s_kb_view, 10 + i * 68, 64, kb_zones[i], kb_zone_cb, (void *)(uintptr_t)i);
    }

    /* ---- STEPPER view ---- */
    s_st_view = lv_obj_create(scr);
    lv_obj_set_size(s_st_view, KNOB_LCD_H_RES, KNOB_LCD_V_RES);
    lv_obj_set_pos(s_st_view, 0, 0);
    lv_obj_set_style_bg_opa(s_st_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_st_view, 0, 0);
    lv_obj_set_style_pad_all(s_st_view, 0, 0);
    lv_obj_set_scrollable(s_st_view, false);

    s_field_label = lv_label_create(s_st_view);
    lv_label_set_text(s_field_label, s_fields[0].name);
    lv_obj_set_style_text_font(s_field_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_field_label, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_clickable(s_field_label, false);

    s_step_label = lv_label_create(s_st_view);
    lv_label_set_text(s_step_label, "STEP");
    lv_obj_align(s_step_label, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_clickable(s_step_label, false);

    static const char *const st_zones[4] = {"SW", "NEXT", "ENT", "X"};
    for (int i = 0; i < 4; i++) {
        zone_create(s_st_view, 14 + i * 86, 78, st_zones[i], st_zone_cb, (void *)(uintptr_t)i);
    }

    lv_obj_set_flag(s_st_view, LV_OBJ_FLAG_HIDDEN, true);

    s_last_enc = knob_board_encoder_count();
    s_ui_tick = lv_tick_get();
    lv_timer_create(ui_timer_cb, 30, NULL);
    ESP_LOGI(TAG, "knob UI up (KEYBOARD/STEPPER, dwell %d ms)", DWELL_MS);
}
