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
#ifdef CONFIG_BOARD_KNOB
/* Knob boot: create/resume the 10 ms UI-tick + 1 s seconds-increment
 * lv_timers that the clocks receive via the wake flow the knob does not
 * run (knob_power owns its ladder). Creation-phase call (before
 * lvgl_task), no locking. See the comment at the definition. */
extern void action_ensure_ui_timers(void);
#endif
/* Register the input-device callback that dismisses the alarm screen on
 * touch while the device is awake (the idle-wake path only covers the
 * asleep case). Call after the input device exists (post ui_init). */
esp_err_t action_register_alarm_touch(void);
/* Push the base screen orientation (the panel's 90 deg CW mount offset)
 * to every screen root. Call after ui_init (the roots must exist) and
 * before the first frame: the IMU's first sample arrives late, and the
 * orientation pipeline only pushes on a change. */
extern void action_orientation_init(void);
/* Register the input-device callback for the 2 s press re-sync: a press
 * of 2 s or more re-anchors the orientation pipeline's home pose to the
 * pose held while pressing (and restores the base orientation). Needed
 * because the one-shot 1 s seed can latch 180 deg off if the device was
 * flipped while the first window filled -- no tilt then recovers it (the
 * T4 rule re-tracks only on step 0). Call after ui_init (the input
 * device must exist). */
esp_err_t action_register_orientation_touch(void);
extern void action_switch_to_light_sleep_screen(lv_event_t *e);
/* Test hook (CONFIG_TEST_ROLL_TILT_SEC): 1 s LVGL timer callback for the
 * headless orientation tilt script -- after N s of boot it feeds the real
 * 90-degree-step orientation pipeline synthetic screen-plane gravity for
 * ~15 s (resting -> 90 CW -> 180 -> 90 CCW -> rest), so the full
 * transform path (the PSRAM layer, the centre pivot) runs on the device
 * without a physical tilt. Create the timer only when the hook is on
 * (LVGL task context). */
void action_roll_test_cb(lv_timer_t *timer);

/* Demo mode (UI_DEMO_MODE): the scripted full tour (moon screens,
 * settings walk, wheel keyboard, alarm ring, dusk, roll, sync) lives in
 * demo.c; docs/verification/demo-mode. These are the action-layer
 * hooks it drives. */
bool action_demo_active(void); /* true while the demo owns the display */
void action_clock_widget_label(int hour, int minute, int second);
/* demo.c (always compiled; a no-op when the mode is off): create the
 * phase engine + virtual pointer (LVGL task context, post ui_init). */
void ui_demo_init(void);
#ifdef CONFIG_UI_DEMO_MODE
/* Claim the finished on-demand sync the UI tick leaves unclaimed while
 * the demo runs (the SYNC phase + the post-wake airplane hand-off). */
bool action_sync_demo_done(bool *ok);
/* Pass one alarm fire through the demo's scheduled-alarm suppression
 * (the demo's own ALARM phase arms this; every other fire is dropped
 * while the tour runs). */
void action_demo_allow_alarm(bool allow);
#endif
/* Arm / poll one run of the synthetic roll script (the demo's ROLL
 * phase; compiles with the roll test hook OR the demo). begin() from
 * the LVGL task, then poll busy() once a second (~27 s run). */
void action_roll_test_begin(void);
bool action_roll_test_busy(void);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/
