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
 * stopwatch U+F2F2 (the solid font's stopwatch glyph -- 62194, probed
 * with fontTools; U+F302 in these OTFs is "images", NOT the stopwatch);
 * alarm-clock U+F34E (the regular font). */
#define GLYPH_XMARK "\xEF\x80\x8D"     /* U+F00D */
#define GLYPH_MOON "\xEF\x86\x86"      /* U+F186 */
#define GLYPH_WIFI "\xEF\x87\xAB"      /* U+F1EB */
#define GLYPH_STOPWATCH "\xEF\x8B\xB2" /* U+F2F2 (was \xEF\xA7\xB2 = U+F9F2, absent -> a box) */
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

/* The exit button: a clean FA xmark icon in a thin grey circle outline
 * (the old concentric grey-donut + nested black disc read as "ugly"; the
 * flash budget leaves no room for a new glyph, so the F00D xmark is kept
 * and the chrome reduced to one thin ring). The whole circle is the tap
 * target; a tap on it fires LV_EVENT_CLICKED on the returned obj. Used
 * central (150 px) on the hub and bottom-centre (90 px) on every
 * sub-screen -- the middle of the screen is occupied by the settings'
 * working widgets, so the exit lives at the bottom edge and the
 * sub-screens' layouts keep the band around it clear. */
lv_obj_t *settings_create_exit_button(lv_obj_t *parent, int size) {
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Black fill (blends into the screen) + a thin grey outline: the
     * ring is the only visible chrome and the tap target at once. */
    lv_obj_set_style_bg_color(ring, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ring, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *xmark = lv_label_create(ring);
    add_style_label_icon_solid(xmark);
    lv_label_set_text(xmark, GLYPH_XMARK);
    lv_obj_set_size(xmark, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(xmark);
    lv_obj_set_style_text_color(xmark, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(xmark, LV_OBJ_FLAG_CLICKABLE);
    return ring;
}

/* The hub's central exit: leaving settings goes to the WiFi screen
 * directly -- the AP-scan settings screen, the one headed by the wifi
 * icon (the airplane / no-wifi screen has no icon, so the two are
 * differentiable at a glance). No background sync is kicked off; the
 * user lands on the WiFi settings screen and can act from there. */
static void event_handler_cb_hub_exit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    loadScreen(SCREEN_ID_SETTINGS_WIFI);
    ui_tick();
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI);
}

/* A sub-screen's bottom-centre exit returns to the CALLING level, not
 * always the hub: the two screens reached FROM the alarm screen (the
 * pomodoro interval + the real-alarm time) go back to the alarm screen,
 * the rest (reached from the hub) go back to the hub. The target rides
 * in the event's user_data (one handler for every sub-screen). */
static void event_handler_cb_settings_exit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    enum ScreensEnum target = (enum ScreensEnum)(intptr_t)lv_event_get_user_data(e);
    loadScreen(target);
    ui_tick();
    settings_screen_refresh_for(target);
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
 * bottom-centre exit button (90 px) back to the CALLING level (the
 * hub, or the alarm screen for its two sub-screens). The sub-screens
 * add their own widgets on top; the band around the exit (x 188..278,
 * y 342..432) stays free of widgets. */
static void settings_sub_screen_prepare(lv_obj_t *obj, enum ScreensEnum exit_target) {
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *exit = settings_create_exit_button(obj, 90);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_add_event_cb(exit, event_handler_cb_settings_exit, LV_EVENT_CLICKED, (void *)(intptr_t)exit_target);
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
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);

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
 * Two shared clock-style rollers side by side (hours 0-23, minutes
 * 0-59) seed from the stored interval rule (H = N / 60, M = N % 60; a
 * sub-hour rule shows hours 0). The rollers ARE the display (the old
 * separate "H:MM" preview label is gone); only RELEASED persists
 * (minutes_total = H*60+M, 0 rejected), keeping the SPIFFS write +
 * RTC reschedule off the drag path. */

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

static void event_handler_cb_settings_pomodoro_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int total = 60 * settings_roller_value(objects.settings_pomodoro_roller_h) +
                settings_roller_value(objects.settings_pomodoro_roller_m);
    if (total < 1) {
        ESP_LOGW("settings_pomodoro", "interval 0 rejected (keeping the current rule)");
        return;
    }
    esp_err_t r = clock_alarm_set_pomodoro_interval(total);
    ESP_LOGW("settings_pomodoro", "interval %d min -> %s", total, esp_err_to_name(r));
}

