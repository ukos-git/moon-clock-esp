#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "clock_alarm_scheduler.h"
#include "clock_idle.h"
#include "clock_sntp.h"
#include "esp_log.h"
#include "fonts.h"
#include "images.h"
#include "moon_ui.h"
#include "screens.h"
#include "settings_store.h"
#include "styles.h"
#include "ui.h"
#include "ui_px.h"
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
 * circle-xmark U+F057 (the settings exit; the glyph IS the circle, so
 * the exit button is the glyph alone, no ring chrome);
 * alarm-clock U+F34E (the regular font). */
#define GLYPH_XMARK "\xEF\x81\x97"     /* U+F057 */
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
static void settings_wheel_prefill_ssid(const char *ssid);
static void settings_wheel_screen_refresh(void);
/* settings_screen_refresh_for is non-static (declared in screens.h; the
 * TEST_WIFI_SCAN_SEC hook calls it directly). */

/* The wheel keyboard's prefilled SSID (the AP-row tap sets it before the
 * screen loads; the wheel section owns the rest of its state). */
static char s_wheel_ssid[33];

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

/* The exit button: the fa-circle-xmark glyph alone (U+F057, a filled
 * white circle with a cut-out cross -- the glyph IS the button). The
 * tap box is the glyph's natural 96 px (the size arg widens it, e.g.
 * the hub's central 150 px); a tap on it fires LV_EVENT_CLICKED on the
 * returned obj. Used central (150 px) on the hub and bottom-centre
 * (96 px) on every sub-screen -- the middle of the screen is occupied
 * by the settings' working widgets, so the exit lives at the bottom
 * edge and the sub-screens' layouts keep the band around it clear. */
lv_obj_t *settings_create_exit_button(lv_obj_t *parent, int size) {
    /* The exit IS the glyph: fa-circle-xmark (U+F057) is a filled
     * circle with a cut-out cross, so the white glyph is the whole
     * button and no ring chrome is drawn. The glyph ink is 96x96 px
     * at the 96 px font, so the tap box is the glyph's natural box
     * (a size arg above 96 widens the tap area only, e.g. the hub's
     * central 150 px); below 96 it would clip the disk, so it floors
     * at 96. */
    const int box = size > UI_PX(96) ? size : UI_PX(96);
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_set_size(ring, box, box);
    lv_obj_set_style_bg_opa(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *xmark = lv_label_create(ring);
    add_style_label_icon_solid(xmark);
    lv_label_set_text(xmark, GLYPH_XMARK);
    lv_obj_set_size(xmark, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(xmark);
    lv_obj_set_style_text_color(xmark, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    return ring;
}

/* The hub's central exit: leaving settings goes back to the HOME
 * screen -- the airplane (modem-sleep) clock screen. No background
 * sync is kicked off; the home tick shows the current time. */
static void event_handler_cb_hub_exit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
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
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // modem_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.modem_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 14);
        }
        {
            // arc-clock-seconds-modem
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds_modem = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_modem_sleep_arc_clock_seconds_modem, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
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
    tick_user_widget_clock(14);
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
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // wifi_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.wifi_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 19);
        }
        {
            // arc-clock-seconds-wifi
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds_wifi = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_wifi_arc_clock_seconds_wifi, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, UI_PX(10), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff787878), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            // screen-indicator-wifi (the connection state: the transient
            // WiFi screen is where the handshake happens, so the icon
            // lives here, not on the airplane screen)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.wifi_indicator_wifi = obj;
            lv_obj_set_pos(obj, UI_PX(185), UI_PX(320));
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, GLYPH_WIFI);
            /* Start grey = "still connecting": the tick recolours it white
             * once wifi_manager_is_connected() flips (see tick_screen_wifi). */
            lv_obj_set_style_text_color(obj, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
            /* Non-clickable: a tap on the indicator must still open the
             * settings hub (the screen-root handler), not die here. */
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    /* Touch pass-through (tap = open the settings hub), same as the
     * airplane screen. */
    unclickable_recursive(objects.wifi_clock);
    lv_obj_add_event_cb(obj, event_handler_cb_home_tap, LV_EVENT_CLICKED, 0);

    tick_screen_wifi();
}

void tick_screen_wifi() {
    tick_user_widget_clock(19);
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock_seconds_wifi);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock_seconds_wifi;
            lv_arc_set_value(objects.arc_clock_seconds_wifi, new_val);
            tick_value_change_obj = NULL;
        }
    }
    /* The connection indicator (grey = still connecting, white =
     * connected). The state flips at most once per sync, so only
     * recolour on a change -- the tick itself is a state compare. */
    {
        static bool s_wifi_ind_state = false; /* starts connecting */
        bool connected = wifi_manager_is_connected();
        if (connected != s_wifi_ind_state) {
            s_wifi_ind_state = connected;
            lv_obj_set_style_text_color(objects.wifi_indicator_wifi, connected ? SETTINGS_WHITE : SETTINGS_GREY,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

void create_screen_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.light_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // light_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.light_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 24);
        }
    }

    tick_screen_light_sleep();
}

void tick_screen_light_sleep() { tick_user_widget_clock(24); }

void create_screen_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.deep_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // deep_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.deep_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 28);
        }
        {
            // screen-indicator-sleep1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_sleep1 = obj;
            lv_obj_set_pos(obj, UI_PX(185), UI_PX(320));
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_deep_sleep();
}

void tick_screen_deep_sleep() { tick_user_widget_clock(28); }

/* The alarm screen dismisses on touch. The idle-wake path (touch while
 * asleep -> action_wake_screen) only fires while the device is
 * in idle mode; while awake, a tap just resets the idle timer, so a
 * dedicated input-device callback dismisses it instead
 * (action_register_alarm_touch in actions.c). */
