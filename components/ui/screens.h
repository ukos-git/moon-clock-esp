#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* objects_t layout contract:
 *
 * 1. The screen ROOTS are the first members, in enum ScreensEnum order:
 *    loadScreen(id) indexes this array at `id - 1` (ui.c
 *    getLvglObjectFromIndex), so a mismatch is an out-of-bounds read,
 *    not a visible bug.
 *
 * 2. create_user_widget_clock(parent, BASE) writes objects[BASE ..
 *    BASE+2] (container, time label, alarm label) and tick_user_widget_clock
 *    reads the same three. BASE is the index of the <screen>_clock__clock
 *    member below:
 *
 *      [0]  modem_sleep              [13]  modem_sleep_clock
 *      [1]  wifi                     [14]  modem_sleep_clock__clock        BASE 14
 *      [2]  light_sleep              [15]  ..._label_clock_time
 *      [3]  deep_sleep               [16]  ..._label_clock_alarm
 *      [4]  alarm                    [17]  arc_clock_seconds_modem
 *      [5]  settings_hub             [18]  wifi_clock
 *      [6]  settings_alarm           [19]  wifi_clock__clock               BASE 19
 *      [7]  settings_pomodoro        [20]  ..._label_clock_time
 *      [8]  settings_alarm_time      [21]  ..._label_clock_alarm
 *      [9]  settings_light_sleep     [22]  arc_clock_seconds_wifi
 *      [10] settings_deep_sleep      [23]  light_sleep_clock
 *      [11] settings_wifi            [24]  light_sleep_clock__clock        BASE 24
 *      [12] settings_wifi_password   [25]  ..._label_clock_time
 *                                 [26]  ..._label_clock_alarm
 *                                 [27]  deep_sleep_clock
 *                                 [28]  deep_sleep_clock__clock           BASE 28
 *                                 [29]  ..._label_clock_time
 *                                 [30]  ..._label_clock_alarm
 *                                 [31]  screen_indicator_sleep1
 *                                 [32]  alarm_clock
 *                                 [33]  alarm_clock__clock                BASE 33
 *                                 [34]  ..._label_clock_time
 *                                 [35]  ..._label_clock_alarm
 *                                 [36]  screen_indicator_alarm
 *                                 [37]  settings_hub_exit        (the settings hub's central exit)
 *                                 [38]  settings_alarm_master    (deactivate-all toggle)
 *                                 [39]  settings_alarm_pomodoro  (pomodoro entry)
 *                                 [40]  settings_alarm_clock     (real-alarm entry)
 *                                 [41]  settings_pomodoro_roller_h
 *                                 [42]  settings_pomodoro_roller_m
 *                                 [43]  settings_pomodoro_caption
 *                                 [44]  settings_alarm_time_roller_h
 *                                 [45]  settings_alarm_time_roller_m
 *                                 [46]  settings_alarm_time_band        (the day-range arc)
 *                                 [47]  settings_alarm_time_handle_start
 *                                 [48]  settings_alarm_time_handle_end
 *                                 [49]  wifi_indicator_wifi
 *
 * The wheel-keyboard's own widgets (the 26 letters, the group dividers,
 * the sweep highlight, the mode keys, the centre preview) are LOCAL to
 * create_screen_settings_wifi_password (static arrays in screens.c);
 * objects_t only names the screen roots and the clock-widget slots.
 */
