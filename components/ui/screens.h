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
 * 2. ONE shared clock widget. create_user_widget_clock(parent, 17) is
 *    called exactly once (the modem-sleep screen) and creates the only
 *    clock: objects[17] (the full-screen flex container), objects[18]
 *    (the HH:MM slot row) and objects[19] (the alarm label). The four
 *    other moon screens ALIAS those same three objects at their own
 *    bases -- objects[22/23/24] (wifi), [27/28/29] (light sleep),
 *    [31/32/33] (deep sleep), [36/37/38] (alarm) point at the SAME
 *    lv_obj_t's -- so every screen ticks and reads one set of labels
 *    that is always current: the vars are updated at the minute tick
 *    (which ticks the shared widget regardless of the active screen)
 *    and ui_clock_attach_to() (the loadScreen choke point) reparents
 *    the widget onto the new moon screen and refreshes the vars from
 *    the RTC, so a switch (the wake in particular) shows the current
 *    minute immediately. The per-screen <screen>_clock containers
 *    ([16], [21], [26], [30], [35]) stay as the widget's parent
 *    targets (plus the unclickable_recursive targets); the deep-sleep
 *    HIDDEN flag and the light-sleep alpha apply to the parent
 *    containers (and thus hide/dim the attached widget too).
 *    tick_user_widget_clock(17) is the only clock tick; the per-screen
 *    tick functions no longer tick it. (The indices shifted +1 when
 *    settings_alarm_sound joined the screen roots, 2026-11-04 -- the
 *    off-by-one that drew the bare slot row instead of the flex widget.)
 *
 *      [0]  modem_sleep              [16]  modem_sleep_clock         (widget parent 1/5)
 *      [1]  wifi                     [17]  modem_sleep_clock__clock  (THE widget container)
 *      [2]  light_sleep              [18]  ..._label_clock_time      (the HH:MM slot row)
 *      [3]  deep_sleep               [19]  ..._label_clock_alarm     (the alarm label)
 *      [4]  alarm                    [20]  arc_clock_seconds_modem
 *      [5]  settings_hub             [21]  wifi_clock                (widget parent 2/5)
 *      [6]  settings_alarm           [22]  wifi_clock__clock         = objects[17] (alias)
 *      [7]  settings_pomodoro        [23]  ..._label_clock_time      = objects[18] (alias)
 *      [8]  settings_alarm_time      [24]  ..._label_clock_alarm     = objects[19] (alias)
 *      [9]  settings_light_sleep     [25]  arc_clock_seconds_wifi
 *      [10] settings_deep_sleep      [26]  light_sleep_clock         (widget parent 3/5)
 *      [11] settings_wifi            [27]  light_sleep_clock__clock  = objects[17] (alias)
 *      [12] settings_wifi_password   [28]  ..._label_clock_time      = objects[18] (alias)
 *      [13] settings_time            [29]  ..._label_clock_alarm     = objects[19] (alias)
 *      [14] settings_tz              [30]  deep_sleep_clock          (widget parent 4/5)
 *      [15] settings_alarm_sound     [31]  deep_sleep_clock__clock   = objects[17] (alias)
 *                                 [32]  ..._label_clock_time        = objects[18] (alias)
 *                                 [33]  ..._label_clock_alarm       = objects[19] (alias)
 *                                 [34]  screen_indicator_sleep1
 *                                 [35]  alarm_clock                 (widget parent 5/5)
 *                                 [36]  alarm_clock__clock          = objects[17] (alias)
 *                                 [37]  ..._label_clock_time        = objects[18] (alias)
 *                                 [38]  ..._label_clock_alarm       = objects[19] (alias)
 *                                 [39]  screen_indicator_alarm
 *                                 [40]  settings_hub_exit        (the settings hub's central exit)
 *                                 [41]  settings_alarm_master    (deactivate-all toggle)
 *                                 [42]  settings_alarm_pomodoro  (pomodoro entry)
 *                                 [43]  settings_alarm_clock     (real-alarm entry)
 *                                 [44]  settings_pomodoro_roller_h
 *                                 [45]  settings_pomodoro_roller_m
 *                                 [46]  settings_pomodoro_caption
 *                                 [47]  settings_alarm_time_roller_h
 *                                 [48]  settings_alarm_time_roller_m
 *                                 [49]  settings_alarm_time_band        (the day-range arc)
 *                                 [50]  settings_alarm_time_handle_start
 *                                 [51]  settings_alarm_time_handle_end
 *                                 [52]  wifi_indicator_wifi
 *                                 [53]  settings_time_roller_d      (manual time entry: date day)
 *                                 [54]  settings_time_roller_m      (date month)
 *                                 [55]  settings_time_roller_y      (date year, 2000+YY)
 *                                 [56]  settings_time_roller_h      (manual time entry: time hour)
 *                                 [57]  settings_time_roller_mi     (time minute)
 *                                 [58]  settings_tz_roller          (timezone/UTC offset, clock_tz.h)
 *                                 [59]  settings_alarm_sound_roller (alarm sound, alarm_audio's 4 loops)
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
    lv_obj_t *settings_time;        /* manual time/date entry (P4 entry point; the screen is shared) */
    lv_obj_t *settings_tz;          /* timezone / UTC offset picker (S3 + P4 entry point) */
    lv_obj_t *settings_alarm_sound; /* alarm sound picker (S3 + P4 entry point) */
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
    lv_obj_t *settings_time_roller_d;      /* manual time entry: the date day (01-31) */
    lv_obj_t *settings_time_roller_m;      /* the date month (01-12) */
    lv_obj_t *settings_time_roller_y;      /* the date year (2000+YY, two-digit 00-99) */
    lv_obj_t *settings_time_roller_h;      /* the time hour (00-23) */
    lv_obj_t *settings_time_roller_mi;     /* the time minute (00-59) */
    lv_obj_t *settings_tz_roller;          /* the timezone / UTC offset (clock_tz option list) */
    lv_obj_t *settings_alarm_sound_roller; /* the alarm sound name (alarm_audio's 4 loops) */
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
    SCREEN_ID_SETTINGS_ALARM_SOUND,
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

/* Alarm sound picker (both boards' entry point: the hub's speaker icon).
 * Both boards carry the ES8311 codec + speaker amp, so unlike the
 * manual time entry there is no board gate. A 4-name roller (the
 * alarm_audio loops: two-tone / beep / bell / arpeggio) seeded from
 * settings_alarm_sound_id; releasing the roller applies + previews the
 * sound and persists it. A speaker button previews the currently
 * selected loop. */
void create_screen_settings_alarm_sound();
void tick_screen_settings_alarm_sound();

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

/* The ONE shared clock widget (layout contract 2): called exactly
 * once, from the modem-sleep screen, with BASE 16. */
void create_user_widget_clock(lv_obj_t *parent_obj, int startWidgetIndex);

/* Tick the shared clock from its vars (the minute tick + the attach).
 * The base is 16 for every caller -- the other four moon screens
 * alias the same objects, so there is nothing else to tick. */
void tick_user_widget_clock(int startWidgetIndex);

/* The loadScreen hook (LVGL task): reparent the shared clock widget
 * onto the active moon screen (settings screens keep it where it
 * was -- computed, not rendered) and refresh the clock vars from the
 * RTC so the switch shows the current minute immediately. Called
 * BEFORE the screen switch's atomic flush, so the fresh digits are
 * part of that one frame. */
void ui_clock_attach_to(enum ScreensEnum screenId);

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

/* ---- demo mode (UI_DEMO_MODE) touch-target surface --------------------
 * The scripted tour (components/ui/demo.c) taps the REAL widgets with a
 * second virtual LVGL pointer, so it needs the objects and the wheel's
 * geometry, not loadScreen shortcuts. The registries are always built
 * (they cost 8+12 pointers); the point API compiles with the demo. */

/* The hub ring's created icons in ring order (target = the screen the
 * icon opens). Returns the count, fills up to max entries. */
int screens_hub_icons(const lv_obj_t **objs, enum ScreensEnum *targets, int max);

/* The bottom-centre exit button of the sub-screen whose ROOT is `root`
 * (objects.settings_*), or NULL if it has none. */
lv_obj_t *screens_sub_exit_get(lv_obj_t *root);

/* The WiFi screen's AP row object (rows exist for the whole boot; the
 * scan only fills their text). NULL for an out-of-range index. */
lv_obj_t *screens_wifi_row_get(int i);
/* Poll the WiFi scan's panel traffic hold (task #30): when a scan the
 * UI started has finished, release the hold with the teardown tail --
 * the release's whole-screen repaint then heals the storm's drops on
 * whatever screen is active. Call from the GENERIC ui tick: the hold
 * follows the scan, not the screen that started it (leaving mid-scan
 * keeps the guard). Idempotent, cheap. */
void screens_wifi_scan_hold_poll(void);

#ifdef CONFIG_BOARD_KNOB
/* ---- P2 dial surface (knob-only; see screens.c "P2 dial surface") ----
 * The dial steps one per-screen edit cursor; a tap commits it through
 * the native CLICKED/RELEASED handlers. LOCK CONTRACT: they touch LVGL
 * objects but never lock -- call them with the LVGL mutex held (dial
 * task) or from indev-callback context, where the mutex is already
 * held (background-tap commit). */
void screens_dial_init(void);    /* post ui_init: press-focus + entry state */
void screens_dial_step(int dir); /* +1 = dial clockwise detent */
void screens_dial_commit(void);  /* tap: commit the focused widget */
#endif                           /* CONFIG_BOARD_KNOB */

#ifdef CONFIG_UI_DEMO_MODE
/* Points in the wheel's LOCAL screen space (the space the wheel
 * handlers' inverse transform maps the raw indev point into); the demo
 * rotates them by the screen root's base orientation into display
 * coords. All false when the wheel cannot answer (wrong state). */
bool settings_wheel_demo_mode_point(int mode, lv_point_t *out);
bool settings_wheel_demo_letter_point(char ch, lv_point_t *out); /* active register */
bool settings_wheel_demo_commit_point(lv_point_t *out);
bool settings_wheel_demo_discard_point(lv_point_t *out);
bool settings_wheel_demo_center_point(lv_point_t *out);
int settings_wheel_demo_pw_len(void);
/* Open the keyboard prefilled (the no-AP walk fallback). */
void settings_wheel_demo_open(const char *ssid);
#endif /* CONFIG_UI_DEMO_MODE */

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/