void create_screen_alarm() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.alarm = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    {
        lv_obj_t *parent_obj = obj;
        {
            // alarm_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.alarm_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 33);
        }
        {
            // screen-indicator-alarm
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_alarm = obj;
            lv_obj_set_pos(obj, UI_PX(185), UI_PX(320));
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_regular(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_alarm();
}

void tick_screen_alarm() { tick_user_widget_clock(33); }

/* ---------------- settings screens ----------------
 *
 * The settings hub is a moon-less screen (settings_screen_prepare):
 * black background, a central circle-xmark exit (the fa-circle-xmark
 * glyph alone), and a ring of sub-screen icons around it. The
 * sub-screens are moon-less too, with the shared bottom-centre exit
 * back to the CALLING level (the hub, or the alarm screen for its two
 * sub-screens) plus their own working widgets. */

void create_screen_settings_hub() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_hub = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    settings_screen_prepare(obj);
    /* The root is not clickable: only the explicit ring icons and the
     * central exit button take touches. */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    /* Central concentric exit (150 px ring). */
    objects.settings_hub_exit = settings_create_exit_button(obj, UI_PX(150));
    lv_obj_center(objects.settings_hub_exit);
    lv_obj_add_event_cb(objects.settings_hub_exit, event_handler_cb_hub_exit, LV_EVENT_CLICKED, 0);

    /* Icon ring at radius ~165 (45-degree diagonals): alarm settings
     * (top-right), light sleep (bottom-right), deep sleep (bottom-left),
     * WiFi settings (top-left). Moon weight distinguishes the two
     * sleep screens (regular / solid). */
    const int32_t R = UI_PX(117); /* ~165 px radius at 45 deg (the scaled ring) */
    settings_hub_icon(obj, GLYPH_ALARM, &ui_font_fontawesome7_regular_96_icons, R, -R, SCREEN_ID_SETTINGS_ALARM);
    settings_hub_icon(obj, GLYPH_MOON, &ui_font_fontawesome7_regular_96_icons, R, R, SCREEN_ID_SETTINGS_LIGHT_SLEEP);
    settings_hub_icon(obj, GLYPH_MOON, &ui_font_fontawesome7_solid_96_icons, -R, R, SCREEN_ID_SETTINGS_DEEP_SLEEP);
    settings_hub_icon(obj, GLYPH_WIFI, &ui_font_fontawesome7_solid_96_icons, -R, -R, SCREEN_ID_SETTINGS_WIFI);
}

void tick_screen_settings_hub() {}

/* One sub-screen shell: black root (non-clickable) + the shared
 * bottom-centre exit button (the 96 px circle-xmark glyph) back to the
 * CALLING level (the hub, or the alarm screen for its two
 * sub-screens). The sub-screens add their own widgets on top; the band
 * around the exit (x 185..281, y 336..432) stays free of widgets. */
static void settings_sub_screen_prepare(lv_obj_t *obj, enum ScreensEnum exit_target) {
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *exit = settings_create_exit_button(obj, UI_PX(90));
    lv_obj_align(exit, LV_ALIGN_BOTTOM_MID, 0, -UI_PX(34));
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
 * icons and the alarm-settings entries; also called directly by the
 * TEST_WIFI_SCAN_SEC hook, which has no event handler to drive it). */
void settings_screen_refresh_for(enum ScreensEnum target) {
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
    case SCREEN_ID_SETTINGS_WIFI_PASSWORD:
        settings_wheel_screen_refresh();
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
    lv_obj_align(objects.settings_alarm_master, LV_ALIGN_CENTER, 0, -UI_PX(30));
    lv_obj_add_flag(objects.settings_alarm_master, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(objects.settings_alarm_master, event_handler_cb_settings_alarm_master, LV_EVENT_CLICKED, 0);

    /* Two entries at the diagonals (stopwatch = pomodoro, alarm-clock =
     * real alarm), 96 px; colours set by the entry refresh. */
    const int32_t R = UI_PX(130);
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
    lv_obj_set_height(r, UI_PX(110));
    lv_obj_align(r, LV_ALIGN_CENTER, x, UI_PX(90));
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
        pos += snprintf(hours + pos, sizeof(hours) - pos, "%02d\n", h);
    }
    char minutes[600];
    pos = 0;
    for (int m = 0; m < 60; m++) {
        pos += snprintf(minutes + pos, sizeof(minutes) - pos, "%02d\n", m);
    }

    /* The two rollers read like the clock's "HH:MM": a `:` sits between
     * them (the bold 96 numbers font carries the colon) and the WHOLE
     * display sits at the home clock label's position -- centred on the
     * screen, vertical centre y 210 (the clock label's centre), so the
     * `:` is centred on the screen exactly where the clock's `:` is.
     * The 110 px tall rollers span y 155..265 -- clear of the
     * bottom-centre exit button's band (top edge y 342).
     * The x offsets replicate the clock label's own glyph centres
     * (its "12" centres at screen x ~181, "35" at ~291, the `:` at
     * ~223 -- the label is centred at 234.5, 10 px off the panel
     * centre): the hour slot centres at 153 (-80), the minute slot
     * at 303 (+70), and the `:` at 223 (-10), so the whole display
     * lands exactly where the home clock's "HH:MM" would. */
    objects.settings_pomodoro_roller_h = settings_create_roller(obj, hours, -UI_PX(80), UI_PX(120));
    lv_obj_align(objects.settings_pomodoro_roller_h, LV_ALIGN_CENTER, -UI_PX(80), -UI_PX(23));
    objects.settings_pomodoro_roller_m = settings_create_roller(obj, minutes, UI_PX(70), UI_PX(120));
    lv_obj_align(objects.settings_pomodoro_roller_m, LV_ALIGN_CENTER, UI_PX(70), -UI_PX(23));

    lv_obj_t *colon = lv_label_create(obj);
    lv_obj_set_style_text_font(colon, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(colon, ":");
    lv_obj_set_size(colon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(colon, LV_ALIGN_CENTER, -UI_PX(10), -UI_PX(23));
    lv_obj_set_style_text_color(colon, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_CLICKABLE);

    /* The caption above the display (the clock label has none -- this
     * screen needs one to say what the value IS). */
    objects.settings_pomodoro_caption = lv_label_create(obj);
    lv_obj_set_style_text_font(objects.settings_pomodoro_caption, &ui_font_montserrat_regular_32_ssid,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(objects.settings_pomodoro_caption, "REPEAT INTERVAL");
    lv_obj_set_size(objects.settings_pomodoro_caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(objects.settings_pomodoro_caption, LV_ALIGN_CENTER, 0, -UI_PX(100));
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
 * 51 deg wedges were hard to press and hard to tell apart). The dark
 * track (LV_PART_MAIN) spans the whole arc, Monday (left) to Sunday
 * (right); the moon-colour INDICATOR spans the two draggable day
 * handles (the range, inclusive). The vendored arc is single-knob and
 * two overlapping knob arcs could not both take a press, so the range
 * endpoints are two plain objects on the ring (the arc itself is
 * non-clickable): a press snaps the handle to the nearest day under
 * the touch point and follows the drag. The "Mo-Fr" range summary
 * above the time carries the day meaning (the former day letters are
 * gone), so the ring can push out to the panel edge (below).
 *
 * Geometry (screen coords, 0 deg = right, 90 deg = down): day d
 * (0 = Mon) sits at 181 + 30d degrees -- the +1 deg offset keeps every
 * stored arc angle off the 360/0 wrap edge (an indicator end angle of
 * exactly 360 stores as 360 and the SW arc mask clamps it to 359,
 * drawing a 1 deg gap at the right edge of the band). The arc centre
 * is the SCREEN centre (233, 233). Because the band is only the UPPER
 * half-circle, its lowest drawn pixels are the two ends (y ~233) --
 * clear of the bottom-centre exit button's band. The time rollers sit
 * at the clock position (vertical centre y 210, the home clock
 * label's centre) inside the ring's hole; the "Mo-Fr" range text sits
 * above them.
 *
 * State: s_alarm_range_{start,end} (day indices, Mon..Sun order) -- the
 * working range (the widgets persist across loadScreen, so it must
 * too; re-seeded from the rule on every entry). The range maps to the
 * scheduler's weekday mask (bit 0 = Sunday .. 6 = Saturday); the
 * Mon-Fri default is 0x3E. A stored non-contiguous mask is displayed
 * rounded to its outermost days (a two-ended range is always
 * contiguous). Minutes are multiples of 5 -- 10:03 is not a wake-up
 * time. */

/* The ring matches the home screen's seconds arc: a THIN ring pushed
 * to the outer edge (outer radius 223, 10 px from the panel edge),
 * the day-range as the moon-colour indicator over a dark track. The
 * day letters that used to crowd the outer margin are gone (the
 * range summary "Mo-Fr" above the time carries the day meaning), so
 * the ring can own that outer band.
 * lv_arc maps object size 2(R + W) -> outer radius R, inner R - W
 * (get_center: radius = obj width/2 - arc_width). The home seconds
 * arc is a 466x466 object at (0,0), width 10 -> outer 223, inner 213;
 * this is that exact ring. */
#define ALARM_TIME_ARC_CX (UI_PANEL_PX / 2)
#define ALARM_TIME_ARC_CY (UI_PANEL_PX / 2)
#define ALARM_TIME_ARC_R UI_PX(223)    /* the ring's OUTER radius (home-seconds-arc match) */
#define ALARM_TIME_ARC_WIDTH UI_PX(10) /* the ring width (home-seconds-arc match) */
#define ALARM_TIME_HANDLE_R UI_PX(218) /* the day handles' radius (the ring midline) */

#define ALARM_DAY_ANG(d) (181 + 30 * (d)) /* 0 = Monday .. 6 = Sunday */

/* The range's working endpoints (day indices, 0 = Monday .. 6 =
 * Sunday; re-seeded from the stored rule on every entry). */
static int s_alarm_range_start = 0;
static int s_alarm_range_end = 4;
static lv_obj_t *s_alarm_range_label; /* the "Mo-Fr" summary above the time */

/* Day abbreviations (Monday..Sunday) for the range text. */
static const char *const ALARM_DAY_ABBR[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};

/* The range summary text: "We" for a single day, "Mo-Fr" for a range
 * (wrap included: "Fr-Mo"). Shown above the time display in the small
 * range font (lowercase + hyphen -- the SSID font has neither). */
static void settings_alarm_time_range_text(void) {
    if (!s_alarm_range_label) {
        return;
    }
    char buf[16];
    int s = s_alarm_range_start, e = s_alarm_range_end;
    if (s == e) {
        snprintf(buf, sizeof buf, "%s", ALARM_DAY_ABBR[s]);
    } else {
        snprintf(buf, sizeof buf, "%s-%s", ALARM_DAY_ABBR[s], ALARM_DAY_ABBR[e]);
    }
    lv_label_set_text(s_alarm_range_label, buf);
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
    lv_obj_t *handles[2] = {objects.settings_alarm_time_handle_start, objects.settings_alarm_time_handle_end};
    int ends[2] = {s, e};
    for (int i = 0; i < 2; i++) {
        float ang = (float)ALARM_DAY_ANG(ends[i]) * 3.14159265f / 180.0f;
        lv_obj_align(handles[i], LV_ALIGN_CENTER, (int32_t)(ALARM_TIME_HANDLE_R * cosf(ang)),
                     (int32_t)(ALARM_TIME_HANDLE_R * sinf(ang)));
    }
    settings_alarm_time_range_text();
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
    /* Read-back (self-diagnosis): what the scheduler actually holds AFTER
     * the write + re-parse. A MISMATCH pins the fault to the write/re-parse
     * layer; a MATCH with the button still grey would pin it to the UI
     * colour refresh (settings_alarm_screen_refresh / rule_types read). */
    int rh = -1, rm = -1;
    uint8_t rmask = 0;
    uint8_t types = clock_alarm_rule_types();
    esp_err_t rr = clock_alarm_get_fixed_rule(&rh, &rm, &rmask);
    ESP_LOGW("settings_alarm_time", "read-back: rule_types=0x%02x fixed=%s got %02d:%02d mask 0x%02x %s", types,
             rr == ESP_OK ? "ok" : esp_err_to_name(rr), rh, rm, rmask,
             (rr == ESP_OK && rh == h && rm == m && rmask == mask && (types & 0x02)) ? "MATCH" : "MISMATCH");
}

/* Seed the rollers + range from the stored fixed-time rule (or 12:00,
 * Mon-Fri, when none). A non-contiguous mask is displayed rounded to
 * its outermost days (a two-ended range is always contiguous). */
static void settings_alarm_time_screen_refresh(void) {
    if (!objects.settings_alarm_time_band) {
        return;
    }
    int h = 12, m = 0;
    uint8_t mask = 0x3E; /* the Mon-Fri default (bit 0 = Sunday) */
    if (clock_alarm_get_fixed_rule(&h, &m, &mask) != ESP_OK) {
        h = 12;
        m = 0;
        mask = 0x3E;
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
     * right), the same thin outer ring as the home screen's seconds
     * arc (a 466x466 object at (0,0); its ring reaches r 223, 10 px
     * from the panel edge -- it clears the exit button by a mile).
     * Track = the theme's dark (the home track's colour, no explicit
     * MAIN arc_color needed), the range = the moon colour (the
     * seconds indicator's SETTINGS_GREY). */
    lv_obj_t *band = lv_arc_create(obj);
    objects.settings_alarm_time_band = band;
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_left(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(band, 181, 1); /* the dark full track (the 180 deg wrap) */
    lv_arc_set_angles(band, 181, 181);  /* the indicator: re-seeded on entry */
    lv_obj_set_style_arc_width(band, ALARM_TIME_ARC_WIDTH, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(band, ALARM_TIME_ARC_WIDTH, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(band, SETTINGS_GREY, LV_PART_INDICATOR | LV_STATE_DEFAULT);
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

    /* The two day handles on the ring (the moon colour, the home
     * knob's look): the range endpoints. Draggable -- snap to the
     * nearest day under the finger. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *h = lv_obj_create(band);
        if (i == 0) {
            objects.settings_alarm_time_handle_start = h;
        } else {
            objects.settings_alarm_time_handle_end = h;
        }
        lv_obj_set_size(h, UI_PX(34), UI_PX(34));
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        /* The home knob's colour (the theme grey) -- 34 px, it pokes
         * out of the 10 px ring and stays visible on track and range. */
        lv_obj_set_style_bg_color(h, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_ext_click_area(h, UI_PX(4));
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(h, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_handle, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_handle, LV_EVENT_PRESSING, (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, event_handler_cb_settings_alarm_time_save, LV_EVENT_RELEASED, 0);
    }

    /* The time rollers at the home clock label's position (vertical
     * centre y 210, the clock label's centre): hours 0-23, minutes in
     * x5 steps, a `:` between them -- the clock's "HH:MM" look, same
     * margins. The 110 px tall rollers span y 155..265 -- inside the
     * arc's hole (ring inner edge r 146 -> y 87..379 at x 0) and clear
     * of the exit band (top edge y 342). */
    char hours[512];
    int pos = 0;
    for (int h = 0; h < 24; h++) {
        pos += snprintf(hours + pos, sizeof(hours) - pos, "%02d\n", h);
    }
    char minutes[200];
    pos = 0;
    for (int m = 0; m < 60; m += 5) {
        pos += snprintf(minutes + pos, sizeof(minutes) - pos, "%02d\n", m);
    }
    /* Same glyph-centre offsets as the pomodoro display (hour slot at
     * 153, minute slot at 303, the `:` at 223 = the home clock's own
     * colon position). */
    objects.settings_alarm_time_roller_h = settings_create_roller(obj, hours, -UI_PX(80), UI_PX(120));
    lv_obj_align(objects.settings_alarm_time_roller_h, LV_ALIGN_CENTER, -UI_PX(80), -UI_PX(23));
    objects.settings_alarm_time_roller_m = settings_create_roller(obj, minutes, UI_PX(70), UI_PX(120));
    lv_obj_align(objects.settings_alarm_time_roller_m, LV_ALIGN_CENTER, UI_PX(70), -UI_PX(23));

    lv_obj_t *colon = lv_label_create(obj);
    lv_obj_set_style_text_font(colon, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(colon, ":");
    lv_obj_set_size(colon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(colon, LV_ALIGN_CENTER, -UI_PX(10), -UI_PX(23));
    lv_obj_set_style_text_color(colon, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_CLICKABLE);

    /* The selected day-range summary ("Mo-Fr", "We", "Fr-Mo"): above
     * the time display, centred (y 133 absolute). The small range font
     * carries lowercase + the hyphen. */
    s_alarm_range_label = lv_label_create(obj);
    lv_obj_set_style_text_font(s_alarm_range_label, &ui_font_montserrat_regular_32_range,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_alarm_range_label, "Mo-Fr");
    lv_obj_set_size(s_alarm_range_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_alarm_range_label, LV_ALIGN_CENTER, 0, -UI_PX(100));
    lv_obj_set_style_text_color(s_alarm_range_label, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_alarm_range_label, LV_OBJ_FLAG_CLICKABLE);

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
 * SETTINGS_LIGHT_SLEEP: the idle-timeout roller (1-30 MINUTES, the
 * airplane -> light-sleep transition; stored as seconds).
 * SETTINGS_DEEP_SLEEP: the deep-sleep countdown roller (light ->
 * deep), also in minutes. The value roller and the animation toggle
 * sit on ONE horizontal line (roller left of centre, toggle right of
 * centre).
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
    int sec = screen ? (int)settings_deep_sleep_countdown_sec() : (int)settings_idle_timeout_sec();
    /* The roller is in MINUTES (1..30); the store is in seconds. */
    int minutes = sec / 60;
    if (minutes < 1) {
        minutes = 1;
    }
    if (minutes > 30) {
        minutes = 30;
    }
    lv_roller_set_selected(s_sleep_roller[screen], minutes - 1, LV_ANIM_OFF);
    settings_anim_apply_colors(screen);
}

/* user_data = screen (0/1). The roller value is MINUTES (1..30); the
 * store and the live idle timer are in seconds. */
static void event_handler_cb_settings_sleep_roller_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int screen = (int)(intptr_t)lv_event_get_user_data(e);
    int minutes = settings_roller_value(s_sleep_roller[screen]);
    if (minutes < 1) {
        minutes = 1;
    }
    uint32_t sec = (uint32_t)minutes * 60u;
    if (screen) {
        settings_set_deep_sleep_countdown_sec(sec);
    } else {
        settings_set_idle_timeout_sec(sec);
        clock_idle_set_timeout_ms((uint64_t)sec * 1000ULL);
    }
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_sleep", "screen %d value %d min -> %s", screen, minutes, esp_err_to_name(r));
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
 * 100 px clickable container (no caption). On the same horizontal line
 * as the minutes roller (offset +90 from screen centre, spans x
 * 273..373): the roller sits at -90 (spans 38..248), so the two share
 * the line with a 25 px gap between them. Vertically centred, so the
 * row clears the bottom-centre exit button's band (top edge y 342). */
static void settings_anim_toggle(lv_obj_t *parent, int screen) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, UI_PX(100), UI_PX(100));
    lv_obj_align(row, LV_ALIGN_CENTER, UI_PX(90), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Kill the theme's default border (a visible square around the glyph)
     * and the shadow: the glyph is the only visible thing. */
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -UI_PX(125));
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    /* Minutes, 1..30 (stored internally as seconds, min 60 / max 1800,
     * default 1 min): the value picker is ONE roller on the shared
     * horizontal line, the animation toggle on the right of it. */
    char options[512];
    int pos = 0;
    for (int v = 1; v <= 30; v++) {
        pos += snprintf(options + pos, sizeof(options) - pos, "%d\n", v);
    }
    s_sleep_roller[screen] = settings_create_roller(obj, options, 0, UI_PX(210));
    lv_obj_align(s_sleep_roller[screen], LV_ALIGN_CENTER, -UI_PX(90), 0);
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
 * background scan (wifi_scan_begin) runs while the screen is open; the
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
    lv_obj_set_pos(s_wifi_sel_bar, UI_PX(14), UI_PX(185) + i * UI_PX(24) - UI_PX(11));
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

/* user_data = the row index. A tap enters the circular wheel keyboard
 * with this AP's SSID prefilled (the raw SSID -- the row display
 * uppercases for font-safety only, the credential must keep the
 * original case). */
static void event_handler_cb_wifi_row(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    s_wifi_selected = (int)(intptr_t)lv_event_get_user_data(e);
    wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
    if (n == 0 || s_wifi_selected < 0 || s_wifi_selected >= n) {
        return;
    }
    settings_wifi_render_rows();
    settings_wheel_prefill_ssid(aps[s_wifi_selected].ssid);
    loadScreen(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
    ui_tick();
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
    ESP_LOGW("settings_wifi", "AP row %d -> wheel keyboard (SSID %zu chars)", s_wifi_selected, strlen(s_wheel_ssid));
}

static void settings_wifi_screen_refresh(void) {
    s_wifi_selected = -1;
    s_wifi_scan_rendered = false;
    s_wifi_scanning_shown = false;
    settings_wifi_place_sel_bar(-1);
    wifi_scan_begin();
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
    lv_obj_align(header, LV_ALIGN_CENTER, 0, -UI_PX(150));
    lv_obj_set_style_text_color(header, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(obj);
    lv_obj_set_style_text_font(title, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(title, "NETWORKS");
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -UI_PX(84));
    lv_obj_set_style_text_color(title, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    /* The selected-row indicator: a short white bar in the left margin
     * (created first so the rows render on top where they could
     * overlap it). */
    s_wifi_sel_bar = lv_obj_create(obj);
    lv_obj_set_size(s_wifi_sel_bar, UI_PX(6), UI_PX(22));
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
        lv_obj_align(row, LV_ALIGN_CENTER, 0, -UI_PX(48) + i * UI_PX(24));
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

/* --- WiFi password: the circular wheel keyboard ------------------------
 *
 * Reached by tapping an AP row on the WiFi screen (the SSID is prefilled,
 * case kept). Two concentric letter rings around the 466x466 centre:
 *
 *  - the INNER frame (at rest): the 26 slots of the active mode, grouped
 *    in threes (bold divider at each group start) at r 150..186. The
 *    grouping is the wayfinding device -- the eye aims at 9 groups, not
 *    26 targets.
 *  - the OUTER frame (revealed on touch-down anywhere in the ring band):
 *    a continuous band at r 192..226 where the letter under the finger is
 *    lit over a 2x-inner-angle span (27 deg, the overlap is resolved by
 *    nearest-slot-centre in the hit-test).
 *  - a 4-key REGISTER/SHIFT arc (abc / ABC / 123 / #$) at r 88..116,
 *    grouped as one arc segment but each key individually tappable; the
 *    active mode is lit.
 *  - the centre: the running password (unmasked -- the user is physically
 *    present) + the selected letter, large, while sweeping. A short tap
 *    on the centre COMMITS (save + connect); a long press BACKSPACES.
 *
 * Interaction: press (anywhere in the radial band r 140..233 --
 * radially forgiving, the fingertip ~ the band width) reveals the outer
 * frame; sweeping moves the highlight (nearest slot centre, inverse-
 * transformed, so it is correct under the 90-degree orientation roll);
 * RELEASE in the band commits the highlighted letter (a quick tap on an
 * inner letter = press+release there = the same commit); release OUT of
 * the band commits nothing. The 2 s press orientation re-sync also fires
 * on long sweeps (pose-stability gated, re-anchors the home pose only --
 * the wheel is transform-aware, so it is benign).
 *
 * The exit deviates from the house sub-screen pattern on purpose: the
 * full-360-degree ring occupies the bottom-centre band, so the wheel gets
 * the mockup's small in-ring exit (26 px circle-x at r 135) instead of
 * settings_sub_screen_prepare()'s 90 px bottom exit.
 *
 * All wheel widgets are LOCAL statics (the layout contract in screens.h
 * only names the screen roots and the clock-widget slots). Angles are
 * LVGL's (0 deg = right, 90 deg = down, clockwise); the mockup's
 * 0 deg = up geometry is a constant -90 offset. */
#define WHEEL_C (UI_PANEL_PX / 2)    /* the panel centre */
#define WHEEL_INNER_R_MID UI_PX(168) /* inner letter row (band 150..186) */
#define WHEEL_OUTER_R_MID UI_PX(209) /* outer letter row (band 192..226) */
#define WHEEL_MODE_R_MID UI_PX(102)  /* mode-key row (band 88..116) */
#define WHEEL_HIT_R_IN UI_PX(140)    /* the forgiving press band: 140..233 */
#define WHEEL_OUTER_SPAN 13          /* the hot sector is +/-13 deg (2x the 26-letter inner angle) */
#define WHEEL_PW_MAX 63              /* keep < MAX_PASSWORD_LEN (64) */
#define WHEEL_EXIT_Y UI_PX(368)      /* the in-ring exit centre (r 135 below centre) */

/* The four register modes (all printable ASCII -- the 32 px font now
 * covers 32-126). 123 / #$ use fewer slots, so those keys get WIDER
 * arc shares (360/n per slot) -- bigger sweep targets than letters. */
static const char *const s_wheel_sets[4] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    "!@#$%^&*()-_+=[]{}:;.,?/",
};

static char s_wheel_pw[WHEEL_PW_MAX + 1];
static int s_wheel_pw_len = 0;
static int s_wheel_mode = -1; /* -1 until the first set_mode drew the letters */
static int s_wheel_n = 0;     /* slots of the active mode (<= 26) */
static char s_wheel_chars[26];
static int s_wheel_hot = -1; /* the highlighted slot (the centre preview) */
static bool s_wheel_revealed = false;

static lv_obj_t *s_wheel_catch; /* the transparent press band (r 140..233) */
static lv_obj_t *s_wheel_outer; /* the revealed outer band + hot sector */
static lv_obj_t *s_wheel_letters_in[26];
static lv_obj_t *s_wheel_letters_out[26];
static lv_obj_t *s_wheel_ticks[26];
static lv_obj_t *s_wheel_mode_arc;
static lv_obj_t *s_wheel_mode_keys[4];
static lv_obj_t *s_wheel_center;
static lv_obj_t *s_wheel_pw_label;
static lv_obj_t *s_wheel_sel_label;
static lv_obj_t *s_wheel_ssid_label;

/* Slot j's centre angle (LVGL degrees, 0 = right, 90 = down, CW). */
static float wheel_slot_center(int j, int n) { return (j + 0.5f) * 360.0f / (float)n - 90.0f; }

/* Map the indev point (display coords) to a slot: inverse the screen
 * root's transform (the orientation roll) first -- the same idiom as
 * alarm_time_point_to_day(). Returns false when the point is outside the
 * forgiving radial band. */
static bool wheel_point_to_slot(int *out_slot) {
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return false;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_obj_transform_point(objects.settings_wifi_password, &p, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE);
    float dx = (float)(p.x - WHEEL_C);
    float dy = (float)(p.y - WHEEL_C);
    float r = sqrtf(dx * dx + dy * dy);
    if (r < WHEEL_HIT_R_IN || r > (float)WHEEL_C) {
        return false;
    }
    /* atan2f: radians, 0 = right, 90 = down -- the LVGL slot convention. */
    float theta = atan2f(dy, dx) * 180.0f / 3.14159265f;
    while (theta < 0.0f) {
        theta += 360.0f;
    }
    while (theta >= 360.0f) {
        theta -= 360.0f;
    }
    int best = 0;
    float best_diff = 1e9f;
    for (int j = 0; j < s_wheel_n; j++) {
        float diff = theta - wheel_slot_center(j, s_wheel_n);
        while (diff > 180.0f) {
            diff -= 360.0f;
        }
        while (diff < -180.0f) {
            diff += 360.0f;
        }
        float ad = diff < 0.0f ? -diff : diff;
        if (ad < best_diff) {
            best_diff = ad;
            best = j;
        }
    }
    *out_slot = best;
    return true;
}

static void settings_wheel_pw_label(void) {
    /* Unmasked, but the preview shows only the LAST 6 chars (a 63-char
     * password would run off the ring; s_wheel_pw keeps it all). */
    if (s_wheel_pw_len == 0) {
        lv_label_set_text(s_wheel_pw_label, "....");
        return;
    }
    char buf[16];
    int start = s_wheel_pw_len > 6 ? s_wheel_pw_len - 6 : 0;
    int pos = start > 0 ? snprintf(buf, sizeof(buf), "...") : 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%.*s", s_wheel_pw_len - start, s_wheel_pw + start);
    (void)pos;
    lv_label_set_text(s_wheel_pw_label, buf);
}

/* Recolour both letter rings: at rest the inner ring is the 175 grey;
 * revealed, the inner dims and the hot letter lights in both rings. */
static void settings_wheel_recolor_letters(void) {
    lv_color_t in_rest = lv_color_hex(0xAF7878);
    lv_color_t in_dim = lv_color_hex(0x555555);
    lv_color_t out_dim = lv_color_hex(0x5F5F5F);
    for (int j = 0; j < 26; j++) {
        if (j >= s_wheel_n) {
            continue;
        }
        bool hot = (j == s_wheel_hot);
        lv_color_t in_c = !s_wheel_revealed ? in_rest : (hot ? SETTINGS_WHITE : in_dim);
        lv_obj_set_style_text_color(s_wheel_letters_in[j], in_c, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_wheel_letters_out[j], hot ? SETTINGS_WHITE : out_dim,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void settings_wheel_set_hot(int j) {
    if (j == s_wheel_hot) {
        return;
    }
    s_wheel_hot = j;
    if (j < 0 || j >= s_wheel_n) {
        lv_obj_add_flag(s_wheel_sel_label, LV_OBJ_FLAG_HIDDEN);
        if (j < 0) {
            lv_arc_set_angles(s_wheel_outer, 0, 0); /* zero span draws nothing */
        }
    } else {
        lv_obj_clear_flag(s_wheel_sel_label, LV_OBJ_FLAG_HIDDEN);
        char one[2] = {s_wheel_chars[j], 0};
        lv_label_set_text(s_wheel_sel_label, one);
        if (s_wheel_revealed) {
            float c = wheel_slot_center(j, s_wheel_n);
            float a0 = c - (float)WHEEL_OUTER_SPAN;
            float a1 = c + (float)WHEEL_OUTER_SPAN;
            while (a0 < 0.0f) {
                a0 += 360.0f;
                a1 += 360.0f;
            }
            while (a1 >= 360.0f) {
                a0 -= 360.0f;
                a1 -= 360.0f;
            }
            lv_arc_set_angles(s_wheel_outer, (lv_value_precise_t)a0, (lv_value_precise_t)a1);
        }
    }
    settings_wheel_recolor_letters();
}

/* Reveal the outer frame (press-down) / hide it (release or cancel). */
static void settings_wheel_set_revealed(bool on) {
    if (on == s_wheel_revealed) {
        return;
    }
    s_wheel_revealed = on;
    if (on) {
        lv_obj_clear_flag(s_wheel_outer, LV_OBJ_FLAG_HIDDEN);
        /* The password shifts left, the selected letter appears right. */
        lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, -30, 0);
    } else {
        lv_obj_add_flag(s_wheel_outer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, 0, 0);
    }
    settings_wheel_recolor_letters();
}

/* Apply a register mode: re-string the 26 slots, re-place the letters +
 * dividers (the slot angle is 360/n), light the mode key. */
static void settings_wheel_set_mode(int m) {
    if (m == s_wheel_mode) {
        return;
    }
    s_wheel_mode = m;
    s_wheel_n = (int)strlen(s_wheel_sets[m]);
    for (int j = 0; j < 26; j++) {
        s_wheel_chars[j] = j < s_wheel_n ? s_wheel_sets[m][j] : 0;
    }
    for (int j = 0; j < 26; j++) {
        bool on = j < s_wheel_n;
        float a = on ? wheel_slot_center(j, s_wheel_n) : 0.0f;
        float ar = a * 3.14159265f / 180.0f;
        if (on) {
            int32_t dx_in = (int32_t)(WHEEL_INNER_R_MID * cosf(ar));
            int32_t dy_in = (int32_t)(WHEEL_INNER_R_MID * sinf(ar));
            char one[2] = {s_wheel_chars[j], 0};
            lv_label_set_text(s_wheel_letters_in[j], one);
            lv_obj_align(s_wheel_letters_in[j], LV_ALIGN_CENTER, dx_in, dy_in);
            int32_t dx_out = (int32_t)(WHEEL_OUTER_R_MID * cosf(ar));
            int32_t dy_out = (int32_t)(WHEEL_OUTER_R_MID * sinf(ar));
            lv_label_set_text(s_wheel_letters_out[j], one);
            lv_obj_align(s_wheel_letters_out[j], LV_ALIGN_CENTER, dx_out, dy_out);
            lv_obj_clear_flag(s_wheel_letters_in[j], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wheel_letters_in[j], LV_OBJ_FLAG_HIDDEN);
        }
        /* The divider BEFORE slot j, at the slot boundary (bold at each
         * group start). */
        bool bold = on && (j % 3 == 0);
        int32_t len = bold ? UI_PX(30) : UI_PX(26);
        float a_b = on ? (float)j * 360.0f / (float)s_wheel_n - 90.0f : 0.0f;
        float arb = a_b * 3.14159265f / 180.0f;
        int32_t tx = (int32_t)(WHEEL_INNER_R_MID * cosf(arb));
        int32_t ty = (int32_t)(WHEEL_INNER_R_MID * sinf(arb));
        lv_obj_set_size(s_wheel_ticks[j], len, bold ? 2 : 1);
        lv_obj_set_pos(s_wheel_ticks[j], WHEEL_C + tx - len / 2, WHEEL_C + ty - (bold ? 1 : 0));
        lv_obj_set_style_transform_rotation(s_wheel_ticks[j], (int32_t)a_b, 0);
        lv_obj_set_style_bg_color(s_wheel_ticks[j], bold ? SETTINGS_GREY : lv_color_hex(0x373737),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        if (on) {
            lv_obj_clear_flag(s_wheel_ticks[j], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wheel_ticks[j], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_arc_set_angles(s_wheel_mode_arc, (lv_value_precise_t)(44 + 24 * m), (lv_value_precise_t)(64 + 24 * m));
    for (int k = 0; k < 4; k++) {
        lv_obj_set_style_text_color(s_wheel_mode_keys[k], k == m ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    settings_wheel_set_hot(-1);
}

/* The sweep press on the ring band (the inner + outer arcs share the one
 * transparent catcher). PRESSED reveals the outer frame; PRESSING tracks
 * the finger; RELEASED commits the highlighted letter when the finger is
 * still in the band (a quick tap commits its own letter; an out-of-band
 * release commits nothing). */
static void event_handler_cb_wheel_sweep(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        settings_wheel_set_revealed(true);
        int j;
        if (wheel_point_to_slot(&j)) {
            settings_wheel_set_hot(j);
        }
    } else if (code == LV_EVENT_PRESSING) {
        int j;
        if (wheel_point_to_slot(&j)) {
            settings_wheel_set_hot(j);
        }
    } else if (code == LV_EVENT_RELEASED) {
        int j;
        bool in_band = wheel_point_to_slot(&j);
        if (in_band && s_wheel_hot >= 0 && s_wheel_hot < s_wheel_n && s_wheel_pw_len < WHEEL_PW_MAX) {
            s_wheel_pw[s_wheel_pw_len++] = s_wheel_chars[s_wheel_hot];
            s_wheel_pw[s_wheel_pw_len] = 0;
            settings_wheel_pw_label();
            ESP_LOGW("settings_wheel", "entered letter %c (pw %d chars)", s_wheel_chars[s_wheel_hot], s_wheel_pw_len);
        }
        settings_wheel_set_revealed(false);
        settings_wheel_set_hot(-1);
    } else if (code == LV_EVENT_CANCEL) {
        settings_wheel_set_revealed(false);
        settings_wheel_set_hot(-1);
    }
}

/* The register/shift arc: a PRESSED on the band snaps to the nearest of
 * the four key centres (54/78/102/126 LVGL deg) and switches the mode. */
static void event_handler_cb_wheel_mode(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_obj_transform_point(objects.settings_wifi_password, &p, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE);
    float theta = atan2f((float)(p.y - WHEEL_C), (float)(p.x - WHEEL_C)) * 180.0f / 3.14159265f;
    while (theta < 0.0f) {
        theta += 360.0f;
    }
    while (theta >= 360.0f) {
        theta -= 360.0f;
    }
    int best = 0;
    float best_diff = 1e9f;
    for (int k = 0; k < 4; k++) {
        float diff = theta - (54.0f + 24.0f * k);
        while (diff > 180.0f) {
            diff -= 360.0f;
        }
        while (diff < -180.0f) {
            diff += 360.0f;
        }
        float ad = diff < 0.0f ? -diff : diff;
        if (ad < best_diff) {
            best_diff = ad;
            best = k;
        }
    }
    settings_wheel_set_mode(best);
    ESP_LOGW("settings_wheel", "mode %d", best);
}

/* The centre: short tap = commit (save the credentials + start the
 * background sync -- action_start_wifi_sync swaps to the transient WiFi
 * screen itself); long press (400 ms, repeating) = backspace. */
static void event_handler_cb_wheel_center(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (s_wheel_pw_len == 0) {
            ESP_LOGW("settings_wheel", "commit: empty password ignored");
            return;
        }
        esp_err_t r = wifi_credentials_write(s_wheel_ssid, s_wheel_pw);
        if (r != ESP_OK) {
            ESP_LOGW("settings_wheel", "write credentials -> %s", esp_err_to_name(r));
            return;
        }
        ESP_LOGW("settings_wheel", "credentials saved (SSID %zu chars, pw %d chars); starting sync",
                 strlen(s_wheel_ssid), s_wheel_pw_len);
        action_start_wifi_sync();
    } else if (code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if (s_wheel_pw_len > 0) {
            s_wheel_pw[--s_wheel_pw_len] = 0;
            settings_wheel_pw_label();
        }
    }
}

static void settings_wheel_prefill_ssid(const char *ssid) {
    size_t n = strlen(ssid);
    if (n > 32) {
        n = 32;
    }
    memcpy(s_wheel_ssid, ssid, n);
    s_wheel_ssid[n] = 0;
}

/* Entry reset (settings_screen_refresh_for): empty password, lowercase
 * mode, nothing revealed. The prefilled SSID (set by the row tap BEFORE
 * the refresh -- the screen was created once at boot, so the refresh is
 * what draws it) is kept and shown. */
static void settings_wheel_screen_refresh(void) {
    s_wheel_pw_len = 0;
    s_wheel_pw[0] = 0;
    settings_wheel_pw_label();
    settings_wheel_set_mode(0);
    settings_wheel_set_hot(-1);
    settings_wheel_set_revealed(false);
    lv_label_set_text(s_wheel_ssid_label, s_wheel_ssid[0] ? s_wheel_ssid : "SELECT A NETWORK");
}

void create_screen_settings_wifi_password() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_wifi_password = obj;
    /* NOT settings_sub_screen_prepare: its 90 px bottom exit would sit
     * under the full-360-degree ring -- the wheel gets the small in-ring
     * exit instead (see the section header). */
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The transparent press band: an arc whose hit annulus is r 140..233
     * (pad 0 -> r 233, width 93 -> inner edge 233-93 = 140) covering the
     * inner ring, the gap and the outer ring -- a press anywhere in the
     * band reveals, which is the radially-forgiving press. Invisible
     * (both arc opacities 0); the full-360-degree bg span keeps the
     * hit-test angle unbounded. ADV_HITTEST is REQUIRED: without it the
     * arc's advanced annulus hit-test never runs and the object falls
     * back to a plain 466x466 bounding-box test that would swallow every
     * press (the mode arc, created later, would then win the search). */
    s_wheel_catch = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_catch, 0, 0);
    lv_obj_set_size(s_wheel_catch, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_all(s_wheel_catch, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_catch, 0, 360);
    lv_arc_set_angles(s_wheel_catch, 0, 0);
    lv_obj_set_style_arc_width(s_wheel_catch, WHEEL_C - WHEEL_HIT_R_IN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_wheel_catch, WHEEL_C - WHEEL_HIT_R_IN, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_catch, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_catch, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_catch, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_catch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_catch, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_flag(s_wheel_catch, LV_OBJ_FLAG_ADV_HITTEST); /* run the arc's annulus hit-test */
    lv_obj_add_event_cb(s_wheel_catch, event_handler_cb_wheel_sweep, LV_EVENT_ALL, 0);

    /* The revealed outer frame: a full-360-degree band (pad 7 -> r 226,
     * width 34 -> r 192..226), dark fill, the hot sector as the white
     * low-opa indicator. Not clickable (the catch handles the presses).
     * The outer letter labels are ITS children -- hiding the arc hides
     * the whole frame in one flag. */
    s_wheel_outer = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_outer, 0, 0);
    lv_obj_set_size(s_wheel_outer, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_all(s_wheel_outer, UI_PX(7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_outer, 0, 360);
    lv_arc_set_angles(s_wheel_outer, 0, 0);
    lv_obj_set_style_arc_width(s_wheel_outer, UI_PX(34), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_wheel_outer, UI_PX(34), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_outer, lv_color_hex(0x1E1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_outer, SETTINGS_WHITE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_outer, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_outer, 0x3C, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_outer, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_outer, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_outer, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_wheel_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_outer, LV_OBJ_FLAG_HIDDEN);

    /* The 26 inner letter labels (32 px, the SSID font) at r 168. */
    for (int j = 0; j < 26; j++) {
        s_wheel_letters_in[j] = lv_label_create(obj);
        lv_obj_set_style_text_font(s_wheel_letters_in[j], &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_wheel_letters_in[j], "");
        lv_obj_set_size(s_wheel_letters_in[j], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(s_wheel_letters_in[j], LV_OBJ_FLAG_CLICKABLE);
        s_wheel_letters_out[j] = lv_label_create(s_wheel_outer);
        lv_obj_set_style_text_font(s_wheel_letters_out[j], &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_wheel_letters_out[j], "");
        lv_obj_set_size(s_wheel_letters_out[j], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(s_wheel_letters_out[j], LV_OBJ_FLAG_CLICKABLE);
    }

    /* The 26 group dividers (radial ticks at the slot boundaries; bold
     * at each group start). Created horizontal, then rotated to radial. */
    for (int j = 0; j < 26; j++) {
        s_wheel_ticks[j] = lv_obj_create(obj);
        lv_obj_set_size(s_wheel_ticks[j], UI_PX(26), 1);
        lv_obj_set_style_bg_color(s_wheel_ticks[j], lv_color_hex(0x373737), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_wheel_ticks[j], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_wheel_ticks[j], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_wheel_ticks[j], LV_OBJ_FLAG_CLICKABLE);
    }

    /* The register/shift arc: track r 88..116 (pad 117 -> r 116, width
     * 28), 42..138 LVGL deg (the mockup's 132..228, 0-up). The active
     * mode's key span is the lit indicator. Clickable (PRESSED snaps to
     * the nearest key centre). */
    s_wheel_mode_arc = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_mode_arc, 0, 0);
    lv_obj_set_size(s_wheel_mode_arc, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_all(s_wheel_mode_arc, UI_PX(117), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_mode_arc, 42, 138);
    lv_arc_set_angles(s_wheel_mode_arc, 44, 64);
    lv_obj_set_style_arc_width(s_wheel_mode_arc, UI_PX(28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_wheel_mode_arc, UI_PX(28), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_mode_arc, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_mode_arc, SETTINGS_WHITE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_mode_arc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_mode_arc, 0x2E, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_mode_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_mode_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_mode_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_mode_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_mode_arc, LV_OBJ_FLAG_ADV_HITTEST); /* its r 88..116 annulus, not the box */
    lv_obj_add_event_cb(s_wheel_mode_arc, event_handler_cb_wheel_mode, LV_EVENT_PRESSED, 0);

    /* The four mode keys (14 px = LV_FONT_DEFAULT), the key dividers,
     * and the arc's five boundary ticks at 42/66/90/114/138. */
    const char *mode_names[4] = {"abc", "ABC", "123", "#$"};
    for (int k = 0; k < 4; k++) {
        float a = (54.0f + 24.0f * k) * 3.14159265f / 180.0f;
        s_wheel_mode_keys[k] = lv_label_create(obj);
        lv_obj_set_style_text_font(s_wheel_mode_keys[k], &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_wheel_mode_keys[k], mode_names[k]);
        lv_obj_set_size(s_wheel_mode_keys[k], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(s_wheel_mode_keys[k], LV_ALIGN_CENTER, (int32_t)(WHEEL_MODE_R_MID * cosf(a)),
                     (int32_t)(WHEEL_MODE_R_MID * sinf(a)));
        lv_obj_set_style_text_color(s_wheel_mode_keys[k], SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_wheel_mode_keys[k], LV_OBJ_FLAG_CLICKABLE);
    }
    for (int k = 0; k < 5; k++) {
        float a = (42.0f + 24.0f * k) * 3.14159265f / 180.0f;
        lv_obj_t *t = lv_obj_create(obj);
        lv_obj_set_size(t, UI_PX(24), 1);
        lv_obj_set_style_bg_color(t, lv_color_hex(0x5F5F5F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(t, WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * cosf(a)) - UI_PX(12),
                       WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * sinf(a)));
        lv_obj_set_style_transform_rotation(t, (int32_t)(42.0f + 24.0f * k), 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The 12-o'clock orientation tick (the mockup's grey radial, r
     * 150..190) -- the north marker between the two frames. */
    {
        lv_obj_t *t = lv_obj_create(obj);
        lv_obj_set_size(t, UI_PX(2), UI_PX(40));
        lv_obj_set_style_bg_color(t, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(t, WHEEL_C - 1, WHEEL_C - UI_PX(190));
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The centre: a 110 px tap box (short tap = commit, long press =
     * backspace) with the password + selected-letter labels (the labels
     * are non-clickable, so the box owns the touches) and the SSID
     * caption above it (case kept -- the font now covers lowercase). */
    s_wheel_center = lv_obj_create(obj);
    lv_obj_set_size(s_wheel_center, UI_PX(110), UI_PX(110));
    lv_obj_align(s_wheel_center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_wheel_center, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wheel_center, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wheel_center, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wheel_center, event_handler_cb_wheel_center, LV_EVENT_ALL, 0);

    s_wheel_ssid_label = lv_label_create(obj);
    lv_obj_set_style_text_font(s_wheel_ssid_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_wheel_ssid_label, "");
    lv_obj_set_size(s_wheel_ssid_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_wheel_ssid_label, LV_ALIGN_CENTER, 0, -UI_PX(75));
    lv_obj_set_style_text_color(s_wheel_ssid_label, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_wheel_ssid_label, LV_OBJ_FLAG_CLICKABLE);

    s_wheel_pw_label = lv_label_create(s_wheel_center);
    lv_obj_set_style_text_font(s_wheel_pw_label, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_wheel_pw_label, "....");
    lv_obj_set_size(s_wheel_pw_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(s_wheel_pw_label, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_wheel_sel_label = lv_label_create(s_wheel_center);
    lv_obj_set_style_text_font(s_wheel_sel_label, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_wheel_sel_label, "");
    lv_obj_set_size(s_wheel_sel_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_wheel_sel_label, LV_ALIGN_CENTER, UI_PX(34), 0);
    lv_obj_set_style_text_color(s_wheel_sel_label, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_sel_label, LV_OBJ_FLAG_HIDDEN);

    /* The small in-ring exit (the mockup's 26 px circle-x at r 135): the
     * house 90 px bottom exit would sit under the ring. A plain circle +
     * a two-bar x (the 96 px FA glyph cannot scale down). */
    {
        lv_obj_t *exit = lv_obj_create(obj);
        lv_obj_set_size(exit, UI_PX(26), UI_PX(26));
        lv_obj_set_style_radius(exit, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(exit, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(exit, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(exit, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(exit, LV_ALIGN_CENTER, 0, WHEEL_EXIT_Y - WHEEL_C);
        lv_obj_add_flag(exit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(exit, event_handler_cb_settings_exit, LV_EVENT_CLICKED,
                            (void *)(intptr_t)SCREEN_ID_SETTINGS_WIFI);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *bar = lv_obj_create(exit);
            lv_obj_set_size(bar, UI_PX(12), UI_PX(2));
            lv_obj_set_style_bg_color(bar, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_transform_rotation(bar, i == 0 ? 45 : 135, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_label_set_text(s_wheel_ssid_label, s_wheel_ssid[0] ? s_wheel_ssid : "SELECT A NETWORK");
    settings_wheel_pw_label();
    settings_wheel_set_mode(0);
}

void tick_screen_settings_wifi_password() {}

/* The alarm weekday prefixes, one per clock widget. Not part of
 * objects_t: the layout contract there only names the three widget
 * slots the tick reads, and the prefix label is internal to the
 * widget (created with it, found by walking the row's children). */
static lv_obj_t *s_alarm_prefix[5];

/* Clock-widget base -> prefix slot (the objects_t bases, screens.h:
 * modem 14, wifi 19, light 24, deep 28, alarm 33). */
static int widget_prefix_slot(int startWidgetIndex) {
    switch (startWidgetIndex) {
    case 14:
        return 0;
    case 19:
        return 1;
    case 24:
        return 2;
    case 28:
        return 3;
    case 33:
        return 4;
    default:
        return -1;
    }
}

/* The clock widget (see create_user_widget_clock): full-screen flex
 * column (time on top, the alarm row bottom-centred). The alarm row
 * is a flex row holding the weekday prefix (32 px range font, grey,
 * empty within 24 h of the fire) and the HH:MM label (48 px) -- the
 * row keeps the pair centred and cross-centres the two heights. */
void create_user_widget_clock(lv_obj_t *parent_obj, int startWidgetIndex) {
    lv_obj_t *obj = parent_obj;
    {
        lv_obj_t *parent_obj = obj;
        {
            // clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            ((lv_obj_t **)&objects)[startWidgetIndex + 0] = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
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
            lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
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
                    // label-clock-alarm-row: the prefix + HH:MM pair,
                    // kept together so the weekday prefix appears to
                    // the left of the time as one centred unit.
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    const int slot = widget_prefix_slot(startWidgetIndex);
                    if (slot >= 0)
                        s_alarm_prefix[(size_t)slot] = obj;
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            // label-clock-alarm-prefix (2-letter weekday,
                            // e.g. "Mo"; empty when the fire is <= 24 h)
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_text_font(obj, &ui_font_montserrat_regular_32_range,
                                                       LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
                            /* The trailing pad is the gap to the time. */
                            lv_obj_set_style_pad_right(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    }
}

/* Refresh one alarm prefix label from the var (empty within 24 h of
 * the fire). Returns whether the text changed. */
static bool tick_alarm_prefix(lv_obj_t *row) {
    lv_obj_t *prefix = lv_obj_get_child(row, 0);
    const char *new_val = get_var_alarm_prefix();
    if (strcmp(new_val, lv_label_get_text(prefix)) == 0)
        return false;
    tick_value_change_obj = prefix;
    lv_label_set_text(prefix, new_val);
    tick_value_change_obj = NULL;
    return true;
}

#if !CONFIG_CLOCK_USE_PCF85063A
/* Unsynced-clock dim (P4): the trust flag is global but each screen carries
 * its own clock label, so the applied state is tracked per widget slot -- a
 * screen that was loaded before the trust flip still converges on its next
 * tick. (The S3's PCF clock is self-trusting; the block below is P4-only.) */
static bool s_clock_unsynced_dim[34];
#endif

void tick_user_widget_clock(int startWidgetIndex) {
    bool label_changed = false;
#if !CONFIG_CLOCK_USE_PCF85063A
    // Dim the time while the P4 clock is unsynced (no SNTP since boot and
    // no trusted manual entry) so an approximate value reads as
    // approximate; the label brightens on the sync (the flag flips in the
    // SNTP callback; this converges on the next tick of each screen).
    {
        bool dim = !clock_sntp_time_trusted();
        if (dim != s_clock_unsynced_dim[(size_t)startWidgetIndex]) {
            s_clock_unsynced_dim[(size_t)startWidgetIndex] = dim;
            lv_obj_set_style_opa(((lv_obj_t **)&objects)[startWidgetIndex + 1], dim ? LV_OPA_50 : LV_OPA_COVER, 0);
        }
    }
#endif
    {
        const char *new_val = get_var_clock_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 1]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 1];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 1], new_val);
            tick_value_change_obj = NULL;
            label_changed = true;
        }
    }
    {
        const char *new_val = get_var_alarm_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 2]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 2];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 2], new_val);
            tick_value_change_obj = NULL;
            label_changed = true;
        }
    }
    {
        const int slot = widget_prefix_slot(startWidgetIndex);
        if (slot >= 0 && tick_alarm_prefix(s_alarm_prefix[(size_t)slot]))
            label_changed = true;
    }
    if (label_changed)
        /* The spin's UI keep-mask protects LVGL's text by pixel: the
         * new digits live at pixels the old mask does not cover (and
         * the old ones at pixels it still covers), so the mask must
         * follow the label (LVGL task here, as the snapshot needs). */
        moon_ui_rebuild_ui_mask();
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
    tick_screen_settings_wifi_password,
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
    create_screen_settings_wifi_password();
}