typedef struct _objects_t {
    /* Screen roots (loadScreen index = ScreensEnum value - 1). */
    lv_obj_t *modem_sleep;
    lv_obj_t *wifi;
    lv_obj_t *light_sleep;
    lv_obj_t *deep_sleep;
    lv_obj_t *alarm;
    lv_obj_t *settings_hub;
    lv_obj_t *settings_alarm;
    lv_obj_t *settings_pomodoro;
    lv_obj_t *settings_alarm_time;
    lv_obj_t *settings_light_sleep;
    lv_obj_t *settings_deep_sleep;
    lv_obj_t *settings_wifi;
    lv_obj_t *settings_wifi_password;
    /* Per-screen children (see the layout contract above). */
    lv_obj_t *modem_sleep_clock;
    lv_obj_t *modem_sleep_clock__clock;
    lv_obj_t *modem_sleep_clock__label_clock_time;
    lv_obj_t *modem_sleep_clock__label_clock_alarm;
    lv_obj_t *arc_clock_seconds_modem;
    lv_obj_t *wifi_clock;
    lv_obj_t *wifi_clock__clock;
    lv_obj_t *wifi_clock__label_clock_time;
    lv_obj_t *wifi_clock__label_clock_alarm;
    lv_obj_t *arc_clock_seconds_wifi;
    lv_obj_t *light_sleep_clock;
    lv_obj_t *light_sleep_clock__clock;
    lv_obj_t *light_sleep_clock__label_clock_time;
    lv_obj_t *light_sleep_clock__label_clock_alarm;
    lv_obj_t *deep_sleep_clock;
    lv_obj_t *deep_sleep_clock__clock;
    lv_obj_t *deep_sleep_clock__label_clock_time;
    lv_obj_t *deep_sleep_clock__label_clock_alarm;
    lv_obj_t *screen_indicator_sleep1;
    lv_obj_t *alarm_clock;
    lv_obj_t *alarm_clock__clock;
    lv_obj_t *alarm_clock__label_clock_time;
    lv_obj_t *alarm_clock__label_clock_alarm;
    lv_obj_t *screen_indicator_alarm;
    lv_obj_t *settings_hub_exit;
    lv_obj_t *settings_alarm_master;
    lv_obj_t *settings_alarm_pomodoro;
    lv_obj_t *settings_alarm_clock;
    lv_obj_t *settings_pomodoro_roller_h;
    lv_obj_t *settings_pomodoro_roller_m;
    lv_obj_t *settings_pomodoro_caption;
    lv_obj_t *settings_alarm_time_roller_h;
    lv_obj_t *settings_alarm_time_roller_m;
    lv_obj_t *settings_alarm_time_band;         /* the day-range arc (upper half circle) */
    lv_obj_t *settings_alarm_time_handle_start; /* the range-start drag handle (a day knob) */
    lv_obj_t *settings_alarm_time_handle_end;   /* the range-end drag handle (a day knob) */
    lv_obj_t *wifi_indicator_wifi; /* WiFi-screen indicator (below the clock): grey = connecting, white = connected */
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MODEM_SLEEP = 1,
    SCREEN_ID_WIFI,
    SCREEN_ID_LIGHT_SLEEP,
    SCREEN_ID_DEEP_SLEEP,
    SCREEN_ID_ALARM,
    SCREEN_ID_SETTINGS_HUB,
    SCREEN_ID_SETTINGS_ALARM,
    SCREEN_ID_SETTINGS_POMODORO,
    SCREEN_ID_SETTINGS_ALARM_TIME,
    SCREEN_ID_SETTINGS_LIGHT_SLEEP,
    SCREEN_ID_SETTINGS_DEEP_SLEEP,
    SCREEN_ID_SETTINGS_WIFI,
    SCREEN_ID_SETTINGS_WIFI_PASSWORD,
};

void create_screen_modem_sleep();
void tick_screen_modem_sleep();

void create_screen_wifi();
void tick_screen_wifi();

void create_screen_light_sleep();
void tick_screen_light_sleep();

void create_screen_deep_sleep();
void tick_screen_deep_sleep();

void create_screen_alarm();
void tick_screen_alarm();

/* Settings screens (moon-less by design: the settings hub and its
 * sub-screens show no moon image, so they must NOT call
 * moon_screen_prepare / moon_ui_create_image). */
void create_screen_settings_hub();
void tick_screen_settings_hub();

void create_screen_settings_alarm();
void tick_screen_settings_alarm();

void create_screen_settings_pomodoro();
void tick_screen_settings_pomodoro();

void create_screen_settings_alarm_time();
void tick_screen_settings_alarm_time();

void create_screen_settings_light_sleep();
void tick_screen_settings_light_sleep();

void create_screen_settings_deep_sleep();
void tick_screen_settings_deep_sleep();

void create_screen_settings_wifi();
void tick_screen_settings_wifi();

/* The circular wheel keyboard for WiFi password entry (reached by
 * tapping an AP row on the WiFi screen, which prefills the SSID).
 * Two concentric letter rings (the inner wayfinding ring + the outer
 * 2x-angle sweep ring revealed on press), a 4-key register/shift arc
 * (abc / ABC / 123 / #$), a centre password preview that commits on a
 * short tap (save + connect) and backspaces on a long press, and the
 * shared sub-screen exit back to the WiFi list. */
void create_screen_settings_wifi_password();
void tick_screen_settings_wifi_password();

/* Shared exit button: the fa-circle-xmark glyph alone (U+F057 -- the
 * filled white circle with the cut-out cross IS the button), used
 * central on the settings hub and bottom-centre on every sub-screen.
 * Returns the tap box (the glyph's natural 96 px, widened by size); a
 * tap on it fires LV_EVENT_CLICKED on it. */
lv_obj_t *settings_create_exit_button(lv_obj_t *parent, int size);

void create_user_widget_clock(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_clock(int startWidgetIndex);

enum Themes {
    THEME_ID_MOON,
};
enum Colors {
    COLOR_ID_MOON,
};
void change_color_theme(uint32_t themeIndex);
extern uint32_t theme_colors[1][1];
extern uint32_t active_theme_index;

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

/* Dispatch a settings entry's screen refresh (the hub-icon / entry
 * handlers call it after loadScreen + ui_tick). Exposed for the
 * TEST_WIFI_SCAN_SEC test hook. */
void settings_screen_refresh_for(enum ScreensEnum target);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
