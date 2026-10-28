#include <string.h>

#include "actions.h"
#include "clock_idle.h"
#include "fonts.h"
#include "images.h"
#include "moon_ui.h"
#include "screens.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"

#include <string.h>

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
 * mapping), as UTF-8: xmark U+F00D, gear U+F013, moon U+F186,
 * wifi U+F1EB, stopwatch U+F2F2 (solid font); alarm-clock U+F34E
 * (regular font). */
#define GLYPH_XMARK "\xE0\x80\x8D"     /* U+F00D */
#define GLYPH_GEAR "\xE0\x80\x93"      /* U+F013 */
#define GLYPH_MOON "\xE0\x86\x86"      /* U+F186 */
#define GLYPH_WIFI "\xE0\x87\xAB"      /* U+F1EB */
#define GLYPH_STOPWATCH "\xE0\x8B\xB2" /* U+F2F2 */
#define GLYPH_ALARM "\xE0\x8D\x8E"     /* U+F34E */

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

/* One icon of the hub's ring: a clickable 96 px FA glyph. The target
 * screen rides in the event's user_data (one handler for all four). */
static void event_handler_cb_hub_icon(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    enum ScreensEnum target = (enum ScreensEnum)(intptr_t)lv_event_get_user_data(e);
    loadScreen(target);
    ui_tick();
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

void create_screen_settings_alarm() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm = obj;
    settings_sub_screen_prepare(obj);
}

void tick_screen_settings_alarm() {}

void create_screen_settings_pomodoro() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_pomodoro = obj;
    settings_sub_screen_prepare(obj);
}

void tick_screen_settings_pomodoro() {}

void create_screen_settings_alarm_time() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm_time = obj;
    settings_sub_screen_prepare(obj);
}

void tick_screen_settings_alarm_time() {}

void create_screen_settings_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_light_sleep = obj;
    settings_sub_screen_prepare(obj);
}

void tick_screen_settings_light_sleep() {}

void create_screen_settings_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_deep_sleep = obj;
    settings_sub_screen_prepare(obj);
}

void tick_screen_settings_deep_sleep() {}

/* Placeholder for now (Step 7 fills it with the AP scan list). */
void create_screen_settings_wifi() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_wifi = obj;
    settings_sub_screen_prepare(obj);

    lv_obj_t *header = lv_label_create(obj);
    add_style_label_icon_solid(header);
    lv_label_set_text(header, GLYPH_WIFI);
    lv_obj_set_size(header, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(header, LV_ALIGN_CENTER, 0, -90);
    lv_obj_set_style_text_color(header, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);
}

void tick_screen_settings_wifi() {}

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