/* Seed both rollers from the stored interval (0:00 when none). */
static void settings_pomodoro_screen_refresh(void) {
    if (!objects.settings_pomodoro_roller_h) {
        return;
    }
    int total = clock_alarm_get_interval_rule();
    int h = total > 0 ? total / 60 : 0;
    int m = total > 0 ? total % 60 : 0;
    lv_roller_set_selected(objects.settings_pomodoro_roller_h, h, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_pomodoro_roller_m, m, LV_ANIM_OFF);
}

/* The shared settings roller, styled like the clock's value display:
 * the 96 px bold numbers font (the clock label font), moon grey for
 * the unselected rows and white for the selected one, no background
 * box (the default theme draws a white card on LV_PART_MAIN and a
 * blue selected row -- both overridden here). One row is visible, so
 * each field is displayed exactly once -- the roller IS the value
 * display (the old separate HH:MM preview labels are gone). LVGL 9.6's
 * selected-row part is LV_PART_SELECTED (the old LV_PART_INDICATOR
 * styling never applied). width sizes to the value: 150 for 2-digit
 * fields, 210 for the 3-digit sleep values. */
static lv_obj_t *settings_create_roller(lv_obj_t *parent, const char *options, int32_t x, int32_t width) {
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 1);
    lv_obj_set_width(r, width);
    lv_obj_set_height(r, 110);
    lv_obj_align(r, LV_ALIGN_CENTER, x, 90);
    lv_obj_set_style_text_font(r, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(r, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(r, SETTINGS_WHITE, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    return r;
}

void create_screen_settings_pomodoro() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_pomodoro = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_ALARM);
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

    /* The two rollers read like the clock's "HH:MM": a `:` sits between
     * them (the bold 96 numbers font carries the colon) and they are
     * tightened from the old +-110/150 spread -- the old distance was too
     * wide for the clock-like look. Lifted from the shared (x, 90) so the
     * 110 px tall rollers clear the bottom-centre exit button (bottom
     * edge y 358 vs 387). */
    objects.settings_pomodoro_roller_h = settings_create_roller(obj, hours, -88, 130);
    lv_obj_align(objects.settings_pomodoro_roller_h, LV_ALIGN_CENTER, -88, 70);
    objects.settings_pomodoro_roller_m = settings_create_roller(obj, minutes, 88, 130);
    lv_obj_align(objects.settings_pomodoro_roller_m, LV_ALIGN_CENTER, 88, 70);

    lv_obj_t *colon = lv_label_create(obj);
    lv_obj_set_style_text_font(colon, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(colon, ":");
    lv_obj_set_size(colon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_text_color(colon, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_CLICKABLE);

    objects.settings_pomodoro_caption = lv_label_create(obj);
    lv_obj_set_style_text_font(objects.settings_pomodoro_caption, &ui_font_montserrat_regular_32_ssid,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(objects.settings_pomodoro_caption, "REPEAT INTERVAL");
    lv_obj_set_size(objects.settings_pomodoro_caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_pomodoro_caption, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_text_color(objects.settings_pomodoro_caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(objects.settings_pomodoro_caption, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(objects.settings_pomodoro_roller_h, event_handler_cb_settings_pomodoro_released,
                        LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_m, event_handler_cb_settings_pomodoro_released,
                        LV_EVENT_RELEASED, 0);

    settings_pomodoro_screen_refresh();
}

void tick_screen_settings_pomodoro() {}

/* --- real alarm: time rollers + two-ended day-range arc --------------
 *
 * The day arc: the upper half-circle that cuts out the bottom-centre
 * exit button (the old full pie's ring ended up behind it, and its
 * 51 deg wedges were hard to press and hard to tell apart). The grey
 * track (LV_PART_MAIN) spans the whole arc, Monday (left) to Sunday
 * (right); the white INDICATOR spans the two draggable day handles
 * (the range, inclusive). The vendored arc is single-knob and two
 * overlapping knob arcs could not both take a press, so the range
 * endpoints are two plain objects on the ring (the arc itself is
 * non-clickable): a press snaps the handle to the nearest day under
 * the touch point and follows the drag. The day letters sit just
 * OUTSIDE the ring, so every day is identifiable even when a handle
 * covers its slot.
 *
 * Geometry (screen coords, 0 deg = right, 90 deg = down): day d
 * (0 = Mon) sits at 181 + 30d degrees -- the +1 deg offset keeps every
 * stored arc angle off the 360/0 wrap edge (an indicator end angle of
 * exactly 360 stores as 360 and the SW arc mask clamps it to 359,
 * drawing a 1 deg gap at the right edge of the band). The arc centre
 * is (233, 240), R = 100 (zero pad, the 200 px object at (133, 140));
 * the ring is 46 px wide (r 54..100), the handles sit at r 77 (the
 * ring midline) and the letters at r 140 (just outside the ring). The
 * time rollers sit up top (y ~60) -- the arc's centre hole no longer
 * hosts them, because the horizontal day positions (y ~240) would
 * collide with centred rollers.
 *
 * State: s_alarm_range_{start,end} (day indices, Mon..Sun order) -- the
 * working range (the widgets persist across loadScreen, so it must
 * too; re-seeded from the rule on every entry). The range maps to the
 * scheduler's weekday mask (bit 0 = Sunday .. 6 = Saturday); the
 * Mon-Fri default is 0x7E. A stored non-contiguous mask is displayed
 * rounded to its outermost days (a two-ended range is always
 * contiguous). Minutes are multiples of 5 -- 10:03 is not a wake-up
 * time. */

#define ALARM_TIME_ARC_CX 233
#define ALARM_TIME_ARC_CY 240
#define ALARM_TIME_ARC_R 100
#define ALARM_TIME_ARC_WIDTH 46
#define ALARM_TIME_HANDLE_R 77  /* the day handles' radius (the ring midline) */
#define ALARM_TIME_LETTER_R 140 /* the day letters' radius (just outside the ring) */

#define ALARM_DAY_ANG(d) (181 + 30 * (d)) /* 0 = Monday .. 6 = Sunday */

/* The range's working endpoints (day indices, 0 = Monday .. 6 =
 * Sunday; re-seeded from the stored rule on every entry). */
static int s_alarm_range_start = 0;
static int s_alarm_range_end = 4;

static bool alarm_range_contains(int s, int e, int d) {
    if (e >= s) {
        return d >= s && d <= e;
    }
    return d >= s || d <= e; /* the range wraps past Sunday */
}

/* Range -> scheduler weekday mask (bit 0 = Sunday .. bit 6 =
 * Saturday), walking the range clockwise, endpoints inclusive. */
static uint8_t alarm_range_to_mask(int s, int e) {
    uint8_t m = 0;
    for (int d = s;; d = (d + 1) % 7) {
        m |= (uint8_t)(1u << ((d + 1) % 7));
        if (d == e) {
            break;
        }
    }
    return m;
}

/* Band indicator + letter colours + handle positions from the current
 * range. The arc's stored angles: start = 181+30s, end = (181+30e)
 * % 360; the wrap case (e < s) stores a start > end pair, which the
 * SW arc mask draws as the arc through 360/0. A single-day range
 * (s == e) draws a 6 deg sliver at the day -- an arc cannot draw a
 * zero-length span. */
static void settings_alarm_time_apply_range(void) {
    int s = s_alarm_range_start, e = s_alarm_range_end;
    if (s == e) {
        int a = ALARM_DAY_ANG(s) % 360;
        lv_arc_set_angles(objects.settings_alarm_time_band, (lv_value_precise_t)a, (lv_value_precise_t)(a + 6));
    } else {
        lv_arc_set_angles(objects.settings_alarm_time_band, (lv_value_precise_t)ALARM_DAY_ANG(s),
                          (lv_value_precise_t)(ALARM_DAY_ANG(e) % 360));
    }
    for (int d = 0; d < 7; d++) {
        lv_obj_set_style_text_color(objects.settings_alarm_time_letters[d],
                                    alarm_range_contains(s, e, d) ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_t *handles[2] = {objects.settings_alarm_time_handle_start, objects.settings_alarm_time_handle_end};
    int ends[2] = {s, e};
    for (int i = 0; i < 2; i++) {
        float ang = (float)ALARM_DAY_ANG(ends[i]) * 3.14159265f / 180.0f;
        lv_obj_align(handles[i], LV_ALIGN_CENTER, (int32_t)(ALARM_TIME_HANDLE_R * cosf(ang)),
                     (int32_t)(ALARM_TIME_HANDLE_R * sinf(ang)));
    }
}

/* Map the indev point (display coords) to the arc's local frame and
 * snap to the nearest day. The screen root may carry the orientation
 * roll -- inverse it first (a no-op at rest). */
static bool alarm_time_point_to_day(int *out_day) {
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_obj_transform_point(objects.settings_alarm_time, &p, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE);
    int32_t dx = p.x - ALARM_TIME_ARC_CX;
    int32_t dy = p.y - ALARM_TIME_ARC_CY;
    if (dx == 0 && dy == 0) {
        return false;
    }
    /* atan2f (not lv_atan2, which reports 90 deg minus the screen
     * angle): radians, 0 = right, 90 = down -- the day-angle convention. */
    float theta = atan2f((float)dy, (float)dx) * 180.0f / 3.14159265f;
    while (theta < 0.0f) {
        theta += 360.0f;
    }
    while (theta >= 360.0f) {
        theta -= 360.0f;
    }
    int best = 0;
    float best_diff = 1e9f;
    for (int d = 0; d < 7; d++) {
        float diff = theta - (float)(ALARM_DAY_ANG(d) % 360);
        while (diff > 180.0f) {
            diff -= 360.0f;
        }
        while (diff < -180.0f) {
            diff += 360.0f;
        }
        float ad = diff < 0.0f ? -diff : diff;
        if (ad < best_diff) {
            best_diff = ad;
            best = d;
        }
    }
    *out_day = best;
    return true;
}

/* Drag: the pressed handle is the endpoint being moved (PRESS_LOCK
 * keeps it under the finger while it leaves the handle). PRESSED +
 * PRESSING snap the endpoint to the nearest day under the touch
 * point (a press on the handle's own day is a no-op). */
static void event_handler_cb_settings_alarm_time_handle(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int day = 0;
    if (!alarm_time_point_to_day(&day)) {
        return;
    }
    if (idx == 0) {
        if (day != s_alarm_range_start) {
            s_alarm_range_start = day;
            settings_alarm_time_apply_range();
        }
    } else if (day != s_alarm_range_end) {
        s_alarm_range_end = day;
        settings_alarm_time_apply_range();
    }
}

/* Persist time + day range (only on RELEASED: the SPIFFS write + the
 * RTC reschedule stay off the drag path). The range always covers at
 * least one day, so no empty-mask rejection is needed. */
static void event_handler_cb_settings_alarm_time_save(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int h = settings_roller_value(objects.settings_alarm_time_roller_h);
    int m = settings_roller_value(objects.settings_alarm_time_roller_m);
    uint8_t mask = alarm_range_to_mask(s_alarm_range_start, s_alarm_range_end);
    esp_err_t r = clock_alarm_set_clock_alarm(h, m, mask);
    ESP_LOGW("settings_alarm_time", "alarm %02d:%02d mask 0x%02x (days %d..%d) -> %s", h, m, mask, s_alarm_range_start,
             s_alarm_range_end, esp_err_to_name(r));
}

/* Seed the rollers + range from the stored fixed-time rule (or 12:00,
 * Mon-Fri, when none). A non-contiguous mask is displayed rounded to
 * its outermost days (a two-ended range is always contiguous). */
static void settings_alarm_time_screen_refresh(void) {
    if (!objects.settings_alarm_time_band) {
        return;
    }
    int h = 12, m = 0;
    uint8_t mask = 0x7E;
    if (clock_alarm_get_fixed_rule(&h, &m, &mask) != ESP_OK) {
        h = 12;
        m = 0;
        mask = 0x7E;
    }
    lv_roller_set_selected(objects.settings_alarm_time_roller_h, h, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_alarm_time_roller_m, m / 5, LV_ANIM_OFF); /* x5 steps */
    int s = -1, e = -1;
    for (int d = 0; d < 7; d++) {
        if (mask & (1u << ((d + 1) % 7))) {
            if (s < 0) {
                s = d;
            }
            e = d;
        }
    }
    if (s < 0) {
        s = 0;
        e = 4;
    }
    s_alarm_range_start = s;
    s_alarm_range_end = e;
    settings_alarm_time_apply_range();
}

void create_screen_settings_alarm_time() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm_time = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_ALARM);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The day-range band: the upper half-circle (Monday left, Sunday
     * right), its ring clearing the bottom-centre exit button. */
    lv_obj_t *band = lv_arc_create(obj);
    objects.settings_alarm_time_band = band;
    lv_obj_set_pos(band, (466 - ALARM_TIME_ARC_R * 2) / 2, ALARM_TIME_ARC_CY - ALARM_TIME_ARC_R);
    lv_obj_set_size(band, ALARM_TIME_ARC_R * 2, ALARM_TIME_ARC_R * 2);
    lv_obj_set_style_pad_left(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(band, 181, 1); /* the grey full track (the 180 deg wrap) */
    lv_arc_set_angles(band, 181, 181);  /* the indicator: re-seeded on entry */
    lv_obj_set_style_arc_width(band, ALARM_TIME_ARC_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(band, ALARM_TIME_ARC_WIDTH, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(band, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(band, SETTINGS_WHITE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(band, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(band, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(band, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(band, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    /* The theme draws the KNOB as a blue-filled rect at the value
     * angle -- kill it (the range endpoints are the two custom
     * handles below). */
    lv_obj_set_style_bg_opa(band, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    /* The arc must not swallow the ring presses: the handles (its
     * children) catch them, everything else falls through. */
    lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);

    /* The day letters, just outside the ring (children of the band so
     * they align to the arc centre), created before the handles so the
     * handles render on top. */
    static const char *weekday_letters[7] = {"M", "T", "W", "T", "F", "S", "S"};
    for (int d = 0; d < 7; d++) {
        lv_obj_t *letter = lv_label_create(band);
        objects.settings_alarm_time_letters[d] = letter;
        lv_obj_set_style_text_font(letter, &ui_font_montserrat_regular_40_weekdays, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(letter, weekday_letters[d]);
        lv_obj_set_size(letter, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        float ang = (float)ALARM_DAY_ANG(d) * 3.14159265f / 180.0f;
        lv_obj_align(letter, LV_ALIGN_CENTER, (int32_t)(ALARM_TIME_LETTER_R * cosf(ang)),
                     (int32_t)(ALARM_TIME_LETTER_R * sinf(ang)));
        lv_obj_clear_flag(letter, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The two day handles on the ring (white, grey border): the range
     * endpoints. Draggable -- snap to the nearest day under the
     * finger. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *h = lv_obj_create(band);
        if (i == 0) {
            objects.settings_alarm_time_handle_start = h;
        } else {
            objects.settings_alarm_time_handle_end = h;
        }
        lv_obj_set_size(h, 34, 34);
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(h, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(h, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_ext_click_area(h, 4);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(h, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_handle, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_handle, LV_EVENT_PRESSING, (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_save, LV_EVENT_RELEASED, 0);
    }

    /* The time rollers up top: hours 0-23, minutes in x5 steps, a `:`
     * between them (the clock's "HH:MM" look). The 110 px tall
     * rollers span y 5..115 -- clear of the arc's top edge (y 140). */
    char hours[512];
    int pos = 0;
    for (int h = 0; h < 24; h++) {
        pos += snprintf(hours + pos, sizeof(hours) - pos, "%d\n", h);
    }
    char minutes[200];
    pos = 0;
    for (int m = 0; m < 60; m += 5) {
        pos += snprintf(minutes + pos, sizeof(minutes) - pos, "%d\n", m);
    }
    objects.settings_alarm_time_roller_h = settings_create_roller(obj, hours, -64, 120);
    lv_obj_align(objects.settings_alarm_time_roller_h, LV_ALIGN_CENTER, -64, -173);
    objects.settings_alarm_time_roller_m = settings_create_roller(obj, minutes, 64, 120);
    lv_obj_align(objects.settings_alarm_time_roller_m, LV_ALIGN_CENTER, 64, -173);

    lv_obj_t *colon = lv_label_create(obj);
    lv_obj_set_style_text_font(colon, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(colon, ":");
    lv_obj_set_size(colon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, -173);
    lv_obj_set_style_text_color(colon, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(objects.settings_alarm_time_roller_h, event_handler_cb_settings_alarm_time_save,
                        LV_EVENT_RELEASED, 0);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_m, event_handler_cb_settings_alarm_time_save,
                        LV_EVENT_RELEASED, 0);

    settings_alarm_time_screen_refresh();
}

void tick_screen_settings_alarm_time() {}

/* --- sleep settings: one parameterized builder (the two screens are
 * near-identical; the plan's fallback 5) ------------------------------
 *
 * SETTINGS_LIGHT_SLEEP: the idle-timeout roller (10-300 s, the
 * airplane -> light-sleep transition). SETTINGS_DEEP_SLEEP: the
 * deep-sleep countdown roller (light -> deep).
 *
 * One icon-only animation toggle per screen (no caption): a 96 px
 * solid moon glyph, white (on) / moon grey (off) -- no switch widget
 * (LV_USE_SWITCH is off). A tap flips the flag, saves, and recolors.
 * The toggle is the SLEEP-TRANSITION animation (the dusk: the pole
 * spin from the current phase to the dark side, the light-sleep
 * transition). The deep-sleep transition itself ends on the flat-black
 * disk with the backlight off, so it carries no separate animation --
 * both sleep screens show the one sleep-transition toggle. (The boot
 * sweep's boot_anim_enable flag stays in the store; it is a BOOT
 * animation, not a sleep transition, so it is not exposed here.)
 * The countdown takes effect at the next dusk arming; the idle
 * timeout is applied live via clock_idle_set_timeout_ms().
 *
 * The widgets are created once (loadScreen never destroys them), so
 * the pointers live here; [0] = light-sleep screen, [1] = deep. */
static lv_obj_t *s_sleep_roller[2];
static lv_obj_t *s_anim_glyph[2]; /* [screen] the single icon-only dusk toggle */

static void settings_anim_apply_colors(int screen) {
    lv_color_t c = settings_dusk_anim_enable() ? SETTINGS_WHITE : SETTINGS_GREY;
    lv_obj_set_style_text_color(s_anim_glyph[screen], c, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void settings_sleep_screen_refresh(int screen) {
    if (!s_sleep_roller[screen]) {
        return;
    }
    int v = screen ? (int)settings_deep_sleep_countdown_sec() : (int)settings_idle_timeout_sec();
    lv_roller_set_selected(s_sleep_roller[screen], v - 10, LV_ANIM_OFF);
    settings_anim_apply_colors(screen);
}

/* user_data = screen (0/1). */
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

/* user_data = screen (0/1): flip the sleep-transition (dusk) animation. */
static void event_handler_cb_settings_anim_toggle(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    int screen = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_dusk_anim_enable(!settings_dusk_anim_enable());
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_anim", "sleep-transition (dusk) anim -> %d (screen %d) -> %s",
             settings_dusk_anim_enable() ? 1 : 0, screen, esp_err_to_name(r));
    settings_anim_apply_colors(screen);
}

/* The single icon-only animation toggle: a 96 px solid moon glyph in a
 * 100 px clickable container (no caption). Centre (0, +58) absolute
 * 241..341: clears the roller (bottom edge 238) above and the
 * bottom-centre exit button's band (top edge 342) below. */
static void settings_anim_toggle(lv_obj_t *parent, int screen) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 100, 100);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 58);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_handler_cb_settings_anim_toggle, LV_EVENT_CLICKED, (void *)(intptr_t)screen);

    lv_obj_t *glyph = lv_label_create(row);
    add_style_label_icon_solid(glyph);
    lv_label_set_text(glyph, GLYPH_MOON);
    lv_obj_set_size(glyph, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(glyph, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);

    s_anim_glyph[screen] = glyph;
}

static void settings_create_sleep_screen(lv_obj_t *obj, int screen) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, screen ? "SLEEP COUNTDOWN" : "IDLE TIMEOUT");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -125);
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    char options[1200];
    int pos = 0;
    for (int v = 10; v <= 300; v++) {
        pos += snprintf(options + pos, sizeof(options) - pos, "%d\n", v);
    }
    /* Lifted to (0, -50) so the icon-only toggle below (241..341)
     * clears the roller's bottom edge (238). */
    s_sleep_roller[screen] = settings_create_roller(obj, options, 0, 210);
    lv_obj_align(s_sleep_roller[screen], LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(s_sleep_roller[screen], event_handler_cb_settings_sleep_roller_released, LV_EVENT_RELEASED,
                        (void *)(intptr_t)screen);

    settings_anim_toggle(obj, screen);

    settings_sleep_screen_refresh(screen);
}

void create_screen_settings_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_light_sleep = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    settings_create_sleep_screen(obj, 0);
}

void tick_screen_settings_light_sleep() {}

void create_screen_settings_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_deep_sleep = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    settings_create_sleep_screen(obj, 1);
}

void tick_screen_settings_deep_sleep() {}

/* --- WiFi settings: the AP scan list ---------------------------------
 *
 * "NETWORKS" header (wifi icon) + the AP scan results below. A
 * background scan (wifi_scan_start) runs while the screen is open; the
 * rows render when the scan finishes (the 10 ms tick polls
 * wifi_scan_in_progress() and renders once). The selected row is
 * marked by a white bar in the left margin (the SSID font has no
 * bullet/arrow glyph, so the indicator is a plain object, not text)
 * plus the white-on-grey text colour. Selection is log-only for now --
 * the circular-keyboard password entry + saving the credentials is the
 * approved follow-up. Row text is font-safe: the SSID font covers only
 * space, '-', ':', digits 0-9 and UPPERCASE A-Z (lowercase dBm would
 * render as missing-glyph boxes), so the strength is shown as "DBM". */
/* 7 rows at 24 px steps: the band between the header and the bottom-
 * centre exit button (y ~165..342) is the list's budget -- the old
 * 12 rows at 30 px ran OFF the bottom of the panel (rows 9-11) and
 * the 8th row landed on the exit button. */
#define WIFI_SETTINGS_ROWS 7

static lv_obj_t *s_wifi_rows[WIFI_SETTINGS_ROWS];
static lv_obj_t *s_wifi_sel_bar;
static bool s_wifi_scan_rendered = true;   /* the results are on the rows */
static bool s_wifi_scanning_shown = false; /* the SCANNING placeholder is up */
static int s_wifi_selected = -1;

/* Move the selection bar to the row at the given index (or hide it).
 * Row i sits at absolute y 233 + (-48 + i*24) = 185 + i*24. */
static void settings_wifi_place_sel_bar(int i) {
    if (!s_wifi_sel_bar) {
        return;
    }
    if (i < 0 || i >= WIFI_SETTINGS_ROWS) {
        lv_obj_add_flag(s_wifi_sel_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_wifi_sel_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_wifi_sel_bar, 14, 185 + i * 24 - 11);
}

static void settings_wifi_render_rows(void) {
    wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
    int selected_shown = -1;
    if (n == 0) {
        for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
            lv_label_set_text(s_wifi_rows[i], i == 0 ? "NO APS FOUND" : "");
        }
    }
    for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
        if (n == 0 || i >= n) {
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
        /* Uppercase "DBM": the SSID font lacks lowercase (see above). */
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %dDBM", (int)aps[i].rssi);
        lv_label_set_text(s_wifi_rows[i], buf);
        bool sel = (i == s_wifi_selected);
        lv_obj_set_style_text_color(s_wifi_rows[i], sel ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        if (sel) {
            selected_shown = i;
        }
    }
    settings_wifi_place_sel_bar(selected_shown);
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
    s_wifi_scanning_shown = false;
    settings_wifi_place_sel_bar(-1);
    wifi_scan_start();
}

void create_screen_settings_wifi() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_wifi = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(obj);
    add_style_label_icon_solid(header);
    lv_label_set_text(header, GLYPH_WIFI);
    lv_obj_set_size(header, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(header, LV_ALIGN_CENTER, 0, -150);
    lv_obj_set_style_text_color(header, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(obj);
    lv_obj_set_style_text_font(title, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(title, "NETWORKS");
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -84);
    lv_obj_set_style_text_color(title, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    /* The selected-row indicator: a short white bar in the left margin
     * (created first so the rows render on top where they could
     * overlap it). */
    s_wifi_sel_bar = lv_obj_create(obj);
    lv_obj_set_size(s_wifi_sel_bar, 6, 22);
    lv_obj_set_style_radius(s_wifi_sel_bar, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_wifi_sel_bar, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_sel_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wifi_sel_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_wifi_sel_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wifi_sel_bar, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < WIFI_SETTINGS_ROWS; i++) {
        lv_obj_t *row = lv_label_create(obj);
        s_wifi_rows[i] = row;
        lv_obj_set_style_text_font(row, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(row, "");
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        /* 7 rows at 24 px steps, first at absolute y 185 (offset -48):
         * the list sits in the band between the title (bottom edge ~y
         * 162) and the bottom-centre exit button (band top y 342 --
         * the last row's bottom edge is ~y 314). */
        lv_obj_align(row, LV_ALIGN_CENTER, 0, -48 + i * 24);
        lv_obj_set_style_text_color(row, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, event_handler_cb_wifi_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void tick_screen_settings_wifi() {
    if (wifi_scan_in_progress()) {
        /* Show a "SCANNING" placeholder while the scan runs (set once --
         * a per-tick set_text would invalidate every 10 ms even though
         * the text never changes). */
        if (!s_wifi_scanning_shown) {
            lv_label_set_text(s_wifi_rows[0], "SCANNING");
            s_wifi_scanning_shown = true;
        }
        return;
    }
    if (s_wifi_scan_rendered) {
        return;
    }
    settings_wifi_render_rows();
    s_wifi_scan_rendered = true;
    s_wifi_scanning_shown = false;
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
