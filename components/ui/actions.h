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
extern void action_wake_screen(lv_event_t *e);
extern void action_switch_to_airplane_screen(lv_event_t *e);
extern void action_switch_to_alarm_screen(lv_event_t *e);
/* Start the on-demand WiFi + NTP sync: loads the transient WiFi screen,
 * runs wifi_manager_init + clock_sync_time_from_ntp on a background
 * task, and (from the UI tick) drops to the airplane screen when the
 * sync completes -- on both success and failure. LVGL task context. */
extern void action_start_wifi_sync(void);
/* Start the boot hand-off poller (app_main, only when boot landed on
 * the WiFi screen): switches to the airplane screen once the boot
 * sweep is out of the way. */
extern void action_boot_handoff_start(void);
/* Register the input-device callback that dismisses the alarm screen on
 * touch while the device is awake (the idle-wake path only covers the
 * asleep case). Call after the input device exists (post ui_init). */
esp_err_t action_register_alarm_touch(void);
extern void action_switch_to_light_sleep_screen(lv_event_t *e);
/* Test hook (CONFIG_TEST_ROLL_TILT_SEC): 1 s LVGL timer callback for the
 * headless orientation tilt script -- after N s of boot it feeds the real
 * 90-degree-step orientation pipeline synthetic screen-plane gravity for
 * ~15 s (resting -> 90 CW -> 180 -> 90 CCW -> rest), so the full
 * transform path (the PSRAM layer, the centre pivot) runs on the device
 * without a physical tilt. Create the timer only when the hook is on
 * (LVGL task context). */
void action_roll_test_cb(lv_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/
