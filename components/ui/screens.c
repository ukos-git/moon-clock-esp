#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "clock_alarm_scheduler.h"
#include "clock_idle.h"
#include "esp_log.h"
#include "fonts.h"
#include "images.h"
#include "moon_ui.h"
#include "screens.h"
#include "settings_store.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"
#include "wifi.h"

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

/* Pure-black screen background: the moon image is black outside the disk,
 * so the corners blend into the screen (AMOLED black = off pixels).
 * The moon image must be created FIRST (LVGL 9.4 has no public z-order
 * reorder API; z-order = creation order) so the clock/arc render on top. */
static void moon_screen_prepare(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    moon_ui_create_image(obj);
}

/* Settings screens are moon-less by design: black background only, NO
 * moon image (the spin pipeline's s_images[] stays at the four moon
 * screens). */
static void settings_screen_prepare(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void event_handler_cb_modem_sleep_arc_clock_seconds_modem(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_arc_get_value(ta);
            set_var_clock_seconds(value);
        }
    }
}

static void event_handler_cb_wifi_arc_clock_seconds_wifi(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_arc_get_value(ta);
            set_var_clock_seconds(value);
        }
    }
}

/* FontAwesome 7 code points (this repo's OTFs carry the legacy
 * mapping), as UTF-8: xmark U+F00D, moon U+F186, wifi U+F1EB,
 * stopwatch U+F302 (the solid font's code point for the stopwatch
 * glyph -- 62210, probed with fontTools); alarm-clock U+F34E (the
 * regular font). */
#define GLYPH_XMARK "\xEF\x80\x8D"     /* U+F00D */
#define GLYPH_MOON "\xEF\x86\x86"      /* U+F186 */
#define GLYPH_WIFI "\xEF\x87\xAB"      /* U+F1EB */
#define GLYPH_STOPWATCH "\xEF\x8C\x82" /* U+F302 */
#define GLYPH_ALARM "\xEF\x8D\x8E"     /* U+F34E */

#define SETTINGS_GREY lv_color_hex(0xff787878) /* the moon/theme grey */
#define SETTINGS_WHITE lv_color_white()

/* Clear LV_OBJ_FLAG_CLICKABLE on a widget tree (root included) so
 * touches fall through to the screen root. The moon (lv_image) is
 * already non-clickable; the full-screen clock containers and their
 * labels are not, and would otherwise swallow every tap. */
static void unclickable_recursive(lv_obj_t *obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        unclickable_recursive(lv_obj_get_child(obj, (int32_t)i));
    }
}

/* Tapping anywhere on the home screens (airplane / transient WiFi)
 * opens the settings hub. Only fires while awake: a tap that wakes
 * the device lands on a sleep screen's root, which has no handler, so
 * the wake tap itself never opens the hub -- a deliberate second tap
 * does. */
static void event_handler_cb_home_tap(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED && !clock_is_idle()) {
        loadScreen(SCREEN_ID_SETTINGS_HUB);
        ui_tick();
    }
}

/* Entry refreshes for the stateful sub-screens (defined below): the
 * icon colours re-read the alarm scheduler, the rollers re-seed from
 * the stored values. */
static void settings_alarm_screen_refresh(void);
static void settings_pomodoro_screen_refresh(void);
static void settings_alarm_time_screen_refresh(void);
static void settings_sleep_screen_refresh(int screen);
static void settings_wifi_screen_refresh(void);
static void settings_screen_refresh_for(enum ScreensEnum target);

/* One icon of the hub's ring: a clickable 96 px FA glyph. The target
 * screen rides in the event's user_data (one handler for all four). */
static void event_handler_cb_hub_icon(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    enum ScreensEnum target = (enum ScreensEnum)(intptr_t)lv_event_get_user_data(e);
    loadScreen(target);
    ui_tick();
    settings_screen_refresh_for(target);
}

static lv_obj_t *settings_hub_icon(lv_obj_t *parent, const char *glyph, const lv_font_t *font, int32_t dx, int32_t dy,
                                   enum ScreensEnum target) {
    lv_obj_t *icon = lv_label_create(parent);
    if (font == &ui_font_fontawesome7_regular_96_icons) {
        add_style_label_icon_regular(icon);
    } else {
        add_style_label_icon_solid(icon);
    }
    lv_label_set_text(icon, glyph);
    lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon, LV_ALIGN_CENTER, dx, dy);
    lv_obj_set_style_text_color(icon, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon, event_handler_cb_hub_icon, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    return icon;
}

/* The concentric exit button: a grey ring with a black inner disc
 * (grey border) and a white xmark centred on it. The ring is the only
 * clickable part; a tap on it fires LV_EVENT_CLICKED on the returned
 * obj. Used central (150 px) on the hub and top-centre (90 px) on
 * every sub-screen. */
