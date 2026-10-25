#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <esp_err.h>

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
extern void action_switch_to_modem_sleep_screen(lv_event_t *e);
extern void action_switch_to_alarm_screen(lv_event_t *e);
/* Register the input-device callback that dismisses the alarm screen on
 * touch while the device is awake (the idle-wake path only covers the
 * asleep case). Call after the input device exists (post ui_init). */
esp_err_t action_register_alarm_touch(void);
extern void action_switch_to_light_sleep_screen(lv_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/
