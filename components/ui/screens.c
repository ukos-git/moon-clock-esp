#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "alarm_audio.h"
#include "clock_alarm_scheduler.h"
#include "clock_idle.h"
#include "clock_sntp.h"
#include "clock_tz.h"
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

/* From clock.h (the manual time-entry commit). The full header is not
 * included: it pulls the bsp/driver headers the host sim (the real
 * screens.c, unmodified) cannot resolve -- only these two prototypes
 * are needed (clock_time_t comes via clock_sntp.h -> clock_types.h).
 * clock_get_time is the shared clock widget's attach refresh
 * (ui_clock_attach_to: a screen switch shows the current minute). */
esp_err_t clock_set_time(const clock_time_t *time);
esp_err_t clock_get_time(clock_time_t *out);

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
 * moon image (s_images[] stays at the four moon
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
#define GLYPH_CLOCK "\xEF\x80\x97"     /* U+F017 (manual time entry; the fonts_p4 regular set only) */
#define GLYPH_GLOBE "\xEF\x82\xAC"     /* U+F0AC (timezone entry; the solid set -- both boards) */
#define GLYPH_SPEAKER "\xEF\x80\xA8"   /* U+F028 (alarm-sound entry; the solid set -- both boards) */
#define GLYPH_ROTATE "\xEF\x8B\xB1"    /* U+F2F1 (display base-orientation entry; the solid set -- all three boards) */

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
static void settings_time_screen_refresh(void);
static void settings_tz_screen_refresh(void);
static void settings_alarm_sound_screen_refresh(void);
static void settings_display_screen_refresh(void);
/* settings_screen_refresh_for is non-static (declared in screens.h; the
 * TEST_WIFI_SCAN_SEC hook calls it directly). */

/* The wheel keyboard's prefilled SSID (the AP-row tap sets it before the
 * screen loads; the wheel section owns the rest of its state). */
static char s_wheel_ssid[33];

/* One icon of the hub's ring: a clickable 96 px FA glyph. The target
 * screen rides in the event's user_data (one handler for the whole
 * ring -- 7 entries on the S3 and the knob, 8 on the P4). */
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
    /* The glyph's label line-height exceeds the box (e.g. ~200 px at the
     * 148 px P4 font in the 148 px box): LVGL's default SCROLLABLE flag
     * would draw a vertical scrollbar at the box's right edge (the thin
     * line the user saw crossing the circle). The box never scrolls. */
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
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
            /* 18 = objects.modem_sleep_clock__clock (the widget
             * container) -- objects_t shifted +1 when settings_alarm_sound
             * joined the screen roots and +1 again when settings_display
             * joined; 17 is the <screen>_clock parent. */
            create_user_widget_clock(obj, 18);
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
    /* No clock tick here: the ONE shared clock widget is ticked from the
     * vars by the minute tick (action_update_clock_display) and by the
     * screen switch (ui_clock_attach_to); only the seconds arc is
     * screen-local. */
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
            /* wifi_clock: the shared clock widget's PARENT on this
             * screen (layout contract 2) -- the widget itself is
             * created once, on the modem-sleep screen, and
             * ui_clock_attach_to() reparents it here at the switch.
             * Only the container is created; the alias rows
             * (wifi_clock__clock & co = objects[18/19/20]) are
             * assigned in create_screen_modem_sleep. */
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
    /* No clock tick: the shared widget is ticked from the vars (minute
     * tick + screen switch); only the seconds arc is screen-local. */
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
        }
    }

    tick_screen_light_sleep();
}

/* No clock tick: the shared widget is ticked from the vars by the
 * minute tick and by ui_clock_attach_to (layout contract 2). */
void tick_screen_light_sleep() {}

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

void tick_screen_deep_sleep() {} /* no clock tick: the shared widget is
                                     ticked from the vars (minute tick +
                                     screen switch). */

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

void tick_screen_alarm() {} /* no clock tick: the shared widget is
                             * ticked from the minute tick regardless of
                             * the active screen */

/* ---------------- settings screens ----------------
 *
 * The settings hub is a moon-less screen (settings_screen_prepare):
 * black background, a central circle-xmark exit (the fa-circle-xmark
 * glyph alone), and a ring of sub-screen icons around it. The
 * sub-screens are moon-less too, with the shared bottom-centre exit
 * back to the CALLING level (the hub, or the alarm screen for its two
 * sub-screens) plus their own working widgets. */

/* Demo-mode walk registries (the tour taps the real touch targets;
 * docs/verification/demo-mode). All screens are created once at boot,
 * so these fill exactly once and the walk reads pointers, never coords.
 * The sub-screen exit map is keyed by the screen ROOT (the pair the
 * sub-screen shell registered); the hub's own central exit is the
 * named objects.settings_hub_exit. */
static lv_obj_t *s_hub_icons[8];
static enum ScreensEnum s_hub_icon_targets[8];
static int s_hub_icons_n = 0;

struct sub_exit {
    lv_obj_t *root;
    lv_obj_t *exit;
};
static struct sub_exit s_sub_exits[12];
static int s_sub_exits_n = 0;

/* The bottom-centre exit of the sub-screen whose ROOT is `root`
 * (NULL when the screen did not build a sub-screen shell). */
lv_obj_t *screens_sub_exit_get(lv_obj_t *root) {
    if (!root) {
        return NULL;
    }
    for (int i = 0; i < s_sub_exits_n; i++) {
        if (s_sub_exits[i].root == root) {
            return s_sub_exits[i].exit;
        }
    }
    return NULL;
}

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

    /* Icon ring: EQUAL ARC DISTANCES -- the icons sit 360/n degrees apart
     * on the ring (S3: 7 icons, ~51.4 deg; P4: 8 icons, 45 deg), the
     * deep-sleep icon anchored at the bottom (180 deg). On the P4 the
     * ring carries the clock (manual time entry, U+F017 -- the P4
     * fonts_p4 regular set only) first. Moon weight distinguishes the
     * two sleep screens (regular / solid); the globe (timezone, U+F0AC,
     * solid), the speaker (alarm sound, U+F028, solid) and the rotate
     * arrow (display trim, U+F2F1, solid) are on all three boards. */
    struct hub_icon {
        const char *glyph;
        const lv_font_t *font;
        enum ScreensEnum target;
    };
    static const struct hub_icon ring[] = {
#if defined(CONFIG_BOARD_P4)
        {GLYPH_CLOCK, &ui_font_fontawesome7_regular_96_icons, SCREEN_ID_SETTINGS_TIME},
#endif
        {GLYPH_ALARM, &ui_font_fontawesome7_regular_96_icons, SCREEN_ID_SETTINGS_ALARM},
        {GLYPH_MOON, &ui_font_fontawesome7_regular_96_icons, SCREEN_ID_SETTINGS_LIGHT_SLEEP},
        {GLYPH_MOON, &ui_font_fontawesome7_solid_96_icons, SCREEN_ID_SETTINGS_DEEP_SLEEP},
        {GLYPH_WIFI, &ui_font_fontawesome7_solid_96_icons, SCREEN_ID_SETTINGS_WIFI},
        {GLYPH_GLOBE, &ui_font_fontawesome7_solid_96_icons, SCREEN_ID_SETTINGS_TZ},
        {GLYPH_SPEAKER, &ui_font_fontawesome7_solid_96_icons, SCREEN_ID_SETTINGS_ALARM_SOUND},
        {GLYPH_ROTATE, &ui_font_fontawesome7_solid_96_icons, SCREEN_ID_SETTINGS_DISPLAY},
    };
    /* Ring radius, balanced against the central exit disk (96 px ink at
     * the 96 px font, 148 px on the P4's scaled icon font): at 140 (466)
     * the icons' inner edge sits 44 px off the disk's rim and their outer
     * edge 45 px off the panel rim (233 = 466/2) -- equal breathing room
     * both sides (117 huddled the icons against the disk with ~68 px of
     * free margin outside; the 2026-11-04 "stacked icons" report). At
     * 720 the same UI_PX ratio gives R = 216 (inner 68 / outer 70). The
     * 7-icon S3 ring's chord at 140 is 121 px > the 96 px icon width, so
     * the icons never crowd each other. */
    const int32_t R = UI_PX(140);
    const size_t n = sizeof(ring) / sizeof(ring[0]);
    const float step = 360.0f / (float)n;
    /* The deep-sleep entry's index: the icon anchored at the bottom
     * (180 deg). Found by target (not by n - 3 -- the ring's count
     * differs between the boards and the anchor must not drift). */
    float deep = 0.0f;
    for (size_t i = 0; i < n; i++) {
        if (ring[i].target == SCREEN_ID_SETTINGS_DEEP_SLEEP) {
            deep = (float)i;
        }
    }
    const float PI = 3.14159265f; /* the LVGL math macro is config-gated */
    for (size_t i = 0; i < n; i++) {
        /* theta clockwise from 12 o'clock; deep sleep at 180 deg. */
        const float th = ((180.0f + step * ((float)i - deep)) * PI) / 180.0f;
        const int32_t dx = (int32_t)(R * sinf(th));
        const int32_t dy = (int32_t)(-R * cosf(th));
        lv_obj_t *icon = settings_hub_icon(obj, ring[i].glyph, ring[i].font, dx, dy, ring[i].target);
        /* Demo-mode walk registry (screens_hub_icons): the tour taps
         * the REAL icons (hit test + CLICKED handler) instead of
         * loadScreen, so it verifies the touch entry path on camera.
         * The hub is created once at boot; 8 covers the 7-icon P4 ring
         * and the 6-icon S3 ring with headroom. */
        if (s_hub_icons_n < (int)(sizeof(s_hub_icons) / sizeof(s_hub_icons[0]))) {
            s_hub_icons[s_hub_icons_n] = icon;
            s_hub_icon_targets[s_hub_icons_n] = ring[i].target;
            s_hub_icons_n++;
        }
    }
}

/* The hub ring's created icons (demo-mode walk; see create_screen_
 * settings_hub). Returns the count, fills up to max entries. */
int screens_hub_icons(const lv_obj_t **objs, enum ScreensEnum *targets, int max) {
    int n = s_hub_icons_n;
    if (n > max) {
        n = max;
    }
    for (int i = 0; i < n; i++) {
        if (objs) {
            objs[i] = s_hub_icons[i];
        }
        if (targets) {
            targets[i] = s_hub_icon_targets[i];
        }
    }
    return n;
}

void tick_screen_settings_hub() {}

/* One sub-screen shell: black root (non-clickable) + the shared
 * bottom-centre exit button (the 96 px circle-xmark glyph) back to the
 * CALLING level (the hub, or the alarm screen for its two
 * sub-screens). The sub-screens add their own widgets on top; the band
 * around the exit (x 185..281, y 336..432) stays free of widgets. */
/* Returns the created exit button so a caller can add its own handler
 * on it (the time entry commits the value when the X is pressed). */
static lv_obj_t *settings_sub_screen_prepare(lv_obj_t *obj, enum ScreensEnum exit_target) {
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *exit = settings_create_exit_button(obj, UI_PX(90));
    lv_obj_align(exit, LV_ALIGN_BOTTOM_MID, 0, -UI_PX(34));
    lv_obj_add_event_cb(exit, event_handler_cb_settings_exit, LV_EVENT_CLICKED, (void *)(intptr_t)exit_target);
    /* Demo-mode walk registry (screens_sub_exit_get). */
    if (s_sub_exits_n < (int)(sizeof(s_sub_exits) / sizeof(s_sub_exits[0]))) {
        s_sub_exits[s_sub_exits_n].root = obj;
        s_sub_exits[s_sub_exits_n].exit = exit;
        s_sub_exits_n++;
    }
    return exit;
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
    case SCREEN_ID_SETTINGS_TIME:
        settings_time_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_TZ:
        settings_tz_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_ALARM_SOUND:
        settings_alarm_sound_screen_refresh();
        break;
    case SCREEN_ID_SETTINGS_DISPLAY:
        settings_display_screen_refresh();
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

static void settings_pomodoro_save_value(void) {
    int total = 60 * settings_roller_value(objects.settings_pomodoro_roller_h) +
                settings_roller_value(objects.settings_pomodoro_roller_m);
    if (total < 1) {
        ESP_LOGW("settings_pomodoro", "interval 0 rejected (keeping the current rule)");
        return;
    }
    esp_err_t r = clock_alarm_set_pomodoro_interval(total);
    ESP_LOGW("settings_pomodoro", "interval %d min -> %s", total, esp_err_to_name(r));
}

static void event_handler_cb_settings_pomodoro_released(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    settings_pomodoro_save_value();
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
 * styling never applied). font + height are the caller's: the 96 px
 * numbers font at 110 px for the time/alarm values, the 48 px numbers
 * font at 70 px for the manual time-entry's date row. width sizes to
 * the value: 120 for the 2-digit fields, 90 for the 48 px ones. */
static lv_obj_t *settings_create_roller(lv_obj_t *parent, const char *options, const lv_font_t *font, int32_t height,
                                        int32_t x, int32_t width) {
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 1);
    lv_obj_set_width(r, width);
    lv_obj_set_height(r, height);
    lv_obj_align(r, LV_ALIGN_CENTER, x, UI_PX(90));
    lv_obj_set_style_text_font(r, font, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    /* The 140 px box (was 120): the Montserrat digits are PROPORTIONAL
     * (not tabular) -- the worst-case "44" pair advances 132 px at the
     * 96 px font / 204 px at 148, which the old box clipped flat at
     * its right edge. 140 leaves >= 4 px margin for the full pair. */
    objects.settings_pomodoro_roller_h =
        settings_create_roller(obj, hours, &ui_font_montserrat_bold_96_numbers, UI_PX(110), -UI_PX(80), UI_PX(140));
    lv_obj_align(objects.settings_pomodoro_roller_h, LV_ALIGN_CENTER, -UI_PX(80), -UI_PX(23));
    objects.settings_pomodoro_roller_m =
        settings_create_roller(obj, minutes, &ui_font_montserrat_bold_96_numbers, UI_PX(110), UI_PX(70), UI_PX(140));
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

/* The ring is pushed right to the outer edge, the day-range as the
 * moon-colour indicator over a dark track. The day letters that used
 * to crowd the outer margin are gone (the range summary "Mo-Fr" above
 * the time carries the day meaning), so the ring can own that outer
 * band. It shares the home seconds arc's thin-grey visual language
 * but is NOT the same ring: the home arc is a 466x466 object with pad
 * UI_PX(10), whose get_center radius (w - pads)/2 = 223 is the
 * stroke's OUTER edge -- lv_draw_sw_arc bands INWARD from it, so the
 * theme's 12 px stroke inks 212..223, 10 px off the rim. This band
 * zeroes all four pads, so get_center returns the panel half itself
 * and the UI_PX(10) stroke inks straight to the round rim (224..233
 * at 466; probed rows 0..9 in the committed screenshots). */
#define ALARM_TIME_ARC_CX (UI_PANEL_PX / 2)
#define ALARM_TIME_ARC_CY (UI_PANEL_PX / 2)
#define ALARM_TIME_ARC_WIDTH UI_PX(10) /* stroke width, inked INWARD from panel/2 */
/* The handles sit centred 10 px under the band midline (228.5 at 466):
 * the 34 px disc spans 201..235, poking through both ink edges. */
#define ALARM_TIME_HANDLE_R UI_PX(218) /* the day handles' centre radius */

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
static void settings_alarm_time_save_value(void) {
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

/* Persist on a roller RELEASED (the SPIFFS write + RTC reschedule stay
 * off the drag path). */
static void event_handler_cb_settings_alarm_time_save(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    settings_alarm_time_save_value();
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
     * right), a thin ring RIGHT AT the rim (a 466x466 object at (0,0)
     * with zeroed pads -> get_center radius panel/2, the UI_PX(10)
     * stroke inked INWARD: 224..233 at 466 -- rows 0..9 of the
     * committed screenshots; unlike the home seconds arc, which the
     * pad insets to outer 223; it clears the exit button by a mile).
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
     * band's hole (its inner ink edge sits at r 224 -> y 9 at the
     * top) and clear of the exit band (settings_create_exit_button
     * floors the UI_PX(90) call at a 96 px box: top edge y 336). */
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
    /* The 140 px box (was 120): the Montserrat digits are PROPORTIONAL
     * (not tabular) -- the worst-case "44" pair advances 132 px at the
     * 96 px font / 204 px at 148, which the old box clipped flat at
     * its right edge (the minutes roller sits closest to the panel's
     * right side, so the clip showed up there). 140 leaves >= 4 px
     * margin for the full pair. */
    objects.settings_alarm_time_roller_h =
        settings_create_roller(obj, hours, &ui_font_montserrat_bold_96_numbers, UI_PX(110), -UI_PX(80), UI_PX(140));
    lv_obj_align(objects.settings_alarm_time_roller_h, LV_ALIGN_CENTER, -UI_PX(80), -UI_PX(23));
    objects.settings_alarm_time_roller_m =
        settings_create_roller(obj, minutes, &ui_font_montserrat_bold_96_numbers, UI_PX(110), UI_PX(70), UI_PX(140));
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
 * deep), also in minutes. One centred roller: the icon-only dusk
 * animation toggle left the screen 2026-11-13 with the dusk itself
 * (the store's dusk_anim_enable -- and the boot sweep's
 * boot_anim_enable -- stay as legacy no-ops, unexposed). The
 * countdown takes effect at the next sleep entry; the idle
 * timeout is applied live via clock_idle_set_timeout_ms().
 *
 * The widgets are created once (loadScreen never destroys them), so
 * the pointers live here; [0] = light-sleep screen, [1] = deep. */
static lv_obj_t *s_sleep_roller[2];

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
     * default 1 min): the value picker is one centred roller (the dusk
     * toggle that shared this line retired 2026-11-13 with the dusk). */
    char options[512];
    int pos = 0;
    for (int v = 1; v <= 30; v++) {
        pos += snprintf(options + pos, sizeof(options) - pos, "%d\n", v);
    }
    s_sleep_roller[screen] =
        settings_create_roller(obj, options, &ui_font_montserrat_bold_96_numbers, UI_PX(110), 0, UI_PX(210));
    lv_obj_align(s_sleep_roller[screen], LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_sleep_roller[screen], event_handler_cb_settings_sleep_roller_released, LV_EVENT_RELEASED,
                        (void *)(intptr_t)screen);

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
 * list renders when the scan finishes (the 10 ms tick polls
 * wifi_scan_in_progress() and renders once). While the scan runs (and
 * when it found nothing) a centred placeholder LABEL carries the line —
 * not the roller: a one-option NORMAL-mode list clamps its scroll at 0
 * and pins that line to the TOP of the 3/4-row window, which read as
 * "SCANNING is not centred" (user 2026-11-11). Row text is font-safe:
 * the SSID font covers only space, '-', ':', digits 0-9 and UPPERCASE
 * A-Z (lowercase dBm would render as missing-glyph boxes), so the
 * strength is shown as "DBM". */