lv_obj_t *settings_create_exit_button(lv_obj_t *parent, int size) {
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ring, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    int inner = size * 2 / 3;
    lv_obj_t *disc = lv_obj_create(ring);
    lv_obj_set_size(disc, inner, inner);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(disc, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(disc, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(disc, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(disc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *xmark = lv_label_create(disc);
    add_style_label_icon_solid(xmark);
    lv_label_set_text(xmark, GLYPH_XMARK);
    lv_obj_set_size(xmark, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(xmark);
    lv_obj_set_style_text_color(xmark, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(xmark, LV_OBJ_FLAG_CLICKABLE);
    return ring;
}

/* The hub's central exit: reconnecting WiFi from the settings means
 * "leave settings" -- it runs the on-demand sync (transient WiFi
 * screen) and drops back to the airplane screen when it completes. */
static void event_handler_cb_hub_exit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_start_wifi_sync();
}

/* Every sub-screen's top-centre exit goes back to the hub. */
static void event_handler_cb_settings_exit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    loadScreen(SCREEN_ID_SETTINGS_HUB);
    ui_tick();
}

void create_screen_modem_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.modem_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // modem_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.modem_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 13);
        }
        {
            // arc-clock-seconds-modem
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds_modem = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_modem_sleep_arc_clock_seconds_modem, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff787878), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
    }

    /* Touch pass-through (tap = open the settings hub): the full-screen
     * clock container and its labels default to clickable and would
     * swallow every tap; clear it so touches land on the screen root.
     * The moon image is already non-clickable. */
    unclickable_recursive(objects.modem_sleep_clock);
    lv_obj_add_event_cb(obj, event_handler_cb_home_tap, LV_EVENT_CLICKED, 0);

    tick_screen_modem_sleep();
}

void tick_screen_modem_sleep() {
    tick_user_widget_clock(13);
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock_seconds_modem);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock_seconds_modem;
            lv_arc_set_value(objects.arc_clock_seconds_modem, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

/* The transient WiFi screen: shown while a WiFi + NTP sync runs (boot,
 * hourly, manual from the settings hub's exit button) and dropped back
 * to the airplane screen when the sync completes. Same structure as the
 * airplane (modem-sleep) screen -- moon, clock, seconds arc -- with no
 * indicator icon; the two screens share the one moon image, so the
 * switch between them is the atomic whole-panel flush. */
void create_screen_wifi() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.wifi = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // wifi_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.wifi_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 18);
        }
        {
            // arc-clock-seconds-wifi
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds_wifi = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_wifi_arc_clock_seconds_wifi, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff787878), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
    }

    /* Touch pass-through (tap = open the settings hub), same as the
     * airplane screen. */
    unclickable_recursive(objects.wifi_clock);
    lv_obj_add_event_cb(obj, event_handler_cb_home_tap, LV_EVENT_CLICKED, 0);

    tick_screen_wifi();
}

void tick_screen_wifi() {
    tick_user_widget_clock(18);
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock_seconds_wifi);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock_seconds_wifi;
            lv_arc_set_value(objects.arc_clock_seconds_wifi, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void create_screen_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.light_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // light_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.light_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 23);
        }
    }

    tick_screen_light_sleep();
}

void tick_screen_light_sleep() { tick_user_widget_clock(23); }

void create_screen_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.deep_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // deep_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.deep_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 27);
        }
        {
            // screen-indicator-sleep1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_sleep1 = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_deep_sleep();
}

void tick_screen_deep_sleep() { tick_user_widget_clock(27); }

/* The alarm screen dismisses on touch. The idle-wake path (touch while
 * asleep -> action_wake_screen) only fires while the device is
 * in idle mode; while awake, a tap just resets the idle timer, so a
 * dedicated input-device callback dismisses it instead
 * (action_register_alarm_touch in actions.c). */
