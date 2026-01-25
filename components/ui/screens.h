#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *deep_sleep;
    lv_obj_t *alarm;
    lv_obj_t *arc_clock_seconds;
    lv_obj_t *main_clock;
    lv_obj_t *main_clock__clock;
    lv_obj_t *main_clock__label_clock_time;
    lv_obj_t *main_clock__label_clock_alarm;
    lv_obj_t *deep_sleep_clock;
    lv_obj_t *deep_sleep_clock__clock;
    lv_obj_t *deep_sleep_clock__label_clock_time;
    lv_obj_t *deep_sleep_clock__label_clock_alarm;
    lv_obj_t *screen_indicator_sleep;
    lv_obj_t *alarm_clock;
    lv_obj_t *alarm_clock__clock;
    lv_obj_t *alarm_clock__label_clock_time;
    lv_obj_t *alarm_clock__label_clock_alarm;
    lv_obj_t *screen_indicator_alarm;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_DEEP_SLEEP = 2,
    SCREEN_ID_ALARM = 3,
};

void create_screen_main();
void tick_screen_main();

void create_screen_deep_sleep();
void tick_screen_deep_sleep();

void create_screen_alarm();
void tick_screen_alarm();

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