/* The family's Wi-Fi list is ONE lv_roller (user 2026-11-10 asked for it
 * on the knob -- "the label rows read too small at 360 px" -- and
 * 2026-11-11 for all three boards: one Wi-Fi selection UI family-wide).
 * Entries use the 48-baseline SSID face per tier (48 px on the S3's
 * 466, 74 px on the P4's 720, 37 px on the knob's 360); the cursor
 * language is unchanged (window centre = cursor, SELECTED entry white,
 * no highlight box). s_wifi_selected mirrors the roller's selection. */
static lv_obj_t *s_wifi_roller;
/* SCANNING / NO APS FOUND placeholder: a centred label, not a roller
 * option (user 2026-11-11 on S3 + P4: "scanning is not centered" —
 * LVGL clamps a one-option NORMAL roller at scroll 0, so the single
 * row sits at the window top; the label centres both ways). */
static lv_obj_t *s_wifi_placeholder;
static bool s_wifi_scan_rendered = true;   /* the results are on the roller */
static bool s_wifi_scanning_shown = false; /* the SCANNING placeholder is up */
static int s_wifi_selected = -1;

/* Roller entry: the rows' shape, but the SSID is truncated to 14 chars
 * with a '+' marker -- a full 20-char uppercase name at 37 px is ~500 px
 * and the round panel only affords a ~300 px chord at the list's band.
 * The RAW SSID still reaches the keyboard (prefill), so a truncated
 * display entry stays connectable. */
static void settings_wifi_fmt_roller_entry(char *buf, size_t sz, const wifi_ap_info_t *ap) {
    size_t len = 0;
    while (len < sizeof(ap->ssid) - 1 && ap->ssid[len] != 0) {
        len++;
    }
    const bool trunc = len > 14;
    const size_t keep = trunc ? 14 : len;
    int pos = 0;
    for (size_t c = 0; c < keep; c++) {
        char ch = ap->ssid[c];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        buf[pos++] = ch;
    }
    if (trunc) {
        buf[pos++] = '+';
    }
    buf[pos++] = 0;
    /* Uppercase "DBM": the SSID font lacks lowercase (see above). */
    snprintf(buf + pos, sz - (size_t)pos, " %dDBM", (int)ap->rssi);
}

static void settings_wifi_render_rows(void) {
    wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
    if (!s_wifi_roller) {
        return;
    }
    static char opts[WIFI_SCAN_MAX_APS * 32];
    if (n <= 0) {
        /* "NO APS FOUND" rides the centred placeholder too (same reason
         * as SCANNING) — the roller steps aside entirely, so it only
         * ever holds real entries. */
        if (s_wifi_placeholder) {
            lv_label_set_text(s_wifi_placeholder, "NO APS FOUND");
            lv_obj_set_hidden(s_wifi_placeholder, false);
        }
        lv_obj_set_hidden(s_wifi_roller, true);
        return;
    }
    size_t used = 0;
    for (int i = 0; i < n && used < sizeof(opts) - 32; i++) {
        char line[32];
        settings_wifi_fmt_roller_entry(line, sizeof(line), &aps[i]);
        const int w = snprintf(opts + used, sizeof(opts) - used, "%s%s", (used != 0) ? "\n" : "", line);
        if (w < 0 || (size_t)w >= sizeof(opts) - used) {
            break;
        }
        used += (size_t)w;
    }
    lv_roller_set_options(s_wifi_roller, opts, LV_ROLLER_MODE_NORMAL);
    if (s_wifi_selected >= 0 && s_wifi_selected < n) {
        lv_roller_set_selected(s_wifi_roller, (uint32_t)s_wifi_selected, LV_ANIM_OFF);
    } else {
        lv_roller_set_selected(s_wifi_roller, 0, LV_ANIM_OFF);
    }
    lv_obj_set_hidden(s_wifi_roller, false);
    if (s_wifi_placeholder) {
        lv_obj_set_hidden(s_wifi_placeholder, true);
    }
}

/* Roller commit: a tap on the roller -- or the dial commit, which
 * re-sends CLICKED from the dial task's wrapper -- opens the wheel
 * keyboard for the entry in the window's middle. The roller's own
 * selection IS the cursor (the dial wrote it); s_wifi_selected mirrors
 * it for screens_wifi_row_get/refresh parity. */
static void event_handler_cb_wifi_roller(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!s_wifi_roller) {
        return;
    }
    wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
    const int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
    const int sel = (int)lv_roller_get_selected(s_wifi_roller);
    if (n == 0 || sel < 0 || sel >= n) {
        return; /* "SCANNING"/"NO APS FOUND" placeholders are not selectable */
    }
    s_wifi_selected = sel;
    settings_wheel_prefill_ssid(aps[sel].ssid);
    loadScreen(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
    ui_tick();
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
    ESP_LOGW("settings_wifi", "roller AP %d -> wheel keyboard (SSID %zu chars)", sel, strlen(s_wheel_ssid));
}

/* Panel traffic hold across the WiFi scan's radio session (task #30,
 * the 2026-11-12 "blank-white clock row after leaving the WiFi password
 * screen" on the S3): the scan's esp_wifi_init firmware-load + the active
 * scan itself steal QSPI DMA completions from live panel traffic -- the
 * bench repro logged 400 co5300 write failures and TWO silently DROPPED
 * strips right over the clock row (y 176..214, y 214..252) in ~3 s. The
 * drop is permanent: LVGL believes the flush landed (flush_cb answers
 * ready even for held writes), nothing re-invalidates those strips, and
 * nothing else repaints them -- the clock
 * row keeps showing whatever the last full paint left there (the user's
 * "blank white, no digits"). Same completion-thief class as the wake-sync
 * storm (task #29) -- which is why that had a traffic hold and this never
 * did.
 *
 * The hold follows the SCAN, not the screen: leaving mid-scan must NOT
 * cut the guard (the user's path -- password -> list re-entry restarts
 * the scan -> exit home lands the home switch's atomic flush inside the
 * running storm). arm() runs before wifi_scan_begin on entry; the
 * GENERIC ui tick (actions.c) polls scan-completion and releases with
 * the sync path's proven 4 s teardown tail, so the first real flush
 * after release lands on a quiet bus and the release's whole-screen
 * repaint heals everything the storm ate, wherever the user went. The
 * 45 s cap covers the 0-AP four-retry worst case (and a refcount double
 * arm from list re-entry while an older hold is still live). */
#define SCAN_PANEL_HOLD_CAP_MS 45000
#define SCAN_PANEL_HOLD_TAIL_MS 4000 /* esp_wifi_stop/deinit churn (task #29 bench: 0.6-3.5 s) */
static bool s_scan_panel_hold;

static void scan_panel_hold_arm(void) {
    if (s_scan_panel_hold)
        return;
    s_scan_panel_hold = true;
    moon_ui_panel_traffic_hold_ext(true, SCAN_PANEL_HOLD_CAP_MS, 0);
}

void screens_wifi_scan_hold_poll(void) {
    if (!s_scan_panel_hold)
        return;
    if (wifi_scan_in_progress())
        return; /* the radio session still runs -- hold stays */
    s_scan_panel_hold = false;
    moon_ui_panel_traffic_hold_ext(false, 0, SCAN_PANEL_HOLD_TAIL_MS);
}

static void settings_wifi_screen_refresh(void) {
    s_wifi_selected = -1;
    s_wifi_scan_rendered = false;
    s_wifi_scanning_shown = false;
    scan_panel_hold_arm(); /* BEFORE the kickoff: the storm starts when
                            * the scan task reaches esp_wifi_init */
    wifi_scan_begin();
}

/* The AP list object for the demo-mode walk (the tour taps the real
 * roller: hit test -> CLICKED -> wheel keyboard with the SSID prefilled
 * for the window-centre entry). The roller exists for the whole boot;
 * only its OPTIONS follow the scan, so any valid index returns the same
 * tappable object. */
lv_obj_t *screens_wifi_row_get(int i) {
    if (i < 0 || i >= WIFI_SCAN_MAX_APS) {
        return NULL;
    }
    return s_wifi_roller;
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

    /* The family's list: one lv_roller (user 2026-11-10: "make the
     * network selection choice a lv_roller ... Roll through rollers via
     * knob spin"; 2026-11-11: all three boards, one Wi-Fi UI). NORMAL
     * mode -- the list is finite, the window centre is the cursor; the
     * dial steps it on the knob (screens_dial_step), a tap -- or the
     * dial commit -- opens the keyboard. The entry font is the tier's
     * 48-baseline SSID face (37 px knob / 48 px S3 / 74 px P4). Visible
     * rows: the knob's 37 px entries fill 4 in the band under the
     * title; the clocks' bigger face fits 3 between title and the
     * bottom-centre exit button. */
    s_wifi_roller = lv_roller_create(obj);
    /* Font BEFORE row count -- lv_roller_set_visible_row_count sizes the
     * roller's height from the CURRENT MAIN font's line height
     * (lv_roller.c:241); with the fonts set after, the height froze at
     * the default font's and the window clipped to ~1.5 rows (uisim
     * render 2026-11-11). All other styles ride here too for the same
     * reason; border width feeds the row-count height. */
    lv_obj_set_style_text_font(s_wifi_roller, &ui_font_montserrat_regular_48_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_wifi_roller, &ui_font_montserrat_regular_48_ssid, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wifi_roller, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
#ifdef CONFIG_BOARD_KNOB
    lv_obj_set_size(s_wifi_roller, UI_PX(300), LV_SIZE_CONTENT);
#else
    /* The clocks: as wide as the round panel's chord affords at the
     * list's band (~442 px at the S3's 466; the P4's UI_PX scales the
     * same numbers onto 720) -- the 48-baseline face needs it, the
     * knob's 300 (its 37 px entries) stays put. */
    lv_obj_set_size(s_wifi_roller, UI_PX(430), LV_SIZE_CONTENT);
#endif
    /* The roller only ever holds real entries — while scanning (or with
     * an empty result) it is hidden and the centred placeholder label
     * below carries the line. */
    lv_roller_set_options(s_wifi_roller, "", LV_ROLLER_MODE_NORMAL);
    lv_obj_align(s_wifi_roller, LV_ALIGN_CENTER, 0, UI_PX(18));
    /* CENTRED entries (user 2026-11-11, twice: "the selection roller is
     * also ragged left, not centered as i want it" — the LEFT-align
     * call of the family-wide port is reversed: a centred cursor column
     * wins over tail-only clipping). A centre-aligned entry wider than
     * the roller clips both ends, but entries are already truncated to
     * 14 chars + '+' and the SSID stays recognisable; the RAW SSID
     * still reaches the keyboard (prefill). */
    lv_obj_set_style_text_align(s_wifi_roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_wifi_roller, LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wifi_roller, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* The family's cursor language: the SELECTED entry goes white
     * (SETTINGS_WHITE), no highlight box -- same as the clock rows +
     * sel bar, so the roller keeps the screen's look. */
    lv_obj_set_style_text_color(s_wifi_roller, SETTINGS_WHITE, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_roller, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_roller, LV_OPA_TRANSP, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wifi_roller, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wifi_roller, 0, LV_PART_SELECTED | LV_STATE_DEFAULT);
#ifdef CONFIG_BOARD_KNOB
    const uint32_t wifi_rows_visible = 4;
#else
    const uint32_t wifi_rows_visible = 3;
#endif
    lv_roller_set_visible_row_count(s_wifi_roller, wifi_rows_visible);
    lv_obj_add_event_cb(s_wifi_roller, event_handler_cb_wifi_roller, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_wifi_roller, LV_OBJ_FLAG_HIDDEN); /* the placeholder owns the band until a scan lands */

    /* The centred placeholder (SCANNING / NO APS FOUND) — same tier
     * face, same band as the roller, centred both ways. Hidden while
     * real entries are up (settings_wifi_render_rows toggles the pair). */
    s_wifi_placeholder = lv_label_create(obj);
    lv_obj_set_style_text_font(s_wifi_placeholder, &ui_font_montserrat_regular_48_ssid,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_wifi_placeholder, "SCANNING");
    lv_obj_set_size(s_wifi_placeholder, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_wifi_placeholder, LV_ALIGN_CENTER, 0, UI_PX(18));
    lv_obj_set_style_text_color(s_wifi_placeholder, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_wifi_placeholder, LV_OBJ_FLAG_CLICKABLE);
}

void tick_screen_settings_wifi() {
    if (wifi_scan_in_progress()) {
        /* Show a "SCANNING" placeholder while the scan runs (set once --
         * a per-tick set_text would invalidate every 10 ms even though
         * the text never changes). */
        if (!s_wifi_scanning_shown) {
            if (s_wifi_roller) {
                lv_roller_set_options(s_wifi_roller, "", LV_ROLLER_MODE_NORMAL);
                lv_obj_set_hidden(s_wifi_roller, true);
            }
            if (s_wifi_placeholder) {
                lv_label_set_text(s_wifi_placeholder, "SCANNING");
                lv_obj_set_hidden(s_wifi_placeholder, false);
            }
            s_wifi_scanning_shown = true;
        }
        return;
    }
    /* Scan session done. The panel traffic hold is NOT released here --
     * the generic ui tick's screens_wifi_scan_hold_poll owns the release
     * (task #30: it must follow the scan, not this screen, so leaving
     * mid-scan cannot cut the guard). The row render below may land
     * inside the release's tail (held, dropped) -- the release's
     * whole-screen repaint heals it. */
    if (s_wifi_scan_rendered) {
        return;
    }
    settings_wifi_render_rows();
    s_wifi_scan_rendered = true;
    s_wifi_scanning_shown = false;
}

/* --- WiFi password: the circular wheel keyboard (v2, 12-sector hybrid) --
 *
 * Reached by tapping an AP row on the WiFi screen (the SSID is prefilled,
 * case kept). The v2 hybrid (doc 7.14; the operable reference is the lab's
 * panel #1, docs/keyboard/round-keyboard-lab.html) re-derives the v1
 * geometry (7.13) from the research's findings:
 *
 *  - PROPORTIONAL SECTORS. The 26 slots of the active mode tile the ring
 *    with EQUAL shares (SHARE = 360/26; every letter owns one share); the
 *    12 frequency-grouped sectors [ab][cd][ef][gh][il][jkq][mn][op][rs][tu]
 *    [vxz][wy] span n*SHARE (2-letter 27.7 deg, the two 3-letter sectors
 *    41.5 deg). The grouping is FIXED across all four registers -- the
 *    sector boundaries never move when the register changes (the muscle
 *    memory of the press position survives the abc -> 123 switch).
 *  - AT REST: the 26 letters of the active mode at r 168, the 12 hairline
 *    sector dividers at r 150..226, band hairlines at r 150 / 226.
 *  - THE REVEAL: on touch-down in the ring band only the PRESSED sector's
 *    boxes appear -- one thick (76 px) non-rounded arc box per letter,
 *    r 150..226 (the inner circle's own radius band), tiling the sector
 *    edge-to-edge. The lit box inverts (white fill, near-black letter).
 *    The reveal is STICKY to the pressed sector (sliding across a sector
 *    boundary does not re-key the set).
 *  - HIT-TEST: the exact box under the finger -- the finger's angle names
 *    the sector, the offset-in-sector names the box (floor / SHARE). No
 *    nearest-centre tie-break (v1's 2x overlap is gone; the boxes tile
 *    the ring exactly).
 *  - CENTRE PREVIEW (DTMF-style): while pressed the lit letter is shown
 *    in the centre + "release commits"; on release the character commits
 *    and the full running entry re-appears (last 6 + ellipsis).
 *  - REGISTER ARC: four 60-degree keys (abc / ABC / 123 / #\$) on the top
 *    240-degree arc (LVGL 150..390), r 84..144; a press is resolved by
 *    exact 60-degree sector, not nearest key.
 *  - COMMIT / DISCARD: the bottom 120-degree arc is the house circle-
 *    CHECK COMMIT key (LVGL ~31.5..88.5) and the house circle-xmark
 *    DISCARD key (LVGL ~91.5..148.5), both at r 114 (icons are built
 *    from rotated bars -- 2026-11-08: X means DISCARD, check means
 *    accept; the earlier xmark-on-commit / trash-on-discard pairing is
 *    gone). A plain tap on the centre
 *    BACKSPACES; a 400 ms LONG PRESS on the centre is the ONLY way back
 *    to the WiFi list (v2 has no in-ring exit -- the xmark glyph is the
 *    discard key now, so settings_sub_screen_prepare is deliberately
 *    skipped).
 *    The centre box (r ~55) never overlaps the sweep band (r 140..233).
 *
 * All presses commit on RELEASE only (the reveal + preview show exactly
 * what the release will commit, so a double-entry is impossible). Angles
 * are LVGL's (0 deg = right, 90 deg = down, clockwise); the mockup's
 * 0 deg = up is a constant -90 offset. The wheel is transform-aware (the
 * inverse-transform idiom, alarm_time_point_to_day) so it stays correct
 * under the 90-degree orientation roll.
 *
 * LVGL mechanics (both panels, S3 466 + P4 720 via UI_PX):
 *  - a FILLED annular sector = a thick NON-ROUNDED lv_arc drawn as its
 *    TRACK (LV_PART_MAIN, the bg angles): the SW arc renderer fills it via
 *    an angle mask whose delta path covers a span that crosses the 360/0
 *    line (lv_arc_set_angles normalises the start into 0..360 and the
 *    renderer's end < start branch draws the wrap). The VALUE part
 *    (LV_PART_INDICATOR, the arc angles) is left empty (0,0) on a plain
 *    box.
 *  - the 240-degree register track + the active mode's 60-degree key are
 *    ONE arc: the track is the MAIN (bg 150..390), the lit key is the
 *    INDICATOR (arc 150+60m .. 210+60m). Styling ONLY the MAIN leaves the
 *    indicator at the theme default (a vivid blue) -- both parts need a
 *    colour.
 *  - lv_line is NOT built on the device (CONFIG_LV_USE_LINE off), so the
 *    hairline dividers and icons are thin lv_obj rectangles; the radial
 *    ones rotate with transform_rotation -- 0.1-degrees units (pass
 *    deg*10; v1 passed raw degrees and its ticks under-rotated 10x, a
 *    latent bug this rewrite fixes). LVGL's transform pivot defaults to
 *    the object's TOP-LEFT corner, so every rotated bar (sector dividers,
 *    box dividers, the mode-arc ticks, the commit key's X-bars) sets an
 *    explicit LV_PCT(50) pivot or its rotation would swing it on a long
 *    arc instead of spinning in place.
 *  - the clickable arcs (catch, mode arc, commit/discard) carry
 *    LV_OBJ_FLAG_ADV_HITTEST -- without it lv_arc falls back to a plain
 *    bounding-box test and the last-created arc wins the search.
 *
 * All wheel widgets are LOCAL statics (the layout contract in screens.h
 * only names the screen roots and the clock-widget slots). The screen is
 * created once at boot; every widget is pre-built once and the press only
 * toggles text / colours / angles (region-limited invalidation -- each
 * frame still lands as the one atomic whole-panel lv_refr_now under
 * s_rgb_mtx, never a per-widget flush). */
#define WHEEL_C (UI_PANEL_PX / 2)    /* the panel centre */
#define WHEEL_INNER_R_MID UI_PX(168) /* at-rest letter row */
#define WHEEL_OUTER_R_MID UI_PX(209) /* reveal letter row */
#define WHEEL_HIT_R_IN UI_PX(140)    /* the forgiving press band: 140..233 */
#define WHEEL_PW_MAX 63              /* keep < MAX_PASSWORD_LEN (64) */
#define WHEEL_MODE_R_MID UI_PX(114)  /* the bottom-band icons' radius */
#define WHEEL_SHARE (360.0f / 26.0f) /* one letter's equal share of the ring */
#define WHEEL_N_SECTORS 12

/* The four register modes (all printable ASCII -- the 32 px font now
 * covers 32-126). The digit/symbol registers are shorter than 26, so
 * their groups fill in order (the last group takes the remainder). */
static const char *const s_wheel_sets[4] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789#@._-+*?!/&%:;,",
    "#$%&@!_=<>[]{}~^:,/?*+-.'",
};

/* The 12 frequency-grouped sectors (FIXED across all four registers).
 * Group 5 = the rare j/k/q cluster, group 10 = v/x/z. */
static const char *const s_wheel_sector_chars[12] = {"ab", "cd", "ef", "gh", "il",  "jkq",
                                                     "mn", "op", "rs", "tu", "vxz", "wy"};

/* House colours for the reveal (the lab's AMOLED palette). */
#define WHEEL_BOX_DARK lv_color_hex(0x2B2E34) /* the un-lit box fill */
#define WHEEL_BOX_LIT lv_color_hex(0xF2F3F5)  /* the lit box fill (inverts) */
#define WHEEL_LIT_INK lv_color_hex(0x101115)  /* the lit box's near-black letter */
#define WHEEL_HAIR lv_color_hex(0x3A3D44)     /* the hairline dividers */

static char s_wheel_pw[WHEEL_PW_MAX + 1];
static int s_wheel_pw_len = 0;
static int s_wheel_mode = -1; /* -1 until the first set_mode drew the letters */
static int s_wheel_n = 0;     /* slots of the active mode (<= 26) */
static char s_wheel_chars[26];
static int s_wheel_sector_start[12];  /* the global letter index of each sector */
static int s_wheel_n_of[12];          /* the per-sector letter count (active mode) */
static float s_wheel_slot_angle[26];  /* global letter s's centre angle (LVGL deg) */
static int s_wheel_press_sector = -1; /* the sticky reveal sector (-1 = none) */
static int s_wheel_hot = -1;          /* the highlighted global letter (preview) */

static lv_obj_t *s_wheel_catch;               /* the transparent press band (r 140..233) */
static lv_obj_t *s_wheel_boxes[12][3];        /* the pre-built reveal arc boxes */
static lv_obj_t *s_wheel_letters_in[26];      /* the at-rest letter labels (r 168) */
static lv_obj_t *s_wheel_letters_out[26];     /* the reveal letter labels (r 209) */
static lv_obj_t *s_wheel_dividers[12];        /* the sector hairlines (r 150..226) */
static lv_obj_t *s_wheel_box_dividers[12][2]; /* the in-sector box hairlines */
static lv_obj_t *s_wheel_mode_arc;            /* the 240-degree register track + lit key */
static lv_obj_t *s_wheel_mode_keys[4];
static lv_obj_t *s_wheel_commit;    /* the house circle-check commit key */
static lv_obj_t *s_wheel_discard;   /* the circle-xmark discard key */
static lv_obj_t *s_wheel_center;    /* the centre tap box */
static lv_obj_t *s_wheel_pw_label;  /* the running entry / centre preview */
static lv_obj_t *s_wheel_sub_label; /* "release commits" */
static lv_obj_t *s_wheel_ssid_label;

/* Map the indev point (display coords) to a letter slot: inverse the
 * screen root's transform (the orientation roll) first -- the same idiom
 * as alarm_time_point_to_day(). Returns false when the point is outside
 * the forgiving radial band. */
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
    /* atan2f: radians, 0 = right, 90 = down -- the LVGL slot convention.
     * The EXACT box: the finger's angle names the sector (its raw start..
     * start + span, which may cross the 360/0 line), the offset-in-sector
     * names the box -- no nearest-centre. */
    float theta = atan2f(dy, dx) * 180.0f / 3.14159265f;
    while (theta < 0.0f) {
        theta += 360.0f;
    }
    while (theta >= 360.0f) {
        theta -= 360.0f;
    }
    int slot = 0;
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        int cnt = s_wheel_n_of[k];
        if (cnt == 0) {
            continue;
        }
        int s0 = s_wheel_sector_start[k];
        float a0 = (float)s0 * WHEEL_SHARE - 90.0f;
        float a1 = a0 + (float)cnt * WHEEL_SHARE;
        float t = theta;
        while (t < a0) {
            t += 360.0f;
        }
        while (t >= a1) {
            t -= 360.0f;
        }
        if (t >= a0 && t < a1) {
            slot = s0 + (int)((t - a0) / WHEEL_SHARE);
            if (slot >= s0 + cnt) { /* float edge: clamp to the last box */
                slot = s0 + cnt - 1;
            }
            break;
        }
    }
    *out_slot = slot;
    return true;
}