void create_screen_alarm() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.alarm = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    {
        lv_obj_t *parent_obj = obj;
        {
            // alarm_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.alarm_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 32);
        }
        {
            // screen-indicator-alarm
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_alarm = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_regular(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_alarm();
}

void tick_screen_alarm() { tick_user_widget_clock(32); }

/* ---------------- settings screens ----------------
 *
 * The settings hub is a moon-less screen (settings_screen_prepare):
 * black background, a central concentric exit button, and a ring of
 * sub-screen icons around it. The sub-screens are placeholders for
 * now (black, moon-less, with the shared top-centre exit button back
 * to the hub); they get their own widgets (alarm toggles, rollers,
 * the weekday pie, the AP scan list) in the later settings steps. */

void create_screen_settings_hub() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_hub = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    settings_screen_prepare(obj);
    /* The root is not clickable: only the explicit ring icons and the
     * central exit button take touches. */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    /* Central concentric exit (150 px ring). */
    objects.settings_hub_exit = settings_create_exit_button(obj, 150);
    lv_obj_center(objects.settings_hub_exit);
    lv_obj_add_event_cb(objects.settings_hub_exit, event_handler_cb_hub_exit, LV_EVENT_CLICKED, 0);

    /* Icon ring at radius ~165 (45-degree diagonals): alarm settings
     * (top-right), light sleep (bottom-right), deep sleep (bottom-left),
     * WiFi settings (top-left). Moon weight distinguishes the two
     * sleep screens (regular / solid). */
    const int32_t R = 117; /* ~165 px radius at 45 deg */
    settings_hub_icon(obj, GLYPH_ALARM, &ui_font_fontawesome7_regular_96_icons, R, -R, SCREEN_ID_SETTINGS_ALARM);
    settings_hub_icon(obj, GLYPH_MOON, &ui_font_fontawesome7_regular_96_icons, R, R, SCREEN_ID_SETTINGS_LIGHT_SLEEP);
    settings_hub_icon(obj, GLYPH_MOON, &ui_font_fontawesome7_solid_96_icons, -R, R, SCREEN_ID_SETTINGS_DEEP_SLEEP);
    settings_hub_icon(obj, GLYPH_WIFI, &ui_font_fontawesome7_solid_96_icons, -R, -R, SCREEN_ID_SETTINGS_WIFI);
}

void tick_screen_settings_hub() {}

/* One sub-screen shell: black root (non-clickable) + the shared
 * top-centre exit button (90 px) back to the hub. The sub-screens add
 * their own widgets on top in the later settings steps. */
static void settings_sub_screen_prepare(lv_obj_t *obj) {
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *exit = settings_create_exit_button(obj, 90);
    lv_obj_align(exit, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_event_cb(exit, event_handler_cb_settings_exit, LV_EVENT_CLICKED, 0);
}

/* --- alarm settings: deactivate-all / pomodoro / real alarm ---------
 *
 * The large central alarm-clock is the master toggle: white while any
 * rule is loaded (clock_alarm_rule_types() != 0), the moon grey when
 * everything is off. A tap deactivates all rules (and disarms the RTC
 * alarm via the scheduler reload). The left stopwatch (pomodoro) and
 * right alarm-clock (real alarm) are the two mutually-exclusive
 * entries: activating one replaces the other's rule, so exactly one
 * kind of rule ever exists. */

/* Re-read the scheduler and recolor the three glyphs. */
static void settings_alarm_screen_refresh(void) {
    if (!objects.settings_alarm_master) {
        return;
    }
    uint8_t types = clock_alarm_rule_types();
    lv_obj_set_style_text_color(objects.settings_alarm_master, types ? SETTINGS_WHITE : SETTINGS_GREY,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(objects.settings_alarm_pomodoro, types & 1 ? SETTINGS_WHITE : SETTINGS_GREY,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(objects.settings_alarm_clock, types & 2 ? SETTINGS_WHITE : SETTINGS_GREY,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void event_handler_cb_settings_alarm_master(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (clock_alarm_rule_types() == 0) {
        return; /* already off; re-arming goes through the two entries */
    }
    esp_err_t r = clock_alarm_deactivate_all();
    ESP_LOGW("settings_alarm", "deactivate all -> %s", esp_err_to_name(r));
    settings_alarm_screen_refresh();
}

/* Dispatch the entry refresh for a target screen (shared by the hub
 * icons and the alarm-settings entries). */
static void settings_screen_refresh_for(enum ScreensEnum target) {
    switch (target) {
    case SCREEN_ID_SETTINGS_ALARM:
        settings_alarm_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_POMODORO:
        settings_pomodoro_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_ALARM_TIME:
        settings_alarm_time_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
        settings_sleep_screen_refresh(0);
        break;
    case SCREEN_ID_SETTINGS_DEEP_SLEEP:
        settings_sleep_screen_refresh(1);
        break;
    case SCREEN_ID_SETTINGS_WIFI:
        settings_wifi_screen_refresh();
        break;
    default:
        break;
    }
}

static void event_handler_cb_settings_alarm_entry(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    enum ScreensEnum target = (enum ScreensEnum)(intptr_t)lv_event_get_user_data(e);
    loadScreen(target);
    ui_tick();
    settings_screen_refresh_for(target);
}

void create_screen_settings_alarm() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm = obj;
    settings_sub_screen_prepare(obj);

    /* Master deactivate-all (central, 96 px regular). */
    objects.settings_alarm_master = lv_label_create(obj);
    add_style_label_icon_regular(objects.settings_alarm_master);
    lv_label_set_text(objects.settings_alarm_master, GLYPH_ALARM);
    lv_obj_set_size(objects.settings_alarm_master, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_alarm_master, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_flag(objects.settings_alarm_master, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(objects.settings_alarm_master, event_handler_cb_settings_alarm_master, LV_EVENT_CLICKED, 0);

    /* Two entries at the diagonals (stopwatch = pomodoro, alarm-clock =
     * real alarm), 96 px; colours set by the entry refresh. */
    const int32_t R = 130;
    objects.settings_alarm_pomodoro = lv_label_create(obj);
    add_style_label_icon_solid(objects.settings_alarm_pomodoro);
    lv_label_set_text(objects.settings_alarm_pomodoro, GLYPH_STOPWATCH);
    lv_obj_set_size(objects.settings_alarm_pomodoro, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_alarm_pomodoro, LV_ALIGN_CENTER, -R, R);
    lv_obj_add_flag(objects.settings_alarm_pomodoro, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(objects.settings_alarm_pomodoro, event_handler_cb_settings_alarm_entry, LV_EVENT_CLICKED,
                        (void *)(intptr_t)SCREEN_ID_SETTINGS_POMODORO);

    objects.settings_alarm_clock = lv_label_create(obj);
    add_style_label_icon_regular(objects.settings_alarm_clock);
    lv_label_set_text(objects.settings_alarm_clock, GLYPH_ALARM);
    lv_obj_set_size(objects.settings_alarm_clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_alarm_clock, LV_ALIGN_CENTER, R, R);
    lv_obj_add_flag(objects.settings_alarm_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(objects.settings_alarm_clock, event_handler_cb_settings_alarm_entry, LV_EVENT_CLICKED,
                        (void *)(intptr_t)SCREEN_ID_SETTINGS_ALARM_TIME);

    settings_alarm_screen_refresh();
}

/* The colours are refreshed on entry and after each save; the tick
 * must NOT re-set the styles every frame (style sets invalidate). */
void tick_screen_settings_alarm() {}

/* --- pomodoro: hour/minute rollers (repeat interval) -----------------
 *
 * Two lv_rollers side by side (hours 0-23, minutes 0-59) seed from
 * the stored interval rule (H = N / 60, M = N % 60; a sub-hour rule
 * shows hours 0). The 96 px preview follows the drag (VALUE_CHANGED);
 * only RELEASED persists (minutes_total = H*60+M, 0 rejected), keeping
 * the SPIFFS write + RTC reschedule off the drag path. */

static int settings_roller_value(lv_obj_t *roller) {
    int32_t sel = lv_roller_get_selected(roller);
    if (sel < 0) {
        return 0;
    }
    char buf[16];
    if (lv_roller_get_option_str(roller, (uint32_t)sel, buf, sizeof(buf)) != LV_RESULT_OK) {
        return 0;
    }
    return atoi(buf);
}

static void settings_pomodoro_preview_update(void) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", settings_roller_value(objects.settings_pomodoro_roller_h),
             settings_roller_value(objects.settings_pomodoro_roller_m));
    lv_label_set_text(objects.settings_pomodoro_preview, buf);
}

static void event_handler_cb_settings_pomodoro_value(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    settings_pomodoro_preview_update();
}

static void event_handler_cb_settings_pomodoro_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int total = 60 * settings_roller_value(objects.settings_pomodoro_roller_h) +
                settings_roller_value(objects.settings_pomodoro_roller_m);
    if (total < 1) {
        ESP_LOGW("settings_pomodoro", "interval 0 rejected (keeping the current rule)");
        settings_pomodoro_preview_update();
        return;
    }
    esp_err_t r = clock_alarm_set_pomodoro_interval(total);
    ESP_LOGW("settings_pomodoro", "interval %d min -> %s", total, esp_err_to_name(r));
}

/* Seed both rollers from the stored interval (or 0:00 when none). */
static void settings_pomodoro_screen_refresh(void) {
    if (!objects.settings_pomodoro_roller_h) {
        return;
    }
    int total = clock_alarm_get_interval_rule();
    int h = total > 0 ? total / 60 : 0;
    int m = total > 0 ? total % 60 : 0;
    lv_roller_set_selected(objects.settings_pomodoro_roller_h, h, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_pomodoro_roller_m, m, LV_ANIM_OFF);
    settings_pomodoro_preview_update();
}

static lv_obj_t *settings_create_roller(lv_obj_t *parent, const char *options, int32_t x) {
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 1);
    lv_obj_set_width(r, 160);
    lv_obj_set_height(r, 96);
    lv_obj_align(r, LV_ALIGN_CENTER, x, 90);
    lv_obj_set_style_text_font(r, &ui_font_montserrat_regular_48_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(r, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(r, SETTINGS_WHITE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    return r;
}

void create_screen_settings_pomodoro() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_pomodoro = obj;
    settings_sub_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    char hours[512];
    int pos = 0;
    for (int h = 0; h < 24; h++) {
        pos += snprintf(hours + pos, sizeof(hours) - pos, "%d\n", h);
    }
    char minutes[600];
    pos = 0;
    for (int m = 0; m < 60; m++) {
        pos += snprintf(minutes + pos, sizeof(minutes) - pos, "%d\n", m);
    }

    objects.settings_pomodoro_roller_h = settings_create_roller(obj, hours, -110);
    objects.settings_pomodoro_roller_m = settings_create_roller(obj, minutes, 110);

    objects.settings_pomodoro_preview = lv_label_create(obj);
    add_style_label_bold(objects.settings_pomodoro_preview);
    lv_label_set_text(objects.settings_pomodoro_preview, "0:00");
    lv_obj_set_size(objects.settings_pomodoro_preview, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_pomodoro_preview, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_text_color(objects.settings_pomodoro_preview, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(objects.settings_pomodoro_preview, LV_OBJ_FLAG_CLICKABLE);

    objects.settings_pomodoro_caption = lv_label_create(obj);
    lv_obj_set_style_text_font(objects.settings_pomodoro_caption, &ui_font_montserrat_regular_32_ssid,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(objects.settings_pomodoro_caption, "REPEAT INTERVAL");
    lv_obj_set_size(objects.settings_pomodoro_caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_pomodoro_caption, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_text_color(objects.settings_pomodoro_caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(objects.settings_pomodoro_caption, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(objects.settings_pomodoro_roller_h, event_handler_cb_settings_pomodoro_value,
                        LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_m, event_handler_cb_settings_pomodoro_value,
                        LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_h, event_handler_cb_settings_pomodoro_released,
                        LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_m, event_handler_cb_settings_pomodoro_released,
                        LV_EVENT_RELEASED, 0);

    settings_pomodoro_screen_refresh();
}

void tick_screen_settings_pomodoro() {}

/* --- real alarm: time rollers + weekday pie of arc wedges ------------
 *
 * The pie: 7 full-size lv_arcs (466x466, zero MAIN pad -> R = 233)
 * sharing one centre, each slice a wedge via the BG part
 * (bg_start/end_angle) and the INDICATOR (value pinned at max, so it
 * fills end-to-end). The hit test only accepts points OUTSIDE
 * R - arc_width(MAIN), so the 80 px MAIN arc_width defines the wedge's
 * clickable ring while the centre hole (Ø ~306) rejects every arc and
 * reaches the time widgets. Square caps (arc_rounded = false) make
 * adjacent wedges share seam pixels. Slice colours: moon grey
 * (unselected) / white (selected); the letter labels mirror them.
 *
 * Slice i (0 = Sunday) starts at i * 360/7 with a 270 deg rotation
 * (12 o'clock start, same convention as the seconds arcs). */

#define ALARM_TIME_PIE_ARC_WIDTH 80
#define ALARM_TIME_PIE_SPAN ((float)(360 / 7))

/* The pie's working weekday mask (the widgets persist across
 * loadScreen, so it must too; re-seeded from the rule on every entry). */
static uint8_t s_alarm_time_dow_mask = 0x7F;

static void settings_alarm_time_apply_day_colors(void) {
    uint8_t types = clock_alarm_rule_types();
    uint8_t mask = 0;
    int h = 12, m = 0;
    if (types & 2) {
        clock_alarm_get_fixed_rule(&h, &m, &mask);
    }
    for (int d = 0; d < 7; d++) {
        bool sel = mask & (1u << d);
        lv_obj_set_style_arc_color(objects.settings_alarm_time_arcs[d], sel ? SETTINGS_WHITE : SETTINGS_GREY,
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.settings_alarm_time_letters[d], sel ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/* Seed the preview + rollers from the stored fixed-time rule (or
 * 12:00, all days, when none). */
static void settings_alarm_time_screen_refresh(void) {
    if (!objects.settings_alarm_time_roller_h) {
        return;
    }
    int h = 12, m = 0;
    uint8_t mask = 0x7F;
    if (clock_alarm_get_fixed_rule(&h, &m, &mask) != ESP_OK) {
        h = 12;
        m = 0;
        mask = 0x7F;
    }
    lv_roller_set_selected(objects.settings_alarm_time_roller_h, h, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_alarm_time_roller_m, m, LV_ANIM_OFF);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    lv_label_set_text(objects.settings_alarm_time_preview, buf);

    /* The weekday state lives in the static mask (the pie widgets are
     * created once at boot); re-render each slice's colour. */
    for (int d = 0; d < 7; d++) {
        bool sel = mask & (1u << d);
        lv_obj_set_style_arc_color(objects.settings_alarm_time_arcs[d], sel ? SETTINGS_WHITE : SETTINGS_GREY,
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.settings_alarm_time_letters[d], sel ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    s_alarm_time_dow_mask = mask;
}

static void event_handler_cb_settings_alarm_time_slice(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_alarm_time_dow_mask ^= (1u << d);
    settings_alarm_time_apply_day_colors();
}

static void event_handler_cb_settings_alarm_time_value(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", settings_roller_value(objects.settings_alarm_time_roller_h),
             settings_roller_value(objects.settings_alarm_time_roller_m));
    lv_label_set_text(objects.settings_alarm_time_preview, buf);
}

static void event_handler_cb_settings_alarm_time_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    if (s_alarm_time_dow_mask == 0) {
        ESP_LOGW("settings_alarm_time", "save rejected: no weekday selected");
        return;
    }
    int h = settings_roller_value(objects.settings_alarm_time_roller_h);
    int m = settings_roller_value(objects.settings_alarm_time_roller_m);
    esp_err_t r = clock_alarm_set_clock_alarm(h, m, s_alarm_time_dow_mask);
    ESP_LOGW("settings_alarm_time", "alarm %02d:%02d mask 0x%02x -> %s", h, m, s_alarm_time_dow_mask,
             esp_err_to_name(r));
}

void create_screen_settings_alarm_time() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm_time = obj;
    settings_sub_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The 7 pie wedges, created first (behind the centre widgets). */
    static const char *weekday_letters[7] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int d = 0; d < 7; d++) {
        lv_obj_t *a = lv_arc_create(obj);
        objects.settings_alarm_time_arcs[d] = a;
        lv_obj_set_pos(a, 0, 0);
        lv_obj_set_size(a, 466, 466);
        lv_obj_set_style_pad_left(a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(a, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arc_set_rotation(a, 270);
        lv_arc_set_bg_start_angle(a, (int32_t)(d * ALARM_TIME_PIE_SPAN));
        lv_arc_set_bg_end_angle(a, (int32_t)((d + 1) * ALARM_TIME_PIE_SPAN));
        lv_arc_set_range(a, 0, 1);
        lv_arc_set_value(a, 1);
        lv_obj_set_style_arc_width(a, ALARM_TIME_PIE_ARC_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_width(a, ALARM_TIME_PIE_ARC_WIDTH, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(a, event_handler_cb_settings_alarm_time_slice, LV_EVENT_CLICKED, (void *)(intptr_t)d);

        /* Weekday letter at the mid-angle, radius ~183 (inside the
         * 80 px wedge ring). */
        lv_obj_t *letter = lv_label_create(obj);
        objects.settings_alarm_time_letters[d] = letter;
        lv_obj_set_style_text_font(letter, &ui_font_montserrat_regular_40_weekdays, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(letter, weekday_letters[d]);
        lv_obj_set_size(letter, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        /* Arc angles use 0 deg = right, 90 deg = down (screen coords);
         * the 270 deg rotation puts the slice start at 12 o'clock.
         * The letter sits at the slice's mid-angle, radius ~183. */
        float screen_deg = (float)((d + 0.5) * 360 / 7) + 270.0f;
        float ang = (screen_deg * 3.14159265f) / 180.0f;
        int32_t dx = (int32_t)(183.0f * cosf(ang));
        int32_t dy = (int32_t)(183.0f * sinf(ang));
        lv_obj_align(letter, LV_ALIGN_CENTER, dx, dy);
        lv_obj_clear_flag(letter, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Centre (unfilled by the pie): the HH:MM preview + the two
     * time rollers below it. */
    objects.settings_alarm_time_preview = lv_label_create(obj);
    add_style_label_bold(objects.settings_alarm_time_preview);
    lv_label_set_text(objects.settings_alarm_time_preview, "12:00");
    lv_obj_set_size(objects.settings_alarm_time_preview, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_alarm_time_preview, LV_ALIGN_CENTER, 0, -36);
    lv_obj_set_style_text_color(objects.settings_alarm_time_preview, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(objects.settings_alarm_time_preview, LV_OBJ_FLAG_CLICKABLE);

    char hours[512];
    int pos = 0;
    for (int h = 0; h < 24; h++) {
        pos += snprintf(hours + pos, sizeof(hours) - pos, "%d\n", h);
    }
    char minutes[600];
    pos = 0;
    for (int m = 0; m < 60; m++) {
        pos += snprintf(minutes + pos, sizeof(minutes) - pos, "%d\n", m);
    }
    objects.settings_alarm_time_roller_h = settings_create_roller(obj, hours, -110);
    lv_obj_align(objects.settings_alarm_time_roller_h, LV_ALIGN_CENTER, -110, 66);
    objects.settings_alarm_time_roller_m = settings_create_roller(obj, minutes, 110);
    lv_obj_align(objects.settings_alarm_time_roller_m, LV_ALIGN_CENTER, 110, 66);

    lv_obj_add_event_cb(objects.settings_alarm_time_roller_h, event_handler_cb_settings_alarm_time_value,
                        LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_m, event_handler_cb_settings_alarm_time_value,
                        LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_h, event_handler_cb_settings_alarm_time_released,
                        LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_m, event_handler_cb_settings_alarm_time_released,
                        LV_EVENT_RELEASED, 0);

    settings_alarm_time_screen_refresh();
}

void tick_screen_settings_alarm_time() {}

/* --- sleep settings: one parameterized builder (the two screens are
 * near-identical; the plan's fallback 5) ------------------------------
 *
 * SETTINGS_LIGHT_SLEEP: the idle-timeout roller (10-300 s, the
 * airplane -> light-sleep transition). SETTINGS_DEEP_SLEEP: the
 * deep-sleep countdown roller (light -> deep). Both screens carry
 * both animation toggles (the spec): a moon glyph + upper-cased
 * caption, coloured white (on) / moon grey (off) -- no switch widget
 * (LV_USE_SWITCH is off). A tap on the toggle row flips the flag,
 * saves, and recolors. The countdown takes effect at the next dusk
 * arming; the idle timeout is applied live via
 * clock_idle_set_timeout_ms().
 *
 * The widgets are created once (loadScreen never destroys them), so
 * the pointers live here; [0] = light-sleep screen, [1] = deep. */
static lv_obj_t *s_sleep_roller[2];
static lv_obj_t *s_anim_glyph[2][2]; /* [screen][0=boot, 1=dusk] */
static lv_obj_t *s_anim_cap[2][2];

static void settings_anim_apply_colors(int screen) {
    bool boot = settings_boot_anim_enable();
    bool dusk = settings_dusk_anim_enable();
    lv_color_t c0 = boot ? SETTINGS_WHITE : SETTINGS_GREY;
    lv_color_t c1 = dusk ? SETTINGS_WHITE : SETTINGS_GREY;
    lv_obj_set_style_text_color(s_anim_glyph[screen][0], c0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_anim_cap[screen][0], c0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_anim_glyph[screen][1], c1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_anim_cap[screen][1], c1, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void settings_sleep_screen_refresh(int screen) {
    if (!s_sleep_roller[screen]) {
        return;
    }
    int v = screen ? (int)settings_deep_sleep_countdown_sec() : (int)settings_idle_timeout_sec();
    lv_roller_set_selected(s_sleep_roller[screen], v - 10, LV_ANIM_OFF);
    settings_anim_apply_colors(screen);
}

/* user_data = screen (0/1) on the roller; screen*2+toggle on a row. */
static void event_handler_cb_settings_sleep_roller_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int screen = (int)(intptr_t)lv_event_get_user_data(e);
    int v = settings_roller_value(s_sleep_roller[screen]);
    if (screen) {
        settings_set_deep_sleep_countdown_sec((uint32_t)v);
    } else {
        settings_set_idle_timeout_sec((uint32_t)v);
        clock_idle_set_timeout_ms((uint64_t)v * 1000ULL);
    }
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_sleep", "screen %d value %d s -> %s", screen, v, esp_err_to_name(r));
}

static void event_handler_cb_settings_anim_toggle(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int sel = (int)(intptr_t)lv_event_get_user_data(e);
    int screen = sel / 2;
    int toggle = sel % 2;
    if (toggle == 0) {
        settings_set_boot_anim_enable(!settings_boot_anim_enable());
    } else {
        settings_set_dusk_anim_enable(!settings_dusk_anim_enable());
    }
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_anim", "toggle %d on screen %d -> %s", toggle, screen, esp_err_to_name(r));
    settings_anim_apply_colors(screen);
}

/* One 96 px moon-glyph + 32 px caption toggle row (clickable
 * container; the labels themselves are not clickable). */
static void settings_anim_toggle(lv_obj_t *parent, int screen, int toggle, int32_t x) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 200, 135);
    lv_obj_align(row, LV_ALIGN_CENTER, x, 118);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_handler_cb_settings_anim_toggle, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(screen * 2 + toggle));

    const bool solid = (toggle != 0);
    lv_obj_t *glyph = lv_label_create(row);
    if (solid) {
        add_style_label_icon_solid(glyph);
    } else {
        add_style_label_icon_regular(glyph);
    }
    lv_label_set_text(glyph, GLYPH_MOON);
    lv_obj_set_size(glyph, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(glyph, LV_ALIGN_CENTER, 0, -18);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *cap = lv_label_create(row);
    lv_obj_set_style_text_font(cap, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(cap, toggle == 0 ? "BOOT ANIM" : "DUSK ANIM");
    lv_obj_set_size(cap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, +44);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    s_anim_glyph[screen][toggle] = glyph;
    s_anim_cap[screen][toggle] = cap;
}

static void settings_create_sleep_screen(lv_obj_t *obj, int screen) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, screen ? "SLEEP COUNTDOWN" : "IDLE TIMEOUT");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -90);
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    char options[1200];
    int pos = 0;
    for (int v = 10; v <= 300; v++) {
        pos += snprintf(options + pos, sizeof(options) - pos, "%d\n", v);
    }
    s_sleep_roller[screen] = settings_create_roller(obj, options, 0);
    lv_obj_align(s_sleep_roller[screen], LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_sleep_roller[screen], event_handler_cb_settings_sleep_roller_released, LV_EVENT_RELEASED,
                        (void *)(intptr_t)screen);

    settings_anim_toggle(obj, screen, 0, -115);
    settings_anim_toggle(obj, screen, 1, +115);

    settings_sleep_screen_refresh(screen);
}

void create_screen_settings_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_light_sleep = obj;
    settings_sub_screen_prepare(obj);
    settings_create_sleep_screen(obj, 0);
}

void tick_screen_settings_light_sleep() {}

void create_screen_settings_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_deep_sleep = obj;
    settings_sub_screen_prepare(obj);
    settings_create_sleep_screen(obj, 1);
}

void tick_screen_settings_deep_sleep() {}

/* --- WiFi settings: the AP scan list ---------------------------------
 *
 * A background scan (wifi_scan_start) runs while the screen is open;
 * the rows below render the results when the scan finishes (the 10 ms
 * tick polls wifi_scan_in_progress() and renders once). Selection is
 * log-only for now -- the circular-keyboard password entry + saving
 * the credentials is the approved follow-up, so the screen carries a
 * note about it. */
#define WIFI_SETTINGS_ROWS 12

static lv_obj_t *s_wifi_rows[WIFI_SETTINGS_ROWS];
static bool s_wifi_scan_rendered = true;
static int s_wifi_selected = -1;

static void settings_wifi_render_rows(void) {
    wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
    if (n == 0) {
        for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
            lv_label_set_text(s_wifi_rows[i], i == 0 ? "NO APs FOUND" : "");
        }
        return;
    }
    for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
        if (i >= n) {
            lv_label_set_text(s_wifi_rows[i], "");
            continue;
        }
        char buf[40];
        int pos = 0;
        for (size_t c = 0; c < sizeof(aps[i].ssid) - 1 && pos < 20; c++) {
            char ch = aps[i].ssid[c];
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - 'a' + 'A');
            }
            buf[pos++] = ch;
        }
        buf[pos++] = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %ddBm", (int)aps[i].rssi);
        lv_label_set_text(s_wifi_rows[i], buf);
        lv_obj_set_style_text_color(s_wifi_rows[i], i == s_wifi_selected ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/* user_data = the row index. */
static void event_handler_cb_wifi_row(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    s_wifi_selected = (int)(intptr_t)lv_event_get_user_data(e);
    settings_wifi_render_rows();
    /* Selection is log-only until the follow-up (circular-keyboard
     * password entry + credentials save). */
    ESP_LOGW("settings_wifi", "AP row %d selected (password entry: follow-up)", s_wifi_selected);
}

static void settings_wifi_screen_refresh(void) {
    s_wifi_selected = -1;
    s_wifi_scan_rendered = false;
    wifi_scan_start();
}

void create_screen_settings_wifi() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_wifi = obj;
    settings_sub_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(obj);
    add_style_label_icon_solid(header);
    lv_label_set_text(header, GLYPH_WIFI);
    lv_obj_set_size(header, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(header, LV_ALIGN_CENTER, 0, -150);
    lv_obj_set_style_text_color(header, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *note = lv_label_create(obj);
    lv_obj_set_style_text_font(note, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(note, "PASSWORD ENTRY: FOLLOW-UP");
    lv_obj_set_size(note, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, -84);
    lv_obj_set_style_text_color(note, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(note, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
        lv_obj_t *row = lv_label_create(obj);
        s_wifi_rows[i] = row;
        lv_obj_set_style_text_font(row, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(row, "");
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, -40 + i * 30);
        lv_obj_set_style_text_color(row, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, event_handler_cb_wifi_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void tick_screen_settings_wifi() {
    if (s_wifi_scan_rendered || wifi_scan_in_progress()) {
        return;
    }
    settings_wifi_render_rows();
    s_wifi_scan_rendered = true;
}

void create_user_widget_clock(lv_obj_t *parent_obj, int startWidgetIndex) {
    (void)startWidgetIndex;
    lv_obj_t *obj = parent_obj;
    {
        lv_obj_t *parent_obj = obj;
        {
            // clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            ((lv_obj_t **)&objects)[startWidgetIndex + 0] = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_END, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // label-clock-time
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 1] = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_label_bold(obj);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // label-clock-alarm
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 2] = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_label_regular(obj);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
}

void tick_user_widget_clock(int startWidgetIndex) {
    (void)startWidgetIndex;
    {
        const char *new_val = get_var_clock_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 1]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 1];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 1], new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_alarm_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 2]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 2];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 2], new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void change_color_theme(uint32_t theme_index) {
    active_theme_index = theme_index;

    lv_style_set_arc_color(get_style_moon_arc_INDICATOR_DEFAULT(), lv_color_hex(theme_colors[theme_index][0]));

    lv_style_set_bg_color(get_style_moon_arc_KNOB_DEFAULT(), lv_color_hex(theme_colors[theme_index][0]));

    lv_obj_invalidate(objects.modem_sleep);
    lv_obj_invalidate(objects.wifi);
    lv_obj_invalidate(objects.light_sleep);
    lv_obj_invalidate(objects.deep_sleep);
    lv_obj_invalidate(objects.alarm);
}

uint32_t theme_colors[1][1] = {
    {0xff787878},
};

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    /* order must match enum ScreensEnum - 1 */
    tick_screen_modem_sleep,
    tick_screen_wifi,
    tick_screen_light_sleep,
    tick_screen_deep_sleep,
    tick_screen_alarm,
    tick_screen_settings_hub,
    tick_screen_settings_alarm,
    tick_screen_settings_pomodoro,
    tick_screen_settings_alarm_time,
    tick_screen_settings_light_sleep,
    tick_screen_settings_deep_sleep,
    tick_screen_settings_wifi,
};
void tick_screen(int screen_index) { tick_screen_funcs[screen_index](); }
void tick_screen_by_id(enum ScreensEnum screenId) { tick_screen_funcs[screenId - 1](); }

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                              true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    create_screen_modem_sleep();
    create_screen_wifi();
    create_screen_light_sleep();
    create_screen_deep_sleep();
    create_screen_alarm();
    create_screen_settings_hub();
    create_screen_settings_alarm();
    create_screen_settings_pomodoro();
    create_screen_settings_alarm_time();
    create_screen_settings_light_sleep();
    create_screen_settings_deep_sleep();
    create_screen_settings_wifi();
}
