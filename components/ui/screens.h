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
 *      [0]  modem_sleep              [15]  modem_sleep_clock
 *      [1]  wifi                     [16]  modem_sleep_clock__clock        BASE 16
 *      [2]  light_sleep              [17]  ..._label_clock_time
 *      [3]  deep_sleep               [18]  ..._label_clock_alarm
 *      [4]  alarm                    [19]  arc_clock_seconds_modem
 *      [5]  settings_hub             [20]  wifi_clock
 *      [6]  settings_alarm           [21]  wifi_clock__clock               BASE 21
 *      [7]  settings_pomodoro        [22]  ..._label_clock_time
 *      [8]  settings_alarm_time      [23]  ..._label_clock_alarm
 *      [9]  settings_light_sleep     [24]  arc_clock_seconds_wifi
 *      [10] settings_deep_sleep      [25]  light_sleep_clock
 *      [11] settings_wifi            [26]  light_sleep_clock__clock        BASE 26
 *      [12] settings_wifi_password   [27]  ..._label_clock_time
 *      [13] settings_time            [28]  ..._label_clock_alarm
 *      [14] settings_tz              [29]  deep_sleep_clock
 *                                 [30]  deep_sleep_clock__clock           BASE 30
 *                                 [31]  ..._label_clock_time
 *                                 [32]  ..._label_clock_alarm
 *                                 [33]  screen_indicator_sleep1
 *                                 [34]  alarm_clock
 *                                 [35]  alarm_clock__clock                BASE 35
 *                                 [36]  ..._label_clock_time
 *                                 [37]  ..._label_clock_alarm
 *                                 [38]  screen_indicator_alarm
 *                                 [39]  settings_hub_exit        (the settings hub's central exit)
 *                                 [40]  settings_alarm_master    (deactivate-all toggle)
 *                                 [41]  settings_alarm_pomodoro  (pomodoro entry)
 *                                 [42]  settings_alarm_clock     (real-alarm entry)
 *                                 [43]  settings_pomodoro_roller_h
 *                                 [44]  settings_pomodoro_roller_m
 *                                 [45]  settings_pomodoro_caption
 *                                 [46]  settings_alarm_time_roller_h
 *                                 [47]  settings_alarm_time_roller_m
 *                                 [48]  settings_alarm_time_band        (the day-range arc)
 *                                 [49]  settings_alarm_time_handle_start
 *                                 [50]  settings_alarm_time_handle_end
 *                                 [51]  wifi_indicator_wifi
 *                                 [52]  settings_time_roller_d      (manual time entry: date day)
 *                                 [53]  settings_time_roller_m      (date month)
 *                                 [54]  settings_time_roller_y      (date year, 2000+YY)
 *                                 [55]  settings_time_roller_h      (manual time entry: time hour)
 *                                 [56]  settings_time_roller_mi     (time minute)
 *                                 [57]  settings_tz_roller          (timezone/UTC offset, clock_tz.h)
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
    lv_obj_t *settings_time; /* manual time/date entry (P4 entry point; the screen is shared) */
    lv_obj_t *settings_tz;   /* timezone / UTC offset picker (S3 + P4 entry point) */
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
    lv_obj_t *settings_time_roller_d;  /* manual time entry: the date day (01-31) */
    lv_obj_t *settings_time_roller_m;  /* the date month (01-12) */
    lv_obj_t *settings_time_roller_y;  /* the date year (2000+YY, two-digit 00-99) */
    lv_obj_t *settings_time_roller_h;  /* the time hour (00-23) */
    lv_obj_t *settings_time_roller_mi; /* the time minute (00-59) */
    lv_obj_t *settings_tz_roller;      /* the timezone / UTC offset (clock_tz option list) */
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
    SCREEN_ID_SETTINGS_TIME,
    SCREEN_ID_SETTINGS_TZ,
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

/* Manual time/date entry (the P4's fallback for a clock it could not
 * sync: reached by the settings hub's clock icon, P4-only -- the S3's
 * time comes from the PCF85063A and the icon is not added there).
 * Six clock-style rollers in two rows (the time HH:MM:SS at the home
 * clock label's position, the date DD.MM.YY below) seed from the wall
 * clock; releasing a roller commits the whole value (clock_set_time +
 * the trusted flag) so the alarm one-shot re-arms and the moon
 * re-bakes. The S3 build carries the screen too (structurally
 * identical) -- it is simply unreachable. */
void create_screen_settings_time();
void tick_screen_settings_time();

/* Timezone / UTC offset (both boards' entry point: the hub's globe icon).
 * One roller over every 30-minute UTC offset (UTC-12:00 .. UTC+14:00) plus
 * Europe/Berlin (the Kconfig default, DST-aware). Releasing the roller
 * applies the zone (clock_tz_apply -- the P4 re-derives its local digits
 * from the same epoch; the S3 rewrites the PCF digits epoch-consistently),
 * re-arms the alarm arming, and persists the selection. The S3 and the P4
 * both reach it, so unlike the manual time entry there is no board gate. */
void create_screen_settings_tz();
void tick_screen_settings_tz();

/* The circular wheel keyboard for WiFi password entry (reached by
 * tapping an AP row on the WiFi screen, which prefills the SSID).
 * The v2 hybrid (doc 7.14; the operable reference is the lab's panel #1,
 * docs/keyboard/round-keyboard-lab.html): the 26 slots of the active
 * register tile the ring in equal shares, grouped into 12 fixed
 * frequency sectors (their boundaries never move between registers); on
 * press only the pressed sector's boxes are revealed edge-to-edge across
 * the ring band (the lit box inverts) with a DTMF-style centre preview
 * -- the letter commits only on release, and the hit-test is the exact
 * box under the finger. A 4-key register/shift arc (abc / ABC / 123 / #$,
 * top 240 deg), the bottom 120 deg is the circle-xmark COMMIT key (save +
 * connect) and the trash DISCARD key, a plain centre tap backspaces, and
 * a 400 ms centre long press is the only way back to the WiFi list (v2
 * has no in-ring exit -- the xmark glyph is the commit key). */
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