static void settings_wheel_pw_label(void) {
    /* Unmasked, but the preview shows only the LAST 6 chars (a 63-char
     * password would run off the ring; s_wheel_pw keeps it all). */
    if (s_wheel_pw_len == 0) {
        lv_label_set_text(s_wheel_pw_label, "....");
        lv_obj_set_style_text_font(s_wheel_pw_label, &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_wheel_sub_label, "");
        return;
    }
    char buf[16];
    int start = s_wheel_pw_len > 6 ? s_wheel_pw_len - 6 : 0;
    int pos = start > 0 ? snprintf(buf, sizeof(buf), "...") : 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%.*s", s_wheel_pw_len - start, s_wheel_pw + start);
    (void)pos;
    lv_label_set_text(s_wheel_pw_label, buf);
    lv_obj_set_style_text_font(s_wheel_pw_label, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_wheel_sub_label, "");
}

/* Recolour both letter rings. The lit global letter's REVEAL letter is
 * the near-black ink on the inverted (white) box; the un-lit reveal
 * letters are the light ink. The at-rest letters stay bright while their
 * sector is the pressed one and dim otherwise. */
static void settings_wheel_recolor_letters(void) {
    lv_color_t in_rest = lv_color_hex(0xC9CCD3);
    lv_color_t in_dim = lv_color_hex(0x555555);
    lv_color_t out_light = lv_color_hex(0xF2F3F5);
    for (int s = 0; s < 26; s++) {
        if (s >= s_wheel_n) {
            continue;
        }
        int k = 0;
        while (k < WHEEL_N_SECTORS - 1 && s >= s_wheel_sector_start[k + 1]) {
            k++;
        }
        bool pressed = (s_wheel_press_sector >= 0 && k == s_wheel_press_sector);
        bool lit = (s == s_wheel_hot);
        bool inner_bright = (s_wheel_press_sector < 0) || pressed;
        lv_obj_set_style_text_color(s_wheel_letters_in[s], inner_bright ? in_rest : in_dim,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_wheel_letters_out[s], lit ? WHEEL_LIT_INK : out_light,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/* The lit box inverts: white fill, near-black letter (the v1 white low-opa
 * indicator is gone -- the box IS the target). */
static void settings_wheel_apply_lit(void) {
    for (int s = 0; s < 26; s++) {
        if (s >= s_wheel_n) {
            continue;
        }
        int k = 0;
        int j = 0;
        while (k < WHEEL_N_SECTORS && s >= s_wheel_sector_start[k] + s_wheel_n_of[k]) {
            k++;
        }
        j = s - s_wheel_sector_start[k];
        lv_obj_set_style_arc_color(s_wheel_boxes[k][j], s == s_wheel_hot ? WHEEL_BOX_LIT : WHEEL_BOX_DARK,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void settings_wheel_set_hot(int s) {
    if (s == s_wheel_hot) {
        return;
    }
    s_wheel_hot = s;
    if (s < 0 || s >= s_wheel_n) {
        /* No preview: the centre shows the running entry (DTMF-style
         * handback), the subline is empty. */
        settings_wheel_pw_label();
    } else {
        /* DTMF-style centre preview: the lit letter large + the commit
         * hint. The commit happens ONLY on release. The 32 px SSID font
         * covers the full printable range (32-126) in every register --
         * the 48 px font is a digits subset (45-58) with no letters. */
        char one[2] = {s_wheel_chars[s], 0};
        lv_label_set_text(s_wheel_pw_label, one);
        lv_obj_set_style_text_font(s_wheel_pw_label, &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(s_wheel_pw_label, LV_ALIGN_CENTER, 0, -UI_PX(6));
        lv_label_set_text(s_wheel_sub_label, "release commits");
    }
    settings_wheel_recolor_letters();
    settings_wheel_apply_lit();
}

/* The sticky reveal: show only the pressed sector's boxes, box dividers
 * and reveal letters. Pressed (on, sector) / released or cancelled (off).
 * The 12 sector dividers stay visible (the rest state's wayfinding). */
static void settings_wheel_set_revealed(bool on, int sector) {
    int sec = on ? sector : -1;
    s_wheel_press_sector = sec;
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        bool vis = on && k == sec;
        for (int j = 0; j < 2; j++) {
            if (j < s_wheel_n_of[k] - 1) {
                if (vis) {
                    lv_obj_clear_flag(s_wheel_box_dividers[k][j], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(s_wheel_box_dividers[k][j], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        for (int j = 0; j < 3; j++) {
            if (j < s_wheel_n_of[k]) {
                if (vis) {
                    lv_obj_clear_flag(s_wheel_boxes[k][j], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(s_wheel_boxes[k][j], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        for (int j = 0; j < s_wheel_n_of[k]; j++) {
            int s = s_wheel_sector_start[k] + j;
            if (vis) {
                lv_obj_clear_flag(s_wheel_letters_out[s], LV_OBJ_FLAG_HIDDEN);
                /* The at-rest letter (r 168) would double with the reveal
                 * letter (r 209) inside the box -- the lab's opaque box
                 * simply covers it; hide it instead. */
                lv_obj_add_flag(s_wheel_letters_in[s], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_wheel_letters_out[s], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(s_wheel_letters_in[s], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (!on) {
        s_wheel_hot = -1;
        settings_wheel_pw_label();
    }
    settings_wheel_recolor_letters();
}

/* Apply a register mode: re-string the 26 slots, re-place the letters,
 * dividers and box sectors (the slot angle is 360/26 per letter), light
 * the mode key. */
static void settings_wheel_set_mode(int m) {
    if (m == s_wheel_mode) {
        return;
    }
    s_wheel_mode = m;
    s_wheel_n = (int)strlen(s_wheel_sets[m]);
    for (int j = 0; j < 26; j++) {
        s_wheel_chars[j] = j < s_wheel_n ? s_wheel_sets[m][j] : 0;
    }
    /* Per-sector counts + the global letter index of each sector start:
     * fill each group in order (the digit/symbol registers are shorter
     * than 26; the last group takes the remainder). */
    int idx = 0;
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        int cnt = (int)strlen(s_wheel_sector_chars[k]);
        if (idx + cnt > s_wheel_n) {
            cnt = s_wheel_n - idx;
        }
        s_wheel_n_of[k] = cnt;
        s_wheel_sector_start[k] = idx;
        idx += cnt;
    }
    for (int s = 0; s < 26; s++) {
        s_wheel_slot_angle[s] = (s + 0.5f) * WHEEL_SHARE - 90.0f;
        bool on = s < s_wheel_n;
        if (on) {
            float a = s_wheel_slot_angle[s] * 3.14159265f / 180.0f;
            int32_t dx_in = (int32_t)(WHEEL_INNER_R_MID * cosf(a));
            int32_t dy_in = (int32_t)(WHEEL_INNER_R_MID * sinf(a));
            char one[2] = {s_wheel_chars[s], 0};
            lv_label_set_text(s_wheel_letters_in[s], one);
            lv_obj_align(s_wheel_letters_in[s], LV_ALIGN_CENTER, dx_in, dy_in);
            int32_t dx_out = (int32_t)(WHEEL_OUTER_R_MID * cosf(a));
            int32_t dy_out = (int32_t)(WHEEL_OUTER_R_MID * sinf(a));
            lv_label_set_text(s_wheel_letters_out[s], one);
            lv_obj_align(s_wheel_letters_out[s], LV_ALIGN_CENTER, dx_out, dy_out);
        } else {
            /* The digit/symbol registers are 25 slots: the extra letter
             * slot (25) must be CLEARED, not left holding the previous
             * register's "z" at its stale position (the stray "Z" the
             * user saw after a register switch). */
            lv_label_set_text(s_wheel_letters_in[s], "");
            lv_label_set_text(s_wheel_letters_out[s], "");
        }
        /* The box sector for global letter s: its equal share, raw angles
         * (may be negative or exceed 360; the setter normalises). */
        int k = 0;
        while (k < WHEEL_N_SECTORS - 1 && s >= s_wheel_sector_start[k + 1]) {
            k++;
        }
        int j = s - s_wheel_sector_start[k];
        float a0 = (float)s * WHEEL_SHARE - 90.0f;
        float a1 = (float)(s + 1) * WHEEL_SHARE - 90.0f;
        if (on) {
            lv_arc_set_bg_angles(s_wheel_boxes[k][j], (lv_value_precise_t)a0, (lv_value_precise_t)a1);
        }
    }
    /* Re-place the 12 sector dividers (the group boundaries) + the
     * per-box dividers (the in-sector hairlines). LVGL's transform pivot
     * defaults to the object's TOP-LEFT corner, so each bar carries an
     * explicit centre pivot (set at creation) and the rotation about its
     * own centre runs it along the radial; the bar is positioned so its
     * centre sits at the mid-radius (150..226 and 154..222 both centre at
     * 188), rotated by 0.1-degrees (pass deg*10). */
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        int cnt = s_wheel_n_of[k];
        if (cnt > 0) {
            float a = (float)s_wheel_sector_start[k] * WHEEL_SHARE - 90.0f; /* the sector's start */
            float ar = a * 3.14159265f / 180.0f;
            int32_t len = UI_PX(76);
            int32_t tx = (int32_t)(UI_PX(188) * cosf(ar));
            int32_t ty = (int32_t)(UI_PX(188) * sinf(ar));
            lv_obj_set_size(s_wheel_dividers[k], len, 1);
            lv_obj_set_pos(s_wheel_dividers[k], WHEEL_C + tx - len / 2, WHEEL_C + ty);
            lv_obj_set_style_transform_rotation(s_wheel_dividers[k], (int32_t)(a * 10.0f), 0);
        }
        for (int j = 0; j < 2; j++) {
            if (j < cnt - 1) {
                int s = s_wheel_sector_start[k] + j + 1; /* the boundary after box j */
                float a = (float)s * WHEEL_SHARE - 90.0f;
                float ar = a * 3.14159265f / 180.0f;
                int32_t len = UI_PX(68);
                int32_t tx = (int32_t)(UI_PX(188) * cosf(ar));
                int32_t ty = (int32_t)(UI_PX(188) * sinf(ar));
                lv_obj_set_size(s_wheel_box_dividers[k][j], len, 1);
                lv_obj_set_pos(s_wheel_box_dividers[k][j], WHEEL_C + tx - len / 2, WHEEL_C + ty);
                lv_obj_set_style_transform_rotation(s_wheel_box_dividers[k][j], (int32_t)(a * 10.0f), 0);
            }
        }
    }
    lv_arc_set_angles(s_wheel_mode_arc, (lv_value_precise_t)(150.0f + 60.0f * m),
                      (lv_value_precise_t)(210.0f + 60.0f * m));
    for (int k = 0; k < 4; k++) {
        lv_obj_set_style_text_color(s_wheel_mode_keys[k], k == m ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    settings_wheel_set_hot(-1);
}

/* The sweep press on the ring band. PRESSED reveals the pressed sector's
 * boxes (the exact box under the finger lights + previews); PRESSING
 * tracks the finger (the sticky sector's box under the finger); RELEASED
 * commits the highlighted letter when the finger is still in the band. */
/* Append slot s's character to the entry (bounds-checked). */
static void settings_wheel_add_slot(int s) {
    if (s >= 0 && s < s_wheel_n && s_wheel_pw_len < WHEEL_PW_MAX) {
        s_wheel_pw[s_wheel_pw_len++] = s_wheel_chars[s];
        s_wheel_pw[s_wheel_pw_len] = 0;
        ESP_LOGW("settings_wheel", "entered letter %c (pw %d chars)", s_wheel_chars[s], s_wheel_pw_len);
    }
}

static void event_handler_cb_wheel_sweep(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        int j;
        if (wheel_point_to_slot(&j)) {
            int sec = 0;
            while (sec < WHEEL_N_SECTORS - 1 && j >= s_wheel_sector_start[sec + 1]) {
                sec++;
            }
            settings_wheel_set_revealed(true, sec);
            settings_wheel_set_hot(j);
        }
    } else if (code == LV_EVENT_PRESSING) {
        int j;
        if (wheel_point_to_slot(&j) && s_wheel_press_sector >= 0) {
            /* The sticky sector: clamp the finger's box to that sector's
             * range (sliding across a boundary extends the sweep over the
             * pressed sector's boxes, it does not re-key the set). */
            int lo = s_wheel_sector_start[s_wheel_press_sector];
            int hi = lo + s_wheel_n_of[s_wheel_press_sector] - 1;
            int cl = (j < lo) ? lo : ((j > hi) ? hi : j);
            settings_wheel_set_hot(cl);
        }
    } else if (code == LV_EVENT_RELEASED) {
        int j;
        bool in_band = wheel_point_to_slot(&j);
        if (in_band) {
            settings_wheel_add_slot(s_wheel_hot);
        }
        settings_wheel_set_revealed(false, -1);
        settings_wheel_pw_label();
    } else if (code == LV_EVENT_CANCEL) {
        settings_wheel_set_revealed(false, -1);
        settings_wheel_pw_label();
    }
}

/* The register/shift arc: a PRESSED on the 240-degree band snaps to the
 * exact 60-degree key under the finger (LVGL 150..390). */
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
    /* The arc spans LVGL 150..390 (240..480 in 0-up, the top half). The
     * finger's key = the 60-degree sector of the 240-degree span. */
    float rel = theta - 150.0f;
    while (rel < 0.0f) {
        rel += 360.0f;
    }
    while (rel >= 360.0f) {
        rel -= 360.0f;
    }
    if (rel < 240.0f) {
        int k = (int)(rel / 60.0f);
        if (k > 3) {
            k = 3;
        }
        settings_wheel_set_mode(k);
        ESP_LOGW("settings_wheel", "mode %d", k);
    }
}

/* Persist the entry and start the sync (the house circle-check COMMIT key). */
static void settings_wheel_commit_password(void) {
    if (s_wheel_pw_len == 0) {
        ESP_LOGW("settings_wheel", "commit: empty password ignored");
        return;
    }
    esp_err_t r = wifi_credentials_write(s_wheel_ssid, s_wheel_pw);
    if (r != ESP_OK) {
        ESP_LOGW("settings_wheel", "write credentials -> %s", esp_err_to_name(r));
        return;
    }
    ESP_LOGW("settings_wheel", "credentials saved (SSID %zu chars, pw %d chars); starting sync", strlen(s_wheel_ssid),
             s_wheel_pw_len);
    action_start_wifi_sync();
}

/* The house circle-CHECK commit key (LVGL ~31.5..88.5). */
static void event_handler_cb_wheel_commit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    settings_wheel_commit_password();
}

/* The exit to the WiFi list (the centre long-press; the circle-xmark
 * discard key with an EMPTY entry -- the keyboard has no other in-ring
 * exit, the commit key is the circle-check). The refresh re-draws the
 * SSID caption and resets the keyboard to lowercase. */
static void settings_wheel_exit_to_list(void) {
    loadScreen(SCREEN_ID_SETTINGS_WIFI);
    ui_tick();
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI);
}

/* The circle-xmark DISCARD key (LVGL ~91.5..148.5). With a non-empty
 * entry it CLEARS it; with an empty entry it EXITS the menu (the user's
 * "X with nothing entered should leave the screen" -- backspace on an
 * empty entry is a no-op, so the X doubles as the zero-input exit). */
static void event_handler_cb_wheel_discard(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    if (s_wheel_pw_len == 0) {
        ESP_LOGW("settings_wheel", "discard X with empty entry -> back to the WiFi list");
        settings_wheel_exit_to_list();
        return;
    }
    s_wheel_pw_len = 0;
    s_wheel_pw[0] = 0;
    settings_wheel_pw_label();
    ESP_LOGW("settings_wheel", "discarded (pw cleared)");
}

/* The centre: a plain tap BACKSPACES (v2 -- on the centre box the 400 ms
 * long-press is the ONLY exit (the empty-entry X doubles as the other
 * exit, see the discard handler): v2 dropped v1's in-ring exit, the
 * circle-xmark glyph is now the discard key (the circle-check commits),
 * and the centre box (r ~55)
 * never overlaps the sweep band (r 140..233), so the hold cannot start
 * a letter -- it is a navigation gesture, like v1's centre long-press). */
static void event_handler_cb_wheel_center(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (s_wheel_pw_len > 0) {
            s_wheel_pw[--s_wheel_pw_len] = 0;
            settings_wheel_pw_label();
            ESP_LOGW("settings_wheel", "backspace (pw %d chars)", s_wheel_pw_len);
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ESP_LOGW("settings_wheel", "long press -> back to the WiFi list");
        settings_wheel_exit_to_list();
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

#ifdef CONFIG_UI_DEMO_MODE
/* ---- demo-mode wheel drive (docs/verification/demo-mode) -------------
 *
 * The tour taps the wheel through the REAL touch path (a second
 * virtual LVGL pointer injected by demo.c: hit test, sticky sector
 * reveal, commit-on-release, the works). These expose the wheel's own
 * geometry so the virtual taps land in the right annuli/sectors
 * instead of duplicating the layout. The points returned are in the
 * wheel's LOCAL screen space (the same space the handlers' inverse
 * transform maps the raw indev point into); demo.c applies the screen
 * root's base orientation rotation to turn them into display coords.
 * All return false when the wheel is not in a state to answer (wrong
 * register, letter not in the active set). */

/* Polar point at the wheel centre, LVGL degrees (0 = right, 90 = down). */
static void wheel_demo_point(float theta_deg, int32_t r, lv_point_t *out) {
    const float PI = 3.14159265f;
    float a = theta_deg * PI / 180.0f;
    out->x = (lv_coord_t)(WHEEL_C + r * cosf(a));
    out->y = (lv_coord_t)(WHEEL_C + r * sinf(a));
}

bool settings_wheel_demo_mode_point(int mode, lv_point_t *out) {
    if (mode < 0 || mode > 3) {
        return false;
    }
    /* The mode arc's annulus r 84..144 (the icon row sits at r 114);
     * key k spans LVGL 150+60k .. 210+60k, centre 180+60k. */
    wheel_demo_point(180.0f + 60.0f * (float)mode, UI_PX(114), out);
    return true;
}

bool settings_wheel_demo_letter_point(char ch, lv_point_t *out) {
    if (s_wheel_n <= 0) {
        return false; /* the active register was never drawn */
    }
    for (int i = 0; i < s_wheel_n; i++) {
        if (s_wheel_chars[i] == ch) {
            /* The press band 140..233; the at-rest letter row sits at
             * r 168, the reveal row at 209 -- aim between them. */
            wheel_demo_point(s_wheel_slot_angle[i], (UI_PX(140) + WHEEL_C) / 2, out);
            return true;
        }
    }
    return false;
}

bool settings_wheel_demo_commit_point(lv_point_t *out) {
    /* circle-CHECK key: LVGL ~31.5..88.5, annulus 84..144. */
    wheel_demo_point(60.0f, UI_PX(114), out);
    return true;
}

bool settings_wheel_demo_discard_point(lv_point_t *out) {
    /* circle-xmark DISCARD key: LVGL ~91.5..148.5, annulus 84..144. */
    wheel_demo_point(120.0f, UI_PX(114), out);
    return true;
}

bool settings_wheel_demo_center_point(lv_point_t *out) {
    out->x = WHEEL_C;
    out->y = WHEEL_C;
    return true;
}

int settings_wheel_demo_pw_len(void) { return s_wheel_pw_len; }

/* Open the keyboard with a fixed SSID (the tour's no-AP fallback: with
 * APs in range the walk taps a real row instead, which prefills). */
void settings_wheel_demo_open(const char *ssid) {
    settings_wheel_prefill_ssid(ssid);
    loadScreen(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
    ui_tick();
    settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI_PASSWORD);
}
#endif /* CONFIG_UI_DEMO_MODE */

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
    settings_wheel_set_revealed(false, -1);
    lv_label_set_text(s_wheel_ssid_label, s_wheel_ssid[0] ? s_wheel_ssid : "SELECT A NETWORK");
}

void create_screen_settings_wifi_password() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_wifi_password = obj;
    /* NOT settings_sub_screen_prepare: its 90 px bottom exit would sit
     * under the full-360-degree ring -- the wheel gets the bottom-band
     * commit/discard keys instead (see the section header). */
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, UI_PANEL_PX, UI_PANEL_PX);
    settings_screen_prepare(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The band hairlines (r 150 / 226): two thin full-360-degree arcs
     * (drawn as the MAIN track, the INDICATOR left empty). The pad is the
     * DISTANCE FROM THE EDGE (r_out = W/2 - pad), so a ring at radius r
     * needs pad WHEEL_C - r. */
    for (int b = 0; b < 2; b++) {
        int32_t r = b == 0 ? UI_PX(150) : UI_PX(226);
        lv_obj_t *h = lv_arc_create(obj);
        lv_obj_set_pos(h, 0, 0);
        lv_obj_set_size(h, UI_PANEL_PX, UI_PANEL_PX);
        lv_obj_set_style_pad_all(h, WHEEL_C - r, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_arc_set_bg_angles(h, 0, 360);
        lv_arc_set_angles(h, 0, 0);
        lv_obj_set_style_arc_width(h, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(h, WHEEL_HAIR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(h, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(h, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(h, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        /* The theme gives EVERY arc a blue circular KNOB (colour primary,
         * opaque); on a full ring it lands at the centre -- kill it. */
        lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The transparent press band: an arc whose hit annulus is r 140..233
     * (pad 0 -> r 233, width 93 -> inner edge 233-93 = 140) covering the
     * inner ring, the gap and the outer ring -- a press anywhere in the
     * band reveals, which is the radially-forgiving press. Invisible
     * (both arc opacities 0); the full-360-degree bg span keeps the
     * hit-test angle unbounded. ADV_HITTEST is REQUIRED (see the section
     * header). */
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

    /* The 12 sector hairlines (r 150..226, always visible) + the per-box
     * hairlines (r 154..222, shown only with the reveal): thin lv_obj
     * bars (lv_line is not built on the device). Pre-built once. */
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        s_wheel_dividers[k] = lv_obj_create(obj);
        lv_obj_set_size(s_wheel_dividers[k], UI_PX(76), 1);
        lv_obj_set_style_bg_color(s_wheel_dividers[k], WHEEL_HAIR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_wheel_dividers[k], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_wheel_dividers[k], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        /* The rotation below runs about the bar's OWN centre -- the LVGL
         * default pivot is the top-left corner, which would swing the bar
         * out on a long arc (the stray line the user saw through "z"). */
        lv_obj_set_style_transform_pivot_x(s_wheel_dividers[k], LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(s_wheel_dividers[k], LV_PCT(50), 0);
        lv_obj_clear_flag(s_wheel_dividers[k], LV_OBJ_FLAG_CLICKABLE);
        for (int j = 0; j < 2; j++) {
            s_wheel_box_dividers[k][j] = lv_obj_create(obj);
            lv_obj_set_size(s_wheel_box_dividers[k][j], UI_PX(68), 1);
            lv_obj_set_style_bg_color(s_wheel_box_dividers[k][j], WHEEL_HAIR, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_wheel_box_dividers[k][j], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(s_wheel_box_dividers[k][j], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            /* Centre pivot: the rotation runs about the bar's own centre
             * (the LVGL default is the top-left corner). */
            lv_obj_set_style_transform_pivot_x(s_wheel_box_dividers[k][j], LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(s_wheel_box_dividers[k][j], LV_PCT(50), 0);
            lv_obj_clear_flag(s_wheel_box_dividers[k][j], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(s_wheel_box_dividers[k][j], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* The 26 reveal arc boxes (r 150..226, 76 px wide, non-rounded). The
     * sector angles are set per mode in settings_wheel_set_mode (the raw
     * share angles; the setter normalises the wrap). The pad is WHEEL_C -
     * r_out, so the outer edge lands at r 226. Pre-built once; hidden
     * until revealed. */
    for (int k = 0; k < WHEEL_N_SECTORS; k++) {
        for (int j = 0; j < 3; j++) {
            s_wheel_boxes[k][j] = lv_arc_create(obj);
            lv_obj_set_pos(s_wheel_boxes[k][j], 0, 0);
            lv_obj_set_size(s_wheel_boxes[k][j], UI_PANEL_PX, UI_PANEL_PX);
            lv_obj_set_style_pad_all(s_wheel_boxes[k][j], WHEEL_C - UI_PX(226), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_arc_set_bg_angles(s_wheel_boxes[k][j], 0, 0);
            lv_arc_set_angles(s_wheel_boxes[k][j], 0, 0);
            lv_obj_set_style_arc_width(s_wheel_boxes[k][j], UI_PX(76), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(s_wheel_boxes[k][j], WHEEL_BOX_DARK, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(s_wheel_boxes[k][j], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_rounded(s_wheel_boxes[k][j], false, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(s_wheel_boxes[k][j], LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_wheel_boxes[k][j], LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
            lv_obj_clear_flag(s_wheel_boxes[k][j], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(s_wheel_boxes[k][j], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* The 26 at-rest letter labels (r 168) + the 26 reveal letter labels
     * (r 209), both 32 px (the SSID font). The reveal letters start
     * hidden (shown only with the pressed sector's reveal). */
    for (int s = 0; s < 26; s++) {
        s_wheel_letters_in[s] = lv_label_create(obj);
        lv_obj_set_style_text_font(s_wheel_letters_in[s], &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_wheel_letters_in[s], "");
        lv_obj_set_size(s_wheel_letters_in[s], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(s_wheel_letters_in[s], lv_color_hex(0xC9CCD3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_wheel_letters_in[s], LV_OBJ_FLAG_CLICKABLE);
        s_wheel_letters_out[s] = lv_label_create(obj);
        lv_obj_set_style_text_font(s_wheel_letters_out[s], &ui_font_montserrat_regular_32_ssid,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_wheel_letters_out[s], "");
        lv_obj_set_size(s_wheel_letters_out[s], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(s_wheel_letters_out[s], lv_color_hex(0xF2F3F5), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(s_wheel_letters_out[s], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_wheel_letters_out[s], LV_OBJ_FLAG_HIDDEN);
    }

    /* The 240-degree register track (r 84..144, LVGL 150..390) + the
     * active mode's 60-degree key (the INDICATOR). The track's START is
     * 150; the END 390 exceeds 360 (the renderer's end < start branch
     * draws the wrap). ADV_HITTEST runs the r 84..144 annulus. BOTH
     * parts get a colour (styling only the MAIN leaves the indicator at
     * the theme default). */
    s_wheel_mode_arc = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_mode_arc, 0, 0);
    lv_obj_set_size(s_wheel_mode_arc, UI_PANEL_PX, UI_PANEL_PX);
    /* r_out = W/2 - pad: the track's outer edge at r 144 needs the MAIN pad
     * WHEEL_C - UI_PX(144) (arc_r = W/2 - MAIN pad = UI_PX(144)). The
     * INDICATOR has its OWN pad: indicator radius = arc_r - INDICATOR pad.
     * It MUST stay 0 -- the item-5a fix (2026-11-04) put the TRACK's pad on
     * the indicator too, which dropped the indicator radius to UI_PX(55)
     * and made the 60 px width clamp (lv_draw_sw_arc: width = min(width,
     * radius)) into a FILLED sector from the centre (the "pie wedges from
     * the middle" the user saw cycling the registers, 2026-11-05). A 0
     * indicator pad leaves the radius at UI_PX(144), so the lit 60-degree
     * key's outer edge lands at r 144 and its 60 px width covers the track's
     * r 84..144 annulus exactly (the ring segment, not a pie). */
    lv_obj_set_style_pad_all(s_wheel_mode_arc, WHEEL_C - UI_PX(144), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wheel_mode_arc, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_mode_arc, 150, 390);
    lv_arc_set_angles(s_wheel_mode_arc, 150, 210);
    lv_obj_set_style_arc_width(s_wheel_mode_arc, UI_PX(60), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_wheel_mode_arc, UI_PX(60), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_mode_arc, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_mode_arc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_mode_arc, SETTINGS_WHITE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_mode_arc, 0x2E, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_mode_arc, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_mode_arc, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_mode_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_mode_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_mode_arc, LV_OBJ_FLAG_ADV_HITTEST); /* its r 84..144 annulus, not the box */
    lv_obj_add_event_cb(s_wheel_mode_arc, event_handler_cb_wheel_mode, LV_EVENT_PRESSED, 0);
#ifdef CONFIG_BOARD_KNOB
    /* Knob press partition (bench 2026-11-11): at 360 px the mode arc's
     * default lv_arc annulus hit-test answered presses on the INNER half
     * of the letter band (r ~108..121 logged "mode 3"/"mode 0" instead
     * of committing a letter, and a register rebuild ended in a
     * task-WDT reset). Lift the character catch to sit just above the
     * register arc: its own native annulus (arc_width WHEEL_C -
     * WHEEL_HIT_R_IN -> r 108..180 at the knob's scale) wins the ring by
     * z-order, and every press it rejects (r < 108) falls through to the
     * register arc exactly as before. The keys/COMMIT/DISCARD/centre,
     * created later, stay above the catch; the clocks never compile this
     * (their geometry is proven at 466 px) and their TU preprocesses
     * identically. */
    lv_obj_move_to_index(s_wheel_catch, lv_obj_get_index(s_wheel_mode_arc) + 1);
#endif

    /* The four mode keys (14 px) at the key centres (LVGL 180/240/300/360,
     * r 114) + the arc's five boundary ticks at 150/210/270/330/390. */
    const char *mode_names[4] = {"abc", "ABC", "123", "#$"};
    for (int k = 0; k < 4; k++) {
        float a = (180.0f + 60.0f * k) * 3.14159265f / 180.0f;
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
        float a = (150.0f + 60.0f * k) * 3.14159265f / 180.0f;
        lv_obj_t *t = lv_obj_create(obj);
        lv_obj_set_size(t, UI_PX(56), 1);
        lv_obj_set_style_bg_color(t, lv_color_hex(0x5F5F5F), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(t, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        /* Centre pivot: the rotation runs about the tick's own centre
         * (the LVGL default is the top-left corner). */
        lv_obj_set_style_transform_pivot_x(t, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(t, LV_PCT(50), 0);
        int32_t tx = (int32_t)(WHEEL_MODE_R_MID * cosf(a));
        int32_t ty = (int32_t)(WHEEL_MODE_R_MID * sinf(a));
        lv_obj_set_pos(t, WHEEL_C + tx - UI_PX(28), WHEEL_C + ty);
        lv_obj_set_style_transform_rotation(t, (int32_t)((150.0f + 60.0f * k) * 10.0f), 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The bottom 120-degree band: the house circle-CHECK commit key
     * (LVGL ~31.5..88.5) and the house circle-xmark discard key
     * (LVGL ~91.5..148.5).
     * Both are 60-degree arcs (the hit annulus r 84..144); the icons sit
     * at r 114. */
    s_wheel_commit = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_commit, 0, 0);
    lv_obj_set_size(s_wheel_commit, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_all(s_wheel_commit, WHEEL_C - UI_PX(144), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_commit, 31.5f, 88.5f);
    lv_arc_set_angles(s_wheel_commit, 0, 0);
    lv_obj_set_style_arc_width(s_wheel_commit, UI_PX(60), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_commit, lv_color_hex(0x1E1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_commit, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_commit, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_commit, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_commit, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_commit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_commit, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(s_wheel_commit, event_handler_cb_wheel_commit, LV_EVENT_PRESSED, 0);
    s_wheel_discard = lv_arc_create(obj);
    lv_obj_set_pos(s_wheel_discard, 0, 0);
    lv_obj_set_size(s_wheel_discard, UI_PANEL_PX, UI_PANEL_PX);
    lv_obj_set_style_pad_all(s_wheel_discard, WHEEL_C - UI_PX(144), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_arc_set_bg_angles(s_wheel_discard, 91.5f, 148.5f);
    lv_arc_set_angles(s_wheel_discard, 0, 0);
    lv_obj_set_style_arc_width(s_wheel_discard, UI_PX(60), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_wheel_discard, lv_color_hex(0x1E1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_discard, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(s_wheel_discard, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(s_wheel_discard, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wheel_discard, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wheel_discard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wheel_discard, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(s_wheel_discard, event_handler_cb_wheel_discard, LV_EVENT_PRESSED, 0);

    /* The commit icon: the house CIRCLE-CHECK at the COMMIT key's centre
     * (LVGL 60, r 114) — same bar idiom as the circle-xmark it replaced
     * (2026-11-08): X now means DISCARD and sits on the discard key, the
     * check means accept. The FontAwesome subset carries no circle-check
     * code point (the fonts are probed-glyph subsets), so the glyph is
     * built, not typed. */
    {
        float a = 60.0f * 3.14159265f / 180.0f;
        int32_t cx = WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * cosf(a));
        int32_t cy = WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * sinf(a));
        lv_obj_t *ring = lv_obj_create(obj);
        lv_obj_set_size(ring, UI_PX(26), UI_PX(26));
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(ring, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(ring, cx - UI_PX(13), cy - UI_PX(13));
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
        /* The check: a short +45-degree stroke into a long -45-degree
         * one, the vertex at bottom-left (LVGL y-down; lengths ~7 and
         * ~14 px, offsets from the ring centre). Centre pivot on both
         * bars (LVGL's default top-left pivot would swing them off). */
        const int32_t chk[2][4] = {{7, 450, -5, 2}, {14, -450, 3, 0}}; /* w, rot10, dx, dy */
        for (int i = 0; i < 2; i++) {
            lv_obj_t *bar = lv_obj_create(ring);
            lv_obj_set_size(bar, UI_PX(chk[i][0]), UI_PX(2));
            lv_obj_set_style_bg_color(bar, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_transform_pivot_x(bar, LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(bar, LV_PCT(50), 0);
            lv_obj_align(bar, LV_ALIGN_CENTER, UI_PX(chk[i][2]), UI_PX(chk[i][3]));
            lv_obj_set_style_transform_rotation(bar, chk[i][1], 0); /* 0.1-deg units */
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    /* The discard icon: the house circle-xmark (the v1 in-ring exit
     * glyph, the documented deviation from the house 90 px bottom exit)
     * at the DISCARD key's centre (LVGL 120, r 114). It moved here from
     * the commit key when X became "discard" (2026-11-08); the bar-built
     * trash can it replaces did not render readably on the panel. */
    {
        float a = 120.0f * 3.14159265f / 180.0f;
        int32_t cx = WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * cosf(a));
        int32_t cy = WHEEL_C + (int32_t)(WHEEL_MODE_R_MID * sinf(a));
        lv_obj_t *exit = lv_obj_create(obj);
        lv_obj_set_size(exit, UI_PX(26), UI_PX(26));
        lv_obj_set_style_radius(exit, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(exit, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(exit, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(exit, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(exit, cx - UI_PX(13), cy - UI_PX(13));
        lv_obj_clear_flag(exit, LV_OBJ_FLAG_CLICKABLE);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *bar = lv_obj_create(exit);
            lv_obj_set_size(bar, UI_PX(12), UI_PX(2));
            lv_obj_set_style_bg_color(bar, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            /* Centre pivot: the 45/135-degree rotation runs about the bar's
             * own centre (the LVGL default is the top-left corner, which
             * swings the bars off the circle and reads as a stray dash). */
            lv_obj_set_style_transform_pivot_x(bar, LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(bar, LV_PCT(50), 0);
            lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_transform_rotation(bar, i == 0 ? 450 : 1350, 0); /* 0.1-deg units */
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    /* The centre: a 110 px tap box (a plain tap BACKSPACES) with the
     * running-entry / preview label + the commit hint + the SSID caption
     * above it (case kept -- the font now covers lowercase). */
    s_wheel_center = lv_obj_create(obj);
    lv_obj_set_size(s_wheel_center, UI_PX(110), UI_PX(110));
    lv_obj_align(s_wheel_center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_wheel_center, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wheel_center, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wheel_center, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* The running entry is the tap box's child; the box is a pure hit
     * target, not a view (no scroll -- the only child, the entry label,
     * fits the box). The wider "release commits" hint is a sibling. */
    lv_obj_clear_flag(s_wheel_center, LV_OBJ_FLAG_SCROLLABLE);
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

    /* The hint is the tap box's SIBLING, not a child: at 14 px
     * "release commits" runs wider than the 110 px box and a child would
     * be clipped at the box edge. */
    s_wheel_sub_label = lv_label_create(obj);
    lv_obj_set_style_text_font(s_wheel_sub_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_wheel_sub_label, "");
    lv_obj_set_size(s_wheel_sub_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_wheel_sub_label, LV_ALIGN_CENTER, 0, UI_PX(16));
    lv_obj_set_style_text_color(s_wheel_sub_label, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_label_set_text(s_wheel_ssid_label, s_wheel_ssid[0] ? s_wheel_ssid : "SELECT A NETWORK");
    settings_wheel_pw_label();
    settings_wheel_set_mode(0);
}

void tick_screen_settings_wifi_password() {}

/* --- manual time entry (P4): HH:MM + DD.MM.YY rollers ---------------
 *
 * The P4's wall clock is software (SNTP). This screen is the offline
 * fallback for a clock it could not sync: when the time is untrusted
 * (dimmed, and no alarm may fire) the user sets the wall clock by hand.
 * Two roller rows, the same look as the other settings value displays:
 * the time row (hour / minute, the 96 px clock font, at the home clock
 * label's position -- the home label shows no seconds, so neither does
 * this screen; a commit sets the seconds to 0) and the date row (day .
 * month . year, the 48 px font, the year as a two-digit 2000+YY).
 * Releasing ANY roller OR the sub-screen X exit commits the whole value:
 * clock_set_time (mktime / settimeofday + the alarm one-shot re-arm),
 * the trusted flag up (undim + the alarm fire gate), and the moon
 * re-bake when the calendar day changes (the day tick). The rollers
 * re-seed from the wall clock on every entry. Reached by the settings
 * hub's clock icon, which the P4 adds and the S3 does not (its time
 * comes from the PCF85063A); the screen itself is shared, so the S3
 * build stays structurally identical. */

/* A "%02d" option string per value in [from..to] (the roller rows). */
static void time_roller_options(char *buf, size_t n, int from, int to) {
    int pos = 0;
    for (int v = from; v <= to; v++) {
        pos += snprintf(buf + pos, n - pos, "%02d\n", v);
    }
}

static void settings_time_screen_refresh(void) {
    if (!objects.settings_time_roller_h) {
        return;
    }
    clock_time_t t;
    if (clock_sntp_get_system_time(&t) != ESP_OK) {
        return; /* No clock yet; keep the previous seed. */
    }
    /* The hour/minute and year option strings start at "00" (index =
     * value); the day and month start at "01" (index = value - 1). */
    lv_roller_set_selected(objects.settings_time_roller_h, t.hour, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_time_roller_mi, t.minute, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_time_roller_d, t.day - 1, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_time_roller_m, t.month - 1, LV_ANIM_OFF);
    lv_roller_set_selected(objects.settings_time_roller_y, (t.year - 2000) % 100, LV_ANIM_OFF);
}

/* Commit the whole value from the current roller positions.
 * clock_set_time writes the wall clock and re-arms the software alarm
 * one-shot from it; the trusted flag lifts the unsynced dim and opens
 * the alarm fire gate; the day tick re-bakes the moon if the date (and
 * thus the phase) changed. Seconds are 0 (this screen has no seconds
 * roller). Called on a roller RELEASED and by the X exit (both stay off
 * the drag path). */
static void settings_time_commit_value(void) {
    if (!objects.settings_time_roller_h) {
        return;
    }
    clock_time_t t;
    t.year = 2000 + settings_roller_value(objects.settings_time_roller_y);
    t.month = settings_roller_value(objects.settings_time_roller_m);
    t.day = settings_roller_value(objects.settings_time_roller_d);
    t.hour = settings_roller_value(objects.settings_time_roller_h);
    t.minute = settings_roller_value(objects.settings_time_roller_mi);
    t.second = 0;
    esp_err_t r = clock_set_time(&t);
    if (r == ESP_OK) {
        clock_sntp_set_time_trusted(true);
        moon_ui_day_tick();
        ESP_LOGW("settings_time", "manual time %04d-%02d-%02d %02d:%02d:%02d -> trusted", t.year, t.month, t.day,
                 t.hour, t.minute, t.second);
    } else {
        ESP_LOGW("settings_time", "manual time %04d-%02d-%02d %02d:%02d:%02d rejected: %s", t.year, t.month, t.day,
                 t.hour, t.minute, t.second, esp_err_to_name(r));
    }
}

/* Commit on a roller RELEASED (the set + re-arm stay off the drag path). */
static void event_handler_cb_settings_time_commit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    settings_time_commit_value();
}

/* The X exit commits too: pressing it is the user's "done" gesture, so
 * the value they scrolled to must not be dropped. It fires in the same
 * click event as the switch (the roller objects outlive the unload, so
 * the commit reads valid positions in either callback order). */
static void event_handler_cb_settings_time_exit(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        settings_time_commit_value();
    }
}

/* The two separator glyphs between the rollers: a `:` in the time row
 * (the 96 px clock font carries it) and a `.` in the date row (the
 * P4's 48 px numbers font is regenerated with the dot; the S3 build
 * never reaches this screen, so its digit-only font is untouched).
 * Non-clickable, so touches fall through to the rollers. */
static void settings_time_separator(lv_obj_t *parent, const char *text, const lv_font_t *font, int32_t x, int32_t y) {
    lv_obj_t *sep = lv_label_create(parent);
    lv_obj_set_style_text_font(sep, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(sep, text);
    lv_obj_set_size(sep, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(sep, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_text_color(sep, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
}

void create_screen_settings_time() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_time = obj;
    lv_obj_t *exit = settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    lv_obj_add_event_cb(exit, event_handler_cb_settings_time_exit, LV_EVENT_CLICKED, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The caption (this screen shows a value the clock label would
     * not: what the two rows ARE). */
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, "SET TIME / DATE");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -UI_PX(110));
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    /* The time row HH:MM (the 96 px clock font, at the home clock
     * label's position -- no seconds: the home label shows none either,
     * and adjusting them here would have no visible effect): hour -75,
     * minute +75, the `:` at centre. The 110 px rollers span y
     * -30±55 (absolute 148..258). */
    char hours[512], mins[600];
    time_roller_options(hours, sizeof(hours), 0, 23);
    time_roller_options(mins, sizeof(mins), 0, 59);
    /* The 140 px box (was 110): the Montserrat digits are PROPORTIONAL
     * (not tabular) -- the worst-case "44" pair advances 132 px at the
     * 96 px font / 204 px at 148, which the old 110 px box clipped
     * flat at its right edge. 140 leaves >= 4 px margin for the full
     * pair. */
    objects.settings_time_roller_h =
        settings_create_roller(obj, hours, &ui_font_montserrat_bold_96_numbers, UI_PX(110), -UI_PX(75), UI_PX(140));
    lv_obj_align(objects.settings_time_roller_h, LV_ALIGN_CENTER, -UI_PX(75), -UI_PX(30));
    settings_time_separator(obj, ":", &ui_font_montserrat_bold_96_numbers, 0, -UI_PX(30));
    objects.settings_time_roller_mi =
        settings_create_roller(obj, mins, &ui_font_montserrat_bold_96_numbers, UI_PX(110), UI_PX(75), UI_PX(140));
    lv_obj_align(objects.settings_time_roller_mi, LV_ALIGN_CENTER, UI_PX(75), -UI_PX(30));

    /* The date row DD.MM.YY (the 48 px font, below the time row): day
     * -75, month 0, year +75, the `.` at ±37. The 70 px rollers span y
     * +70±35 (absolute 268..338) -- clear of the bottom-centre exit
     * band (top y 342). */
    char days[128], months[64], years[400];
    time_roller_options(days, sizeof(days), 1, 31);
    time_roller_options(months, sizeof(months), 1, 12);
    time_roller_options(years, sizeof(years), 0, 99);
    objects.settings_time_roller_d =
        settings_create_roller(obj, days, &ui_font_montserrat_regular_48_numbers, UI_PX(70), -UI_PX(75), UI_PX(60));
    lv_obj_align(objects.settings_time_roller_d, LV_ALIGN_CENTER, -UI_PX(75), UI_PX(70));
    settings_time_separator(obj, ".", &ui_font_montserrat_regular_48_numbers, -UI_PX(37), UI_PX(70));
    objects.settings_time_roller_m =
        settings_create_roller(obj, months, &ui_font_montserrat_regular_48_numbers, UI_PX(70), 0, UI_PX(60));
    lv_obj_align(objects.settings_time_roller_m, LV_ALIGN_CENTER, 0, UI_PX(70));
    settings_time_separator(obj, ".", &ui_font_montserrat_regular_48_numbers, UI_PX(37), UI_PX(70));
    objects.settings_time_roller_y =
        settings_create_roller(obj, years, &ui_font_montserrat_regular_48_numbers, UI_PX(70), UI_PX(75), UI_PX(60));
    lv_obj_align(objects.settings_time_roller_y, LV_ALIGN_CENTER, UI_PX(75), UI_PX(70));

    /* Releasing any one roller commits the whole value. */
    const lv_obj_t *const rollers[] = {
        objects.settings_time_roller_h, objects.settings_time_roller_mi, objects.settings_time_roller_d,
        objects.settings_time_roller_m, objects.settings_time_roller_y,
    };
    for (int i = 0; i < 5; i++) {
        lv_obj_add_event_cb((lv_obj_t *)rollers[i], event_handler_cb_settings_time_commit, LV_EVENT_RELEASED, 0);
    }

    settings_time_screen_refresh();
}

void tick_screen_settings_time() {}

/* --- timezone / UTC offset (S3 + P4): one roller, all offsets --------
 *
 * Every 30-minute UTC offset (UTC-12:00 .. UTC+14:00) plus Europe/
 * Berlin (the Kconfig default, DST-aware) as the final entry -- the
 * option list is clock_tz.h's. Releasing the roller applies the zone
 * (clock_tz_apply: the P4 re-derives its local digits from the same
 * epoch; the S3 rewrites the PCF digits epoch-consistently), re-arms
 * the alarm arming, and persists the selection. Reached by the hub's
 * globe icon on BOTH boards. */

static char s_tz_options[800];

/* One option string per clock_tz entry ("UTC-12:00" .. "UTC+14:00",
 * "Europe/Berlin"). */
static void settings_tz_options(void) {
    int pos = 0;
    for (int i = 0; i < CLOCK_TZ_COUNT; i++) {
        const char *n = clock_tz_option_name(i);
        if (n)
            pos += snprintf(s_tz_options + pos, sizeof(s_tz_options) - pos, "%s\n", n);
    }
}

static void settings_tz_screen_refresh(void) {
    if (!objects.settings_tz_roller) {
        return;
    }
    int idx = settings_timezone_idx();
    if (idx < 0 || idx >= CLOCK_TZ_COUNT) {
        idx = CLOCK_TZ_BERLIN_IDX;
    }
    lv_roller_set_selected(objects.settings_tz_roller, idx, LV_ANIM_OFF);
}

/* Commit on RELEASED (the zone switch + re-arm + store save stay off
 * the drag path). The selected index IS the option index (the options
 * were generated in the index order), so no string parsing. */
static void settings_tz_commit_value(void) {
    int32_t sel = lv_roller_get_selected(objects.settings_tz_roller);
    if (sel < 0 || sel >= CLOCK_TZ_COUNT) {
        return;
    }
    esp_err_t r = clock_tz_apply((int)sel);
    if (r == ESP_OK) {
        settings_set_timezone_idx((int)sel);
        settings_store_save();
        ESP_LOGW("settings_tz", "timezone %s (idx %d) applied + saved", clock_tz_option_name((int)sel), (int)sel);
    } else {
        ESP_LOGW("settings_tz", "timezone idx %d rejected: %s", (int)sel, esp_err_to_name(r));
    }
}

/* Commit on RELEASED. */
static void event_handler_cb_settings_tz_commit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    settings_tz_commit_value();
}

void create_screen_settings_tz() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_tz = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The caption. */
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, "TIMEZONE / UTC OFFSET");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -UI_PX(110));
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    /* The offset roller (the 32 px ssid font -- full printable ASCII:
     * the entries carry letters, `+`, `:` and `/`, which the digits
     * fonts lack), centred and wide enough for "Europe/Berlin". */
    settings_tz_options();
    objects.settings_tz_roller =
        settings_create_roller(obj, s_tz_options, &ui_font_montserrat_regular_32_ssid, UI_PX(110), 0, UI_PX(380));
    lv_obj_align(objects.settings_tz_roller, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(objects.settings_tz_roller, event_handler_cb_settings_tz_commit, LV_EVENT_RELEASED, 0);

    settings_tz_screen_refresh();
}

void tick_screen_settings_tz() {}

/* --- alarm sound (S3 + P4): one roller over the four baked loops ----
 *
 * Both boards carry the ES8311 codec + speaker amp (the alarm_audio
 * module), so there is no board gate. The roller lists the four loop
 * names (alarm_audio's: two-tone / beep / bell / arpeggio); releasing
 * it applies + persists the selection and plays a preview. A speaker
 * button previews the currently selected loop. Reached by the hub's
 * speaker icon on BOTH boards. */

static void settings_alarm_sound_options(char *buf, size_t n) {
    int pos = 0;
    for (int i = 0; i < alarm_audio_count(); i++) {
        pos += snprintf(buf + pos, n - pos, "%s\n", alarm_audio_name(i));
    }
}

static void settings_alarm_sound_screen_refresh(void) {
    if (!objects.settings_alarm_sound_roller) {
        return;
    }
    int id = settings_alarm_sound_id();
    lv_roller_set_selected(objects.settings_alarm_sound_roller, (int32_t)id, LV_ANIM_OFF);
}

/* Commit on RELEASED (the persist stays off the drag path). The
 * selected index IS the sound id (the options were generated in the
 * id order), so no string parsing. The new selection is applied,
 * persisted, and previewed. */
static void event_handler_cb_settings_alarm_sound_commit(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    int32_t sel = lv_roller_get_selected(objects.settings_alarm_sound_roller);
    if (sel < 0 || sel >= alarm_audio_count()) {
        return;
    }
    alarm_audio_select((int)sel);
    settings_set_alarm_sound_id((int)sel);
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_alarm_sound", "alarm sound %d (%s) selected + saved -> %s", (int)sel, alarm_audio_name((int)sel),
             esp_err_to_name(r));
    alarm_audio_preview((int)sel);
}

/* The preview button: the currently selected loop, once. (A release on
 * the roller previews its own selection -- this is for re-hearing the
 * standing choice without touching the roller.) */
static void event_handler_cb_settings_alarm_sound_preview(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGW("settings_alarm_sound", "preview sound %d (%s)", alarm_audio_selected(),
             alarm_audio_name(alarm_audio_selected()));
    alarm_audio_preview(alarm_audio_selected());
}

void create_screen_settings_alarm_sound() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_alarm_sound = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The caption. */
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, "ALARM SOUND");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -UI_PX(110));
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    /* The sound-name roller (the 32 px ssid font), left of centre to
     * make room for the preview button on the same line. */
    char options[128];
    settings_alarm_sound_options(options, sizeof(options));
    objects.settings_alarm_sound_roller =
        settings_create_roller(obj, options, &ui_font_montserrat_regular_32_ssid, UI_PX(110), 0, UI_PX(260));
    lv_obj_align(objects.settings_alarm_sound_roller, LV_ALIGN_CENTER, -UI_PX(70), 0);
    lv_obj_add_event_cb(objects.settings_alarm_sound_roller, event_handler_cb_settings_alarm_sound_commit,
                        LV_EVENT_RELEASED, 0);

    /* The preview button: the solid speaker glyph (U+F028) in a
     * transparent 100 px box (the anim-toggle pattern), right of the
     * roller on the same line. */
    lv_obj_t *row = lv_obj_create(obj);
    lv_obj_set_size(row, UI_PX(100), UI_PX(100));
    lv_obj_align(row, LV_ALIGN_CENTER, UI_PX(150), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, event_handler_cb_settings_alarm_sound_preview, LV_EVENT_CLICKED, 0);

    lv_obj_t *glyph = lv_label_create(row);
    add_style_label_icon_solid(glyph);
    lv_label_set_text(glyph, GLYPH_SPEAKER);
    lv_obj_set_size(glyph, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(glyph, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(glyph, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);

    settings_alarm_sound_screen_refresh();
}

void tick_screen_settings_alarm_sound() {}

/* --- display base orientation (all three boards): the wall-mount trim
 * roller --------------------------------------------------------------
 *
 * The 90-degree-step orientation pipeline treats the pose held at boot
 * (or at the 2 s press resync) as HOME and rotates every screen root
 * by the Kconfig mount offset plus the persisted user trim. A clock
 * hung on a wall whose "up" is neither 0 nor a multiple of 90 degrees
 * needs that trim -- this screen is its editor (the hub's rotate
 * icon). A 0..359-degree house roller (settings_create_roller, the
 * alarm/time/pomodoro picker) IS the value display: it replaced the
 * old rim-handle drag, which was too fiddly to hit on a touch panel.
 * Scrolling is live -- the centre card tips by the same amount, that
 * is how the content will lean. RELEASED persists -- settings.txt +
 * action_orientation_set_user_base_deg re-pushes all 17 screen roots
 * right away, so the whole display tilts for real the way it will on
 * the wall. On the knob the dial steps the angle in 5-degree detents
 * and a tap commits through the roller's own RELEASED save. */

static lv_obj_t *s_display_roller;  /* the 0..359 degree picker (a house roller) */
static lv_obj_t *s_display_preview; /* the centre card that tips with the trim */
static int s_display_deg;           /* working value: degrees CW from 12 o'clock */

/* Sync the picker + the tilting card to the working angle. The card
 * tips the way the screen's CONTENT will lean (0.1-deg units, centre
 * pivot -- the setter ori_apply_v10 uses). The roller is skipped when
 * it already shows the value (the finger-flick path), so apply never
 * yanks the roller mid-scroll; the dial and the entry refresh are the
 * only callers that actually move it. */
static void settings_display_apply(void) {
    if (!s_display_roller) {
        return;
    }
    if ((int)lv_roller_get_selected(s_display_roller) != s_display_deg) {
        lv_roller_set_selected(s_display_roller, s_display_deg, LV_ANIM_OFF);
    }
    lv_obj_set_style_transform_rotation(s_display_preview, (int32_t)s_display_deg * 10, 0);
}

/* Scroll: the roller's VALUE_CHANGED moves the working angle live (the
 * centre card tips for real as you roll; the screen itself re-pushes on
 * RELEASED). A no-op when the value did not change. */
static void event_handler_cb_settings_display_roller(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    int deg = (int)lv_roller_get_selected(s_display_roller);
    if (deg == s_display_deg) {
        return;
    }
    s_display_deg = deg;
    settings_display_apply();
}

/* Persist on RELEASED (the SPIFFS write + the root re-push stay off
 * the drag path). The trim lands immediately -- the screen under the
 * finger tilts for real, which is the whole point of the screen. */
static void event_handler_cb_settings_display_save(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    settings_set_display_rotation_deg(s_display_deg);
    esp_err_t r = settings_store_save();
    ESP_LOGW("settings_display", "display base trim %d deg saved -> %s", s_display_deg, esp_err_to_name(r));
    action_orientation_set_user_base_deg(s_display_deg);
}

static void settings_display_screen_refresh(void) {
    if (!s_display_roller) {
        return;
    }
    s_display_deg = settings_display_rotation_deg();
    settings_display_apply();
}

void create_screen_settings_display() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings_display = obj;
    settings_sub_screen_prepare(obj, SCREEN_ID_SETTINGS_HUB);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* The caption. */
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &ui_font_montserrat_regular_32_ssid, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(caption, "ZERO ANGLE");
    lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, -UI_PX(140));
    lv_obj_set_style_text_color(caption, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_CLICKABLE);

    /* The picker: a 0..359-degree house roller (the alarm/time/pomodoro
     * settings_create_roller -- the roller IS the value display, so no
     * separate "N DEG" readout). This replaces the old rim-handle drag,
     * which was too fiddly to hit on a touch panel. Scrolling moves the
     * working angle live (VALUE_CHANGED tips the card); RELEASED
     * persists the trim + re-pushes every screen root, so the whole
     * display tilts for real the way it will on the wall. Upper-centre,
     * above the preview card. */
    static char opts[360 * 5]; /* "0\n".."359\n" */
    int pos = 0;
    for (int d = 0; d < 360; d++) {
        pos += snprintf(opts + pos, sizeof(opts) - pos, "%d\n", d);
    }
    s_display_roller =
        settings_create_roller(obj, opts, &ui_font_montserrat_regular_48_numbers, UI_PX(66), 0, UI_PX(120));
    lv_obj_align(s_display_roller, LV_ALIGN_CENTER, 0, -UI_PX(40));
    lv_obj_add_event_cb(s_display_roller, event_handler_cb_settings_display_roller, LV_EVENT_VALUE_CHANGED, 0);
    lv_obj_add_event_cb(s_display_roller, event_handler_cb_settings_display_save, LV_EVENT_RELEASED, 0);

    /* The preview: a grey-outlined card whose WHITE TIP BAR (a child,
     * top-centre) is "up". The card tips with the trim the way the
     * screen content will; its centre stays put (centre pivot). A live
     * hint of the lean while the finger is on the roller. Lower-centre. */
    s_display_preview = lv_obj_create(obj);
    lv_obj_set_size(s_display_preview, UI_PX(128), UI_PX(128));
    lv_obj_align(s_display_preview, LV_ALIGN_CENTER, 0, UI_PX(78));
    lv_obj_set_style_bg_opa(s_display_preview, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_display_preview, UI_PX(16), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_display_preview, UI_PX(2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(s_display_preview, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_display_preview, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_display_preview, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_display_preview, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_display_preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(s_display_preview, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(s_display_preview, LV_PCT(50), 0);

    lv_obj_t *tip = lv_obj_create(s_display_preview);
    lv_obj_set_size(tip, UI_PX(64), UI_PX(6));
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(tip, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tip, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(tip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(tip, LV_OBJ_FLAG_CLICKABLE);

    settings_display_screen_refresh();
}

void tick_screen_settings_display() {}

/* The shared clock widget's alarm weekday prefix row (the prefix
 * label is its first child). ONE row for the one widget: the five
 * moon screens alias the same objects (layout contract 2), so there
 * is no per-screen set any more. Not part of objects_t: the layout
 * contract names only the slot row at 18+1 and the alarm label at
 * 18+2; the prefix row is internal to the widget. */
static lv_obj_t *s_alarm_prefix;

/* The shared clock widget's HH:MM pair labels (see
 * create_user_widget_clock), one per digit PAIR (0 = HH, 1 = MM) --
 * the settings time screen's model: the roller scrolls the whole
 * value as one block inside a fixed-width box, so the pair here
 * scrolls as one 2-line block inside its cell. ONE set for the one
 * widget (the 2026-11-04 bugreport's "single shared widget": the five
 * moon screens used to each carry their own labels and only the
 * active screen's set was ticked, so a wake showed the pre-wake value
 * until the next minute tick; now the one set is updated at the
 * minute tick and at every screen switch, so it is always current
 * wherever it is attached). Each pair label sits in a one-line
 * CLIPPING cell so the PAIR ROLL (odometer slide, see the block
 * above tick_user_widget_clock) scrolls the label inside a one-line
 * window. */
static lv_obj_t *s_clock_pair_label[2];

/* Each pair's one-line height (the roll's translate distance + the
 * cell height), captured at creation from the digits font. */
static int32_t s_clock_line_h[2];

/* One of the next-alarm row's HALF-BOXES (the centre-anchored pair,
 * 2026-11-05 v2): fixed w x content height, transparent, ZERO
 * padding (the theme's 20 px base pad would shift the child's ink),
 * a flex row that places its single label at the box's inner edge
 * (END = the box's right edge, START = its left edge) and
 * cross-centres it. The two halves meet at the row's centre (the
 * colon), so each label's inner edge is pinned to the centre. */
static lv_obj_t *alarm_row_half(lv_obj_t *row, int32_t w, bool inner_left) {
    lv_obj_t *box = lv_obj_create(row);
    lv_obj_set_size(box, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(box, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(box, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(box, inner_left ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_END,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(box, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return box;
}

/* One clock row cell (the HH / colon / MM box): fixed w x one line,
 * transparent, non-scrollable, CLIPPING (the default overflow) --
 * that clip is the one-line WINDOW the pair roll scrolls inside.
 * No layout inside: the pair label is created at the cell origin
 * (0,0) with a FIXED width (the cell width, the settings roller's
 * fixed box) and a content HEIGHT (one line at rest, the 2-line
 * strip during a roll -- the height follows the content, so the
 * label's own clip follows the strip, and the CELL clips the strip
 * to the one-line window). Fixed width = no horizontal re-layout
 * ever: every text change is centred inside the same box, exactly
 * like the settings roller (the proportional font's pair width
 * varies, "11" 116 px vs "44" 204 px at 720, but the box does not).
 * The roll-start comment carries the translate arithmetic.
 * ZERO PADDING: the theme's base style pads 20 px (all sides); the
 * label is created at the content origin at the cell's FIXED width,
 * so any padding shifts it right+down and the cell's clip cuts its
 * right/bottom 20 px (the "21:26" photo: digits cut at the bottom,
 * the colon cut at the right). */
static lv_obj_t *clock_row_cell(lv_obj_t *row, int32_t w, int32_t line_h) {
    lv_obj_t *cell = lv_obj_create(row);
    lv_obj_set_size(cell, w, line_h);
    lv_obj_set_style_bg_opa(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(cell, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    return cell;
}

/* The ONE shared clock widget (layout contract 2): full-screen flex
 * column (time on top, the alarm row bottom-centred). The alarm row
 * is CENTER-anchored (2026-11-05 v2): two half-width boxes meet at
 * the row's centre (= the time row's centre = the colon), the LEFT
 * box right-aligns the weekday prefix (32 px range font, grey, empty
 * within 24 h of the fire) and the RIGHT box left-aligns the HH:MM
 * label (48 px) -- each label's inner edge is pinned to the centre
 * (see the row block + alarm_row_half below). Called exactly once
 * (BASE 18, the modem-sleep screen); the other four moon screens'
 * bases ALIAS objects[18/19/20] (the last block below), so all five
 * screens read and tick the same three objects -- the clock is always
 * current on whichever screen it is attached to. */
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
                    // label-clock-time: the HH:MM row as TWO PAIR CELLS +
                    // the COLON cell -- the settings time screen's
                    // geometry (its roller box is 140 px, the digit pair
                    // centred inside), and its MOTION: the roller scrolls
                    // the whole value as one block, so each pair scrolls
                    // as one 2-line block inside its cell (the unchanged
                    // digit appears on both lines and does not move --
                    // a real split-flap pair). The 96 px numbers font is
                    // PROPORTIONAL ("1" advances 37.6 px, "4" 66.1 px at
                    // 466), so the earlier per-digit 67 px slots left
                    // each glyph centred in its own wide box -- "16:16"
                    // read as scattered, "the numbers are not fitting
                    // in the boxes" (the 2026-11-04 cheese photo).
                    // Each pair cell (136 px -- 4 px inside the worst
                    // case, the "44" pair that advances 132 px at 466
                    // / 204 px at the P4 148 font; the earlier 140 px
                    // box left a ~32 px margin at narrow pairs like
                    // "11", the "too much margin between the numbers
                    // and the colon" of the 2026-11-05 photo) holds ONE
                    // label at the cell's FIXED width and a content
                    // height, text centred in the box: the pair reads
                    // tight, and because the width is fixed a digit
                    // change can never move the pair (or the other
                    // pair) sideways. The colon cell is 30 px (advance
                    // 25.1 px at 466 / 38.75 px at the 148 font). The
                    // row's total width is constant (136 + 30 + 136 px
                    // at 466). objects[BASE+1] stays the row: the
                    // tick's unsynced-dim lv_obj_set_style_opa targets
                    // it and the style cascades to the children.
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 1] = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        /* The one-line height of the digits font = the cell
                         * height = the pair roll's translate distance. */
                        int32_t line_h = lv_font_get_line_height(&ui_font_montserrat_bold_96_numbers);
                        /* The three row cells: the HH pair, the colon, the
                         * MM pair (child order = left to right). Each PAIR
                         * cell holds its ONE pair label (fixed width = the
                         * cell width -- the settings roller's fixed box --
                         * content height, text centred): at rest a single
                         * line; during a roll the 2-line old/new strip,
                         * scrolled one line in the direction of change (the
                         * settings roller's motion, see the roll block
                         * above tick_user_widget_clock). The cell CLIPS the
                         * strip to the one-line window (default overflow).
                         * The colon cell holds its plain label the same way. */
                        /* Pair cells 136 px (466): the worst-case "44"
                         * pair advances 132 px (the settings roller's
                         * 140 px box left a ~4 px-per-side margin at
                         * worst case and a ~32 px one at "11" -- the
                         * "too much margin between the numbers and the
                         * colon" of the 2026-11-05 photo). The colon
                         * cell 30 px: its advance is 25.1 px (466) /
                         * 38.75 px (the P4 148 font) -- 26 px left the
                         * ink ~0.6 px from the cell edge at 720. */
                        lv_obj_t *cell_hh = clock_row_cell(obj, UI_PX(136), line_h);
                        lv_obj_t *cell_colon = clock_row_cell(obj, UI_PX(30), line_h);
                        lv_obj_t *cell_mm = clock_row_cell(obj, UI_PX(136), line_h);
                        s_clock_pair_label[0] = lv_label_create(cell_hh);
                        s_clock_pair_label[1] = lv_label_create(cell_mm);
                        lv_obj_t *label_colon = lv_label_create(cell_colon);
                        for (int i = 0; i < 2; i++) {
                            lv_obj_t *label = s_clock_pair_label[(size_t)i];
                            lv_obj_set_size(label, UI_PX(136), LV_SIZE_CONTENT);
                            /* ANCHORED toward the colon, not centred
                             * (tabular figures): centring leaves a gap
                             * to the colon that varies with the digits
                             * ("21" 145 px advance vs "44" 204 px at
                             * 720 -- 64 px vs 9 px of ink-gap, the
                             * "too much margin between the numbers and
                             * the colon" of the 2026-11-05 photo).
                             * The HH pair is RIGHT-aligned (its right
                             * edge, colon-side, stays put for every
                             * hour) and the MM pair LEFT-aligned; the
                             * variable space falls on the outside edge
                             * where it reads as the clock's centre
                             * margin. The roll strip (old\nnew) keeps
                             * both lines aligned the same way. */
                            lv_obj_set_style_text_align(label, i == 0 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT,
                                                        LV_PART_MAIN | LV_STATE_DEFAULT);
                            add_style_label_bold(label);
                            s_clock_line_h[(size_t)i] =
                                lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
                            lv_label_set_text(label, "");
                        }
                        lv_obj_set_size(label_colon, UI_PX(30), LV_SIZE_CONTENT);
                        lv_obj_set_style_text_align(label_colon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                        add_style_label_bold(label_colon);
                        lv_label_set_text(label_colon, ":");
                    }
                }
                {
                    // label-clock-alarm-row: the prefix + HH:MM pair,
                    // CENTER-anchored (2026-11-05 v2) -- the row is
                    // exactly as wide as the HH:MM row above (the same
                    // 136+30+136 px cells PLUS the same 2 x flex
                    // pad_row the theme puts between the time row's
                    // cells -- read from the time row, so it tracks the
                    // theme's DPI gap at 466 and 720), so its centre
                    // = the time row's centre = the colon = the screen
                    // centre. Two half-boxes meet at the centre: the
                    // LEFT half right-aligns the weekday prefix (its
                    // RIGHT edge pinned at the centre -- the ragged
                    // edge is the left one) and the RIGHT half
                    // left-aligns the time (its LEFT edge pinned at
                    // the centre -- the ragged edge is the right one).
                    // Each element keeps its anchor through clock
                    // adjustments: a digit change grows the time
                    // rightward, the prefix appears/disappears
                    // leftward, and neither ever crosses the colon.
                    lv_obj_t *obj = lv_obj_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_t *time_row = ((lv_obj_t **)&objects)[startWidgetIndex + 1];
                    int32_t alarm_w = UI_PX(136) + UI_PX(30) + UI_PX(136) + 2 * lv_obj_get_style_pad_row(time_row, 0);
                    lv_obj_set_size(obj, alarm_w, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    /* The theme's flex gap (pad_column, DPI-scaled 10/11 px)
                     * would wedge 10 px between the two halves (and push
                     * them 5 px past the row's edges): zero it so the
                     * halves MEET at the centre; the 8 px label pads are
                     * the gap to the centre. */
                    lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    s_alarm_prefix = obj;
                    lv_obj_t *half_l = alarm_row_half(obj, alarm_w / 2, false);
                    lv_obj_t *half_r = alarm_row_half(obj, alarm_w - alarm_w / 2, true);
                    {
                        // label-clock-alarm-prefix (2-letter weekday,
                        // e.g. "Mo"; empty when the fire is <= 24 h)
                        lv_obj_t *obj = lv_label_create(half_l);
                        lv_obj_set_pos(obj, 0, 0);
                        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                        lv_obj_set_style_text_font(obj, &ui_font_montserrat_regular_32_range,
                                                   LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_obj_set_style_text_color(obj, SETTINGS_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
                        /* The trailing pad is the gap to the centre
                         * (the time's own leading pad mirrors it). */
                        lv_obj_set_style_pad_right(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_label_set_text(obj, "");
                    }
                    {
                        // label-clock-alarm
                        lv_obj_t *obj = lv_label_create(half_r);
                        ((lv_obj_t **)&objects)[startWidgetIndex + 2] = obj;
                        lv_obj_set_pos(obj, 0, 0);
                        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                        add_style_label_regular(obj);
                        /* The leading pad is the gap to the centre. */
                        lv_obj_set_style_pad_left(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
                        lv_label_set_text(obj, "");
                    }
                }
            }
        }
    }
    /* The four other moon screens ALIAS the same three objects at
     * their own bases (layout contract 2): one widget, five screens.
     * The <screen>_clock containers (17/22/27/31/36) stay distinct
     * (the attach targets + the unclickable_recursive targets). */
    lv_obj_t *w[3] = {objects.modem_sleep_clock__clock, objects.modem_sleep_clock__label_clock_time,
                      objects.modem_sleep_clock__label_clock_alarm};
    /* __clock slots: +1 each when settings_alarm_sound joined the screen
     * roots and +1 again when settings_display joined (the flex is now at
     * objects[18], the slot row [19], the alarm [20]); the four screens
     * alias the shared widget at their __clock. */
    int bases[4] = {23, 28, 32, 37};
    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < 3; i++) {
            ((lv_obj_t **)&objects)[bases[b] + i] = w[i];
        }
    }
}

/* Refresh the alarm prefix label from the var (empty within 24 h of
 * the fire). The centre-anchored row holds the label inside its LEFT
 * half-box (row child 0), so the label is one level down. Returns
 * whether the text changed. */
static bool tick_alarm_prefix(lv_obj_t *row) {
    lv_obj_t *prefix = lv_obj_get_child(lv_obj_get_child(row, 0), 0);
    const char *new_val = get_var_alarm_prefix();
    if (strcmp(new_val, lv_label_get_text(prefix)) == 0)
        return false;
    tick_value_change_obj = prefix;
    lv_label_set_text(prefix, new_val);
    tick_value_change_obj = NULL;
    return true;
}

#if !CONFIG_CLOCK_USE_PCF85063A
/* Unsynced-clock dim (P4): the trust flag is global; ONE shared clock
 * widget means the applied state is a single bool -- the widget brightens
 * on the sync wherever it is attached (the S3's PCF clock is
 * self-trusting; the block below is P4-only). */
static bool s_clock_unsynced_dim;
#endif

/* ---- HH:MM PAIR ROLL (odometer) -- 2026-11-04 bugreport, item 6 ----
 *
 * The minute tick no longer hard-swaps the time: each digit PAIR (the
 * HH and MM pair labels, one per pair cell -- see
 * create_user_widget_clock) is swapped to a TWO-LINE strip (old pair
 * over new pair) and SCROLLED one line in the direction of change --
 * the SETTINGS ROLLER's motion, the user's reference ("peak into the
 * settings screens how the clock is done there"): the roller scrolls
 * the whole value as one block, so 12:35 -> 12:36 rolls the MM pair
 * UP out of its window as "36" enters from below (the unchanged "3"
 * sits on both lines -- it does not move), and 12:36 -> 12:35 rolls
 * the pair DOWN as "35" enters from above. The scroll uses the
 * roller's ease-out path (fast start, gentle settle -- a physical
 * wheel, not a ramp). The one-line CELL clips the strip, so no
 * adjacent digit ever peeks into the window: at rest each pair shows
 * exactly its two digits, tight and centred in its fixed box -- and
 * the fixed box width means a digit change can never move the pair
 * sideways (the proportional font's pair width varies, "11" 78 px vs
 * "44" 132 px at 466).
 *
 * Geometry: the label sits at the cell origin with a fixed width and
 * a CONTENT height, so the strip is top-aligned in the one-line
 * window (cell 0..line_h): line 1 at translate 0, line 2 at
 * +line_height (below the window). The roll keeps the OLD line in the
 * window: up (new pair enters from below) = old over new, the old
 * line is line 1 -- pre-translated to 0 (visually identical to the
 * resting pair, swapped in the same frame) and animated to
 * -line_height; down (new pair enters from above) = new over old,
 * the old line is line 2 -- in the window only at -line_height, so
 * pre-translated there and animated to 0. The settle rewrites the
 * label to the one-line target at translate 0 in the same frame the
 * animation completes (LVGL renders after the timer handler) -- the
 * target holds its on-screen position, no flicker.
 *
 * Only single-step wheel changes animate: every digit that changes
 * must move delta 1 (up) or 9 (down), all in the SAME direction
 * (12:35 -> 12:36: the "5" alone changes, delta 1 -> up). A larger
 * jump (a manual time entry far from the current minute, 59 -> 00)
 * and any non-digit character hard-swap instead.
 *
 * In-flight guard: a screen-switch attach can re-run the tick
 * mid-roll. If it is converging on the LATEST pair it is left to
 * finish; if a NEWER pair arrived, the roll is dropped (the label
 * settles to the latest at translate 0) and no second animation
 * starts. */
/* Roll gate (UI_CLOCK_ROLL_ENABLE, default n -- the user asked for the
 * minute-change animation off 2026-11-13): seed mirrors the Kconfig 'n'
 * for Kconfig-less builds (the host sim compiles this file); a target
 * 'y' arrives as a real =1. Disabled, every pair change takes the
 * existing hard-swap branch below. */
#ifndef CONFIG_UI_CLOCK_ROLL_ENABLE
#define CONFIG_UI_CLOCK_ROLL_ENABLE 0
#endif

#define CLOCK_ROLL_MS 300 /* one pair's roll time (the roller's default) */

/* Per-pair in-flight flag + the pair the label is rolling toward.
 * ONE set for the one widget. */
static int s_clock_rolling[2];
static char s_clock_roll_target[2][3]; /* "NN" + NUL */

static void clock_roll_done(lv_anim_t *a);

static int clock_pair_index(lv_obj_t *label) {
    for (int i = 0; i < 2; i++) {
        if (s_clock_pair_label[(size_t)i] == label)
            return i;
    }
    return -1;
}

static void clock_roll_exec(void *obj, int32_t y) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, y, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* The roll finished (LVGL task, from the animation timer). The pair
 * label is at the strip's end translate with the NEW pair in the
 * window: settle to the resting state -- one line (the rolled-in pair)
 * at translate 0. Both changes land in the same frame (LVGL renders
 * after the timer handler returns), so the pair holds its on-screen
 * position -- no flicker. */
static void clock_roll_done(lv_anim_t *a) {
    lv_obj_t *label = (lv_obj_t *)a->var;
    int i = clock_pair_index(label);
    if (i < 0)
        return;
    lv_obj_set_style_translate_y(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    tick_value_change_obj = label;
    lv_label_set_text(label, s_clock_roll_target[(size_t)i]);
    tick_value_change_obj = NULL;
    s_clock_rolling[i] = 0;
}

void tick_user_widget_clock(int startWidgetIndex) {
#if !CONFIG_CLOCK_USE_PCF85063A
    // Dim the time while the P4 clock is unsynced (no SNTP since boot and
    // no trusted manual entry) so an approximate value reads as
    // approximate; the label brightens on the sync (the flag flips in the
    // SNTP callback; this converges on the next tick).
    {
        bool dim = !clock_sntp_time_trusted();
        if (dim != s_clock_unsynced_dim) {
            s_clock_unsynced_dim = dim;
            lv_obj_set_style_opa(((lv_obj_t **)&objects)[startWidgetIndex + 1], dim ? LV_OPA_50 : LV_OPA_COVER, 0);
        }
    }
#endif
    {
        /* The HH:MM row (objects[17+1], the shared set): each digit
         * PAIR that changes ROLLS (odometer slide, the settings
         * roller's motion, see the block above), not a hard swap. */
        const char *new_val = get_var_clock_label();
        for (int i = 0; i < 2; i++) {
            lv_obj_t *label = s_clock_pair_label[(size_t)i];
            /* new_val is "HH:MM": the HH pair at 0,1, the MM pair at
             * 3,4 (index 2 is the colon). */
            int a = i == 0 ? 0 : 3;
            char new_pair[3] = {new_val[a], new_val[a + 1], 0};
            if (s_clock_rolling[i]) {
                if (strcmp(s_clock_roll_target[(size_t)i], new_pair) == 0)
                    continue; /* a roll toward the LATEST pair is in
                               * flight: let it finish (its settle
                               * rewrites the label) */
                /* A NEWER pair arrived mid-roll: settle the label to
                 * the latest (one line at translate 0) -- no second
                 * animation on one label. */
                lv_anim_delete(label, clock_roll_exec);
                lv_obj_set_style_translate_y(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                s_clock_rolling[i] = 0;
                tick_value_change_obj = label;
                lv_label_set_text(label, new_pair);
                tick_value_change_obj = NULL;
                continue;
            }
            const char *old = lv_label_get_text(label);
            if (old[0] == 0) {
                /* A pair that has never shown digits (the first tick):
                 * there is no old pair to roll out -- hard-set the new
                 * one (rolling "from" empty would build a strip the
                 * first line of which reads as empty: a blank flash). */
                tick_value_change_obj = label;
                lv_label_set_text(label, new_pair);
                tick_value_change_obj = NULL;
                continue;
            }
            if (old[0] == new_pair[0] && old[1] == new_pair[1])
                continue;
            if (!CONFIG_UI_CLOCK_ROLL_ENABLE) {
                /* Roll off (family default): hard-swap every change --
                 * the SAME branch the mixed-jump hard-swap takes. (The
                 * arc itself never animated: LVGL's
                 * lv_arc_set_value is instant.) */
                tick_value_change_obj = label;
                lv_label_set_text(label, new_pair);
                tick_value_change_obj = NULL;
                continue;
            }
            /* The wheel delta on the mod-10 digit circles: every
             * CHANGED digit must move delta 1 (up, new below the old)
             * or delta 9 (down, new above), all in the SAME direction
             * -- 12:35 -> 12:36: the "3" is unchanged, the "5" moves
             * delta 1, so the pair rolls UP. Anything else (a larger
             * jump, e.g. a manual time entry far from the current
             * minute, or a mixed direction) hard-swaps. */
            int dir = 0;
            for (int j = 0; j < 2; j++) {
                if (old[j] < '0' || old[j] > '9' || new_pair[j] < '0' || new_pair[j] > '9') {
                    dir = -1;
                    break;
                }
                if (old[j] == new_pair[j])
                    continue;
                int d = (new_pair[j] - old[j] + 10) % 10;
                if (d != 1 && d != 9) {
                    dir = -1;
                    break;
                }
                if (dir == 0)
                    dir = d;
                else if (dir != d) {
                    dir = -1;
                    break;
                }
            }
            if (dir < 0) {
                tick_value_change_obj = label;
                lv_label_set_text(label, new_pair);
                tick_value_change_obj = NULL;
                continue;
            }
            /* Build the two-line strip centred on the old pair. The
             * label tops the cell (line 1 at translate 0, line 2 at
             * +line_height inside the 1-line window at cell 0..line_h):
             * UP (dir 1) = old over new -- the old line (line 1) is in
             * the window at translate 0 (visually identical to the
             * resting pair, swapped in this same frame), animated to
             * -line_height as the new line (line 2) enters from below;
             * DOWN (dir 9) = new over old -- the old line is line 2,
             * in the window only at translate -line_height (at +0 or
             * +line_height it sits below the window: a blank flash),
             * so pre-translate to -line_height and animate to 0 as the
             * new line (line 1) enters from above. The roller's
             * ease-out path. */
            char strip[6];
            if (dir == 1)
                snprintf(strip, sizeof strip, "%s\n%s", old, new_pair);
            else
                snprintf(strip, sizeof strip, "%s\n%s", new_pair, old);
            int32_t lh = s_clock_line_h[(size_t)i];
            int32_t from = dir == 1 ? 0 : -lh;
            int32_t to = dir == 1 ? -lh : 0;
            tick_value_change_obj = label;
            lv_label_set_text(label, strip);
            lv_obj_set_style_translate_y(label, from, LV_PART_MAIN | LV_STATE_DEFAULT);
            tick_value_change_obj = NULL;
            s_clock_rolling[i] = 1;
            strcpy(s_clock_roll_target[(size_t)i], new_pair);
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, label);
            lv_anim_set_values(&anim, from, to);
            lv_anim_set_duration(&anim, CLOCK_ROLL_MS);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&anim, clock_roll_exec);
            lv_anim_set_completed_cb(&anim, clock_roll_done);
            lv_anim_start(&anim);
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
    {
        tick_alarm_prefix(s_alarm_prefix);
    }
}

/* The shared clock widget's per-screen PARENT container (layout
 * contract 2): the <screen>_clock container on each moon screen. The
 * settings screens have no moon (and no clock) -- the widget stays
 * where it was, computed but not rendered. Returns NULL for the
 * non-moon screens. */
static lv_obj_t *clock_attach_parent(enum ScreensEnum screenId) {
    switch (screenId) {
    case SCREEN_ID_MODEM_SLEEP:
        return objects.modem_sleep_clock;
    case SCREEN_ID_WIFI:
        return objects.wifi_clock;
    case SCREEN_ID_LIGHT_SLEEP:
        return objects.light_sleep_clock;
    case SCREEN_ID_DEEP_SLEEP:
        return objects.deep_sleep_clock;
    case SCREEN_ID_ALARM:
        return objects.alarm_clock;
    default:
        return 0;
    }
}

/* The loadScreen hook (LVGL task; called BEFORE the screen switch's
 * atomic flush, so the refreshed digits are part of that one frame).
 * Reparents the ONE shared clock widget onto the new moon screen and
 * refreshes its vars from the RTC, so the switch -- the wake in
 * particular -- shows the current minute immediately instead of the
 * pre-wake value held by the last-ticked screen's labels (the
 * "quick adjustment" on wake, the 2026-11-04 bugreport). The widget
 * reparents, not re-creates: its style, position and labels carry over
 * (a reparent to a fresh container keeps the full-screen layout). */
void ui_clock_attach_to(enum ScreensEnum screenId) {
    lv_obj_t *parent = clock_attach_parent(screenId);
    if (!parent || !objects.modem_sleep_clock__clock) {
        return; /* a non-moon screen (keep the widget where it was) or
                 * before the widget exists (pre create_screens) */
    }
    if (lv_obj_get_parent(objects.modem_sleep_clock__clock) != parent)
        lv_obj_set_parent(objects.modem_sleep_clock__clock, parent);
    /* Refresh the vars NOW (the minute tick normally does this; a
     * screen switch can land well before the next tick). The clock
     * label + seconds come from the wall clock; the alarm label +
     * weekday prefix from the scheduler cache (no I2C). */
    clock_time_t now;
    if (clock_get_time(&now) == ESP_OK) {
        /* The HH:MM/MM:SS format rule of the minute tick (action
         * clock_widget_label): in the demo mode the attach must not
         * fight the per-second MM:SS label. */
        action_clock_widget_label(now.hour, now.minute, now.second);
        set_var_clock_seconds(now.second);
    }
    action_update_alarm_display(NULL);
    /* Base derived from objects_t (OBJECTS_CLOCK_WIDGET_SLOT) — never
     * hand-pinned; see the screens.h contract. */
    tick_user_widget_clock(OBJECTS_CLOCK_WIDGET_SLOT);
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
    tick_screen_settings_time,
    tick_screen_settings_tz,
    tick_screen_settings_alarm_sound,
    tick_screen_settings_display,
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
    create_screen_settings_time();
    create_screen_settings_tz();
    create_screen_settings_alarm_sound();
    create_screen_settings_display();
}

#ifdef CONFIG_BOARD_KNOB
/* ===== P2 dial surface (knob-only) ====================================
 * The EC1 dial steps one per-screen edit cursor; a tap commits it. Every
 * commit re-sends the event the TOUCH path already handles (CLICKED on
 * icons/toggles/rows, RELEASED on rollers -- every screen's save path
 * rides exactly those two codes), so the dial duplicates no handler
 * logic and the clocks' behaviour is structurally untouched: the whole
 * block compiles only when CONFIG_BOARD_KNOB is set, which only the
 * knob's board-shim choice defines.
 *
 * Cursor rules (docs/implementation/knob-display.rst "Dial semantics"):
 *   hub          step the ring (icons + central exit), white = focused
 *   alarm list   step the three glyphs (focus ring), tap toggles/opens
 *   sleep        dial steps the minutes roller (the dusk toggle left
 *                this screen with the dusk, 2026-11-13)
 *   rollers      dial steps the FOCUSED roller; pressing another roller
 *                focuses it (twin-roller screens); tap = RELEASED
 *   day arc      (P3, alarm time) the two day handles join the focus
 *                set — pressing a handle focuses it; dial steps the
 *                focused endpoint day by day; tap = the handle's own
 *                RELEASED save (same as lifting a finger off a drag)
 *   wifi list    step the selection (reuses the row colour + sel bar;
 *                the native row CLICKED handler owns the cursor too)
 *   wheel        step the hot slot (reveals its sector -- the sticky-
 *                reveal the touch press does); tap commits the hot letter
 * The moon screens (home family) have no cursor: a dial turn there is
 * pure activity (knob-side un-dims via the ladder).
 *
 * LOCKING: screens_dial_step/commit touch LVGL objects. The dial task
 * takes the LVGL mutex around them (knob_dial.c); the background-tap
 * commit lands from the indev callback -- already inside the mutex --
 * and must call the commit WITHOUT re-locking (the mutex is recursive,
 * but the callers own the contract). */

static enum ScreensEnum s_dial_screen = SCREEN_ID_MODEM_SLEEP;
static bool s_dial_dirty;       /* a step happened since entry/commit */
static int s_dial_hub_focus;    /* 0..n-1 ring icons, n = central exit */
static int s_dial_alarm_focus;  /* 0 master, 1 pomodoro, 2 alarm-clock */
static int s_dial_roller_focus; /* 0/1 on the twin-roller screens */

static void dial_ring(lv_obj_t *o, bool on) {
    if (!o) {
        return;
    }
    lv_obj_set_style_border_width(o, on ? UI_PX(3) : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(o, SETTINGS_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(o, on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* The hub's white-glyph focus (grey = unfocused, like the alarm
 * screen's state colouring). */
static void dial_hub_apply(void) {
    const lv_obj_t *icons[8];
    enum ScreensEnum targets[8];
    int n = screens_hub_icons(icons, targets, 8);
    for (int i = 0; i < n; i++) {
        lv_obj_set_style_text_color((lv_obj_t *)icons[i], i == s_dial_hub_focus ? SETTINGS_WHITE : SETTINGS_GREY,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (objects.settings_hub_exit) {
        dial_ring(objects.settings_hub_exit, s_dial_hub_focus == n);
    }
}

static lv_obj_t *dial_alarm_obj(int idx) {
    switch (idx) {
    case 0:
        return objects.settings_alarm_master;
    case 1:
        return objects.settings_alarm_pomodoro;
    default:
        return objects.settings_alarm_clock;
    }
}

static void dial_alarm_apply(void) {
    for (int i = 0; i < 3; i++) {
        dial_ring(dial_alarm_obj(i), i == s_dial_alarm_focus);
    }
}

/* The roller pair of the current twin-roller screen (NULL pair
 * elsewhere). */
static void dial_roller_pair(enum ScreensEnum id, lv_obj_t **a, lv_obj_t **b) {
    *a = *b = NULL;
    switch (id) {
    case SCREEN_ID_SETTINGS_POMODORO:
        *a = objects.settings_pomodoro_roller_h;
        *b = objects.settings_pomodoro_roller_m;
        break;
    case SCREEN_ID_SETTINGS_ALARM_TIME:
        *a = objects.settings_alarm_time_roller_h;
        *b = objects.settings_alarm_time_roller_m;
        break;
    case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
        *a = s_sleep_roller[0];
        break;
    case SCREEN_ID_SETTINGS_DEEP_SLEEP:
        *a = s_sleep_roller[1];
        break;
    case SCREEN_ID_SETTINGS_TZ:
        *a = objects.settings_tz_roller;
        break;
    case SCREEN_ID_SETTINGS_ALARM_SOUND:
        *a = objects.settings_alarm_sound_roller;
        break;
    case SCREEN_ID_SETTINGS_DISPLAY:
        *a = s_display_roller;
        break;
    default:
        break;
    }
}

/* The day handle object of focus slot 2/3 (NULL elsewhere) -- the
 * alarm-time screen's two day-range handles. The display-trim screen
 * has NO handle now (its cursor is a real roller; see
 * dial_roller_pair), so it is not part of this set. */
static lv_obj_t *dial_handle_obj(enum ScreensEnum id, int focus) {
    if (id != SCREEN_ID_SETTINGS_ALARM_TIME || focus < 2) {
        return NULL;
    }
    return focus == 2 ? objects.settings_alarm_time_handle_start : objects.settings_alarm_time_handle_end;
}

static void dial_roller_apply(enum ScreensEnum id) {
    lv_obj_t *a = NULL, *b = NULL;
    dial_roller_pair(id, &a, &b);
    dial_ring(a, s_dial_roller_focus == 0);
    dial_ring(b, b != NULL && s_dial_roller_focus == 1);
    /* P3: the alarm-time screen's two extra focus slots (day handles). */
    lv_obj_t *h2 = dial_handle_obj(id, 2);
    dial_ring(h2, h2 != NULL && s_dial_roller_focus == 2);
    lv_obj_t *h3 = dial_handle_obj(id, 3);
    dial_ring(h3, h3 != NULL && s_dial_roller_focus == 3);
}

/* P3: press focus for the alarm-time day handles (drag path untouched:
 * this rides ALONGSIDE the drag's own PRESSED handler, user_data = the
 * handle index; focus 2 = start, 3 = end). */
static void event_handler_cb_dial_handle_press(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    s_dial_roller_focus = 2 + (int)(intptr_t)lv_event_get_user_data(e);
}

/* Focus defaults on entry (the dial task notices the switch itself via
 * ui_current_screen()). */
static void dial_screen_reset(enum ScreensEnum id) {
    s_dial_screen = id;
    s_dial_dirty = false;
    s_dial_roller_focus = 0;
    if (id == SCREEN_ID_SETTINGS_HUB) {
        s_dial_hub_focus = 0;
        dial_hub_apply();
    } else if (id == SCREEN_ID_SETTINGS_ALARM) {
        s_dial_alarm_focus = 0;
        dial_alarm_apply();
    } else if (id == SCREEN_ID_SETTINGS_WIFI) {
        /* Cursor = the screen's own s_wifi_selected (reset to -1 by the
         * entry refresh); the first step takes entry 0. On the knob it
         * additionally mirrors the roller's selection — there the
         * roller's window centre IS the cursor. */
    } else if (id == SCREEN_ID_SETTINGS_WIFI_PASSWORD) {
        settings_wheel_set_revealed(false, -1); /* full at-rest ring */
    } else {
        dial_roller_apply(id);
    }
}

/* Press focus for the twin-roller screens (user_data = slot). */
static void event_handler_cb_dial_roller_press(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    s_dial_roller_focus = (int)(intptr_t)lv_event_get_user_data(e);
}

void screens_dial_init(void) {
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_h, event_handler_cb_dial_roller_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)0);
    lv_obj_add_event_cb(objects.settings_pomodoro_roller_m, event_handler_cb_dial_roller_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)1);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_h, event_handler_cb_dial_roller_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)0);
    lv_obj_add_event_cb(objects.settings_alarm_time_roller_m, event_handler_cb_dial_roller_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)1);
    /* P3: the day handles join the focus set (alongside their drag
     * handlers — pressing a handle focuses it for the dial). */
    lv_obj_add_event_cb(objects.settings_alarm_time_handle_start, event_handler_cb_dial_handle_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)0);
    lv_obj_add_event_cb(objects.settings_alarm_time_handle_end, event_handler_cb_dial_handle_press, LV_EVENT_PRESSED,
                        (void *)(intptr_t)1);
    /* The display-trim screen's roller needs no press-focus handler:
     * it is a single-slot screen, focus 0 from dial_screen_reset (the
     * other single-roller screens work the same way). */
    dial_screen_reset(ui_current_screen());
}

static void dial_roller_step(lv_obj_t *r, int dir) {
    if (!r) {
        return;
    }
    uint32_t cnt = lv_roller_get_option_cnt(r);
    if (cnt == 0) {
        return;
    }
    int32_t sel = lv_roller_get_selected(r);
    if (sel < 0) {
        sel = 0;
    }
    int32_t next = (int32_t)(((int64_t)sel + dir + (int64_t)cnt * 4) % (int64_t)cnt); /* wrap, like an encoder */
    lv_roller_set_selected(r, (uint32_t)next, LV_ANIM_ON);
}

void screens_dial_step(int dir) {
    enum ScreensEnum id = ui_current_screen();
    if (id != s_dial_screen) {
        dial_screen_reset(id);
    }
    switch (id) {
    case SCREEN_ID_SETTINGS_HUB: {
        const lv_obj_t *icons[8];
        enum ScreensEnum targets[8];
        int n = screens_hub_icons(icons, targets, 8);
        const int n1 = n + 1; /* + the central exit */
        s_dial_hub_focus = (int)(((int64_t)s_dial_hub_focus + dir + (int64_t)n1 * 4) % n1);
        dial_hub_apply();
        s_dial_dirty = true;
        break;
    }
    case SCREEN_ID_SETTINGS_ALARM:
        s_dial_alarm_focus = (s_dial_alarm_focus + dir + 3) % 3;
        dial_alarm_apply();
        s_dial_dirty = true;
        break;
    case SCREEN_ID_SETTINGS_ALARM_TIME: {
        /* P3: four focus slots — hour, minute, day-range start, day-range
         * end (touch press picks the slot; the dial steps its VALUE). */
        if (s_dial_roller_focus < 2) {
            lv_obj_t *a = NULL, *b = NULL;
            dial_roller_pair(id, &a, &b);
            dial_roller_step(s_dial_roller_focus == 1 ? b : a, dir);
        } else {
            int *day = (s_dial_roller_focus == 2) ? &s_alarm_range_start : &s_alarm_range_end;
            *day = (int)(((int64_t)*day + dir + 7 * 4) % 7); /* wrap Mon..Sun */
            settings_alarm_time_apply_range();
        }
        dial_roller_apply(id);
        s_dial_dirty = true;
        break;
    }
    case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
    case SCREEN_ID_SETTINGS_DEEP_SLEEP:
    case SCREEN_ID_SETTINGS_POMODORO:
    case SCREEN_ID_SETTINGS_TZ:
    case SCREEN_ID_SETTINGS_ALARM_SOUND: {
        lv_obj_t *a = NULL, *b = NULL;
        dial_roller_pair(id, &a, &b);
        dial_roller_step(s_dial_roller_focus == 1 ? b : a, dir);
        dial_roller_apply(id);
        s_dial_dirty = true;
        break;
    }
    case SCREEN_ID_SETTINGS_DISPLAY: {
        /* The wall-mount trim: 5-degree detents (a finger drag on the
         * panels is free-form; the dial quantises). */
        s_display_deg = (int)(((int64_t)s_display_deg + (int64_t)dir * 5 + 360 * 8) % 360);
        settings_display_apply();
        dial_roller_apply(id);
        s_dial_dirty = true;
        break;
    }
    case SCREEN_ID_SETTINGS_WIFI: {
        if (!s_wifi_roller) {
            break;
        }
        wifi_ap_info_t aps[WIFI_SCAN_MAX_APS];
        const int n = wifi_scan_get_results(aps, WIFI_SCAN_MAX_APS);
        if (n <= 0 || lv_roller_get_option_cnt(s_wifi_roller) == 0) {
            break; /* still scanning / nothing found: nothing to roll */
        }
        if (s_wifi_selected < 0) {
            s_wifi_selected = 0; /* first step takes entry 0 (clock parity) */
            lv_roller_set_selected(s_wifi_roller, 0, LV_ANIM_ON);
        } else {
            dial_roller_step(s_wifi_roller, dir); /* NORMAL mode clamps at
                                                   * the ends, like a real
                                                   * roller — no wrap on a
                                                   * finite AP list */
            s_wifi_selected = (int)lv_roller_get_selected(s_wifi_roller);
        }
        s_dial_dirty = true;
        break;
    }
    case SCREEN_ID_SETTINGS_WIFI_PASSWORD: {
        const int n = s_wheel_n;
        if (n <= 0) {
            break;
        }
        int hot = s_wheel_hot;
        hot = (hot < 0) ? (dir > 0 ? 0 : n - 1) : ((hot + dir + n * 4) % n);
        int sec = 0;
        while (sec < WHEEL_N_SECTORS - 1 && hot >= s_wheel_sector_start[sec + 1]) {
            sec++;
        }
        settings_wheel_set_revealed(true, sec);
        settings_wheel_set_hot(hot);
        s_dial_dirty = true;
        break;
    }
    default:
        break; /* moon screens / manual time entry: no dial cursor */
    }
}

void screens_dial_commit(void) {
    enum ScreensEnum id = ui_current_screen();
    if (id != s_dial_screen) {
        dial_screen_reset(id);
    }
    if (!s_dial_dirty) {
        ESP_LOGD("screens_dial", "tap commit skipped (cursor clean) @%d", (int)id);
        return; /* no cursor moved since entry: a stray background tap
                 * must not save/commit anything */
    }
    ESP_LOGI("screens_dial", "tap commit @%d (focus %d)", (int)id, s_dial_roller_focus);
    switch (id) {
    case SCREEN_ID_SETTINGS_HUB: {
        const lv_obj_t *icons[8];
        enum ScreensEnum targets[8];
        int n = screens_hub_icons(icons, targets, 8);
        if (s_dial_hub_focus < n) {
            lv_obj_send_event((lv_obj_t *)icons[s_dial_hub_focus], LV_EVENT_CLICKED, NULL);
        } else if (objects.settings_hub_exit) {
            lv_obj_send_event(objects.settings_hub_exit, LV_EVENT_CLICKED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_ALARM: {
        lv_obj_t *o = dial_alarm_obj(s_dial_alarm_focus);
        if (o) {
            lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_ALARM_TIME: {
        /* P3: rollers commit with RELEASED as before; a day handle
         * commits through ITS RELEASED save cb (the same lift-a-finger
         * path the drag uses — persists time + range, re-arms). */
        lv_obj_t *h = dial_handle_obj(id, s_dial_roller_focus);
        if (h != NULL) {
            lv_obj_send_event(h, LV_EVENT_RELEASED, NULL);
        } else {
            lv_obj_t *a = NULL, *b = NULL;
            dial_roller_pair(id, &a, &b);
            lv_obj_send_event((s_dial_roller_focus == 1) ? b : a, LV_EVENT_RELEASED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
    case SCREEN_ID_SETTINGS_DEEP_SLEEP:
    case SCREEN_ID_SETTINGS_POMODORO:
    case SCREEN_ID_SETTINGS_TZ:
    case SCREEN_ID_SETTINGS_ALARM_SOUND: {
        lv_obj_t *a = NULL, *b = NULL;
        dial_roller_pair(id, &a, &b);
        lv_obj_t *r = (s_dial_roller_focus == 1) ? b : a;
        if (r) {
            /* The roller's RELEASED handler IS the commit (store save /
             * reschedule) -- same as lifting a finger off a drag. */
            lv_obj_send_event(r, LV_EVENT_RELEASED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_DISPLAY: {
        /* The roller's own RELEASED is the save (settings.txt + the
         * root re-push) -- same as lifting a finger off a drag. */
        if (s_display_roller) {
            lv_obj_send_event(s_display_roller, LV_EVENT_RELEASED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_WIFI: {
        if (s_wifi_roller) {
            /* The roller's CLICKED cb IS the commit (opens the wheel
             * keyboard for the window-centre entry) -- the dial commit
             * is exactly a tap here. */
            lv_obj_send_event(s_wifi_roller, LV_EVENT_CLICKED, NULL);
        }
        break;
    }
    case SCREEN_ID_SETTINGS_WIFI_PASSWORD: {
        const int hot = s_wheel_hot;
        if (hot >= 0 && hot < s_wheel_n) {
            settings_wheel_add_slot(hot);
            settings_wheel_set_hot(-1);  /* refresh the centre to the
                                          * running entry... */
            settings_wheel_set_hot(hot); /* ...and keep the lit box */
        }
        break;
    }
    default:
        break;
    }
    s_dial_dirty = false;
}
#endif /* CONFIG_BOARD_KNOB */
