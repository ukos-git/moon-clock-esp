#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_update_clock_display(lv_event_t *e);
extern void action_increment_clock_seconds(lv_event_t *e);
extern void action_update_alarm_display(lv_event_t *e);
extern void action_alarm_triggered(lv_event_t *e);
extern void action_switch_to_deep_sleep_screen(lv_event_t *e);
extern void action_switch_to_main_screen(lv_event_t *e);
extern void action_switch_to_alarm_screen(lv_event_t *e);
extern void action_switch_to_light_sleep_screen(lv_event_t *e);

// Helper function to set UI tick timer reference (not a UI action)
extern void action_set_ui_tick_timer(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/
