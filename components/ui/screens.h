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
 *      [0]  modem_sleep              [12]  modem_sleep_clock
 *      [1]  wifi                     [13]  modem_sleep_clock__clock        BASE 13
 *      [2]  light_sleep              [14]  ..._label_clock_time
 *      [3]  deep_sleep               [15]  ..._label_clock_alarm
 *      [4]  alarm                    [16]  arc_clock_seconds_modem
 *      [5]  settings_hub             [17]  wifi_clock
 *      [6]  settings_alarm           [18]  wifi_clock__clock               BASE 18
 *      [7]  settings_pomodoro        [19]  ..._label_clock_time
 *      [8]  settings_alarm_time      [20]  ..._label_clock_alarm
 *      [9]  settings_light_sleep     [21]  arc_clock_seconds_wifi
 *      [10] settings_deep_sleep      [22]  light_sleep_clock
 *      [11] settings_wifi            [23]  light_sleep_clock__clock        BASE 23
 *                                 [24]  ..._label_clock_time
 *                                 [25]  ..._label_clock_alarm
 *                                 [26]  deep_sleep_clock
 *                                 [27]  deep_sleep_clock__clock           BASE 27
 *                                 [28]  ..._label_clock_time
 *                                 [29]  ..._label_clock_alarm
 *                                 [30]  screen_indicator_sleep1
 *                                 [31]  alarm_clock
 *                                 [32]  alarm_clock__clock                BASE 32
 *                                 [33]  ..._label_clock_time
 *                                 [34]  ..._label_clock_alarm
 *                                 [35]  screen_indicator_alarm
 *                                 [36]  settings_hub_exit        (the settings hub's central exit)
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

/* Shared concentric exit button (grey ring + black inner disc + white
 * xmark), used by the settings hub (central) and every sub-screen
 * (top-centre). Returns the outer ring obj; a tap on it fires
 * LV_EVENT_CLICKED on it. The xmark label sits centred on the ring and
 * is non-clickable (the ring catches the tap). */
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

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
