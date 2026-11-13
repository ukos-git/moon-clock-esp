#ifndef MOON_UI_H
#define MOON_UI_H

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Moon phase UI layer (device side of components/moon).
 *
 * Owns the PSRAM frame buffers, the shared lv_image_dsc_t the moon screens
 * display (shown STATIC by plain LVGL -- the old in-plane burn-in spin and
 * its direct-panel-write compositing were removed 2026-11-13), the
 * tilt-driven
 * out-of-plane orbit (QMI8658 IMU sphere model),
 * the light-sleep "dusk" spin (the pole-axis rotation to the fully
 * unilluminated side; the deep-sleep countdown is armed only when it
 * completes), and the tilt activity/wake monitor. Every frame the
 * pipeline produces is displayed as one atomic whole-frame / whole-panel
 * lv_refr_now (never LVGL's async 25-row strip drain), which is the "no
 * top-to-bottom redraw" rule.
 *
 * Call order (app_main, under the display lock, before ui_init):
 *   moon_ui_init()   -- PSRAM buffers + shared image descriptor + timers
 *   create_screens() -- each moon screen calls moon_ui_create_image(parent)
 *                       FIRST so the moon sits below the clock (z-order)
 *   moon_ui_start()  -- mark started and bake the CURRENT phase
 *                       directly: boot shows the static phase from the
 *                       first frame (the synodic boot sweep was removed
 *                       2026-11-13)
 */

/** Allocate buffers, build the image descriptor, start the tick timer. */
void moon_ui_init(void);

/**
 * Create the moon lv_image (400x400 disk centered on the 466x466 panel,
 * black outside the disk) pointed at the shared PSRAM buffer. Must be the
 * first child of the screen so the clock/arc render on top of it.
 */
lv_obj_t *moon_ui_create_image(lv_obj_t *parent);

/** Invalidate the moon image on every attached screen (after a bake/rotate). */
void moon_ui_refresh(void);

/** Bake + display the moon for the given UTC epoch. */
void moon_ui_bake_now(int64_t utc_epoch);

/** Start the moon: bake the current phase directly (boot is static). */
void moon_ui_start(void);

/**
 * Boot-animation SEAM, kept for the knob companion: the clock home
 * plays NO boot animation since 2026-11-13 (this is an empty no-op
 * there), while the knob's own producer seam plays its OWN boot sweep
 * here, so the shared wake flow keeps a single call site family-wide.
 */
void moon_ui_replay_animation(void);

/**
 * Cheap day-change probe (call at most once per minute, e.g. from the clock
 * display update): re-bakes when the calendar day rolls over.
 */
void moon_ui_day_tick(void);

/**
 * Pause/resume the moon tick (sleep screens show a static moon). A pause
 * joins a running
 * dusk (the light-sleep pole spin) and interrupts the orbit; the frame
 * on screen simply stays under the sleep screens. The deep-sleep
 * transition after a
 * completed dusk keeps the dark side on screen.
 */
void moon_ui_pause(bool pause);

/**
 * Start the light-sleep "dusk" (the pole-axis spin to the fully
 * unilluminated side, 1 degree/frame, shortest arc). Call AFTER
 * moon_ui_pause(true) so the on-screen pose (tilt
 * orbit viewport) is captured. The spin is a constant projection: the
 * fully-lit body-frame equirect map is baked ONCE for the epoch (~2 s)
 * and the intermediate frames re-project that fixed map through the
 * folded camera basis (a gather-class pass, no per-frame lighting
 * recompute, no terminator flicker); the final (dark-side) frame is one
 * full-resolution bake at the endpoint and lingers on the sleep
 * screens. The deep-sleep RTC countdown is armed on the LAST frame's
 * display (the tick), never at the light-sleep entry. Returns false
 * when the dusk could not start (no static phase yet / a pipeline owns
 * the buffers / the dark side is already the on-screen state), so the
 * caller can arm the countdown immediately as a fallback.
 * UI_MOON_DUSK_ENABLE.
 */
bool moon_ui_dusk_start(void);

/**
 * The tilt monitor's settled state: true while the measured departure of
 * the current pose from the settled-pose reference is below the activity
 * bar (the device is at rest / slowly drifting), false while it is
 * deliberately in motion. The clock-roll correction (actions.c) uses it
 * as its "at rest" gate (the settled reference tracks only while settled,
 * so at-rest drift cannot walk the text).
 */
bool moon_ui_tilt_settled(void);

/**
 * Boot-animation SEAM predicate: true while a boot sweep is baking.
 * Always false on the clock home (the sweep retired 2026-11-13); the
 * knob companion's seam answers from its OWN boot sweep, which the
 * shared actions.c idle re-arm and Wi-Fi-sync gates still honor there.
 */
bool moon_ui_sweeping(void);

/**
 * True once the dusk spin's dark-side frame is on screen (see
 * moon_ui_dusk_start). Reset by the wake and by any fresh bake. The
 * demo tour polls it to know its dusk spin completed (UI_DEMO_MODE).
 */
bool moon_ui_dusk_complete(void);

/**
 * Display the new screen's contents (moon image included) as ONE
 * atomic whole-panel flush after a plain lv_scr_load. Must run in the
 * LVGL task context. No-op while a pipeline owns the buffer (a bake
 * completion then displays the screen atomically itself).
 * When a screen-switch fade-out dimmed the panel (s_fade_pending,
 * loadScreen), the new screen's atomic flush runs while the panel is
 * still black and the hardware brightness then ramps back
 * (UI_SCREEN_SWITCH_FADE).
 */
void moon_ui_screen_switched(void);

/**
 * Dim the panel's OWN hardware brightness to black ahead of a screen
 * switch (LVGL task; loadScreen calls it before lv_scr_load on EVERY
 * switch except the both-moon one). Writes no pixels; the static panel
 * simply dims. No-op when the fade is off, the
 * moon UI is not ready, or a bake pipeline owns the buffers.
 */
void moon_ui_fade_out(void);

/** Does the screen with this id carry a moon image (loadScreen's
 * fade decision: every switch fades, the both-moon switch stays
 * atomic). */
bool moon_ui_screen_is_moon(enum ScreensEnum id);

/**
 * Set the panel's own hardware brightness (0..100 %), serialized with
 * LVGL's flushes (the one QSPI bus the panel
 * io owns). The wake / deep-sleep / boot backlight paths use it IN PLACE
 * OF bsp_display_brightness_set()/bsp_display_backlight_on/off() so a 0x51
 * write is never queued behind a strip DMA that could stall the bus.
 * Safe from any task; direct BSP call while the moon UI is not ready.
 */
esp_err_t moon_ui_panel_brightness_set(int pct);

/**
 * Ramp the panel's hardware brightness from its current level to `to_pct`
 * (0..100 %), paced like the screen-switch fade (UI_SCREEN_SWITCH_FADE ms).
 * LVGL task (the light-sleep entry / wake / alarm call sites); non-LVGL /
 * not-ready callers get the instant set. Holds off the other 0x51 writers
 * for the ramp's duration (the 0x51 steps must be the only panel-io traffic
 * in flight) but owes NO fade-in afterwards (s_fade_pending is untouched).
 * No-op when the panel is already at the target. The light-sleep dim uses
 * it (down to CONFIG_UI_LIGHT_SLEEP_DIM_PCT, back to 100 on wake / an
 * alarm ring); the screen-switch fade keeps moon_ui_fade_out.
 */
void moon_ui_panel_dim_ramp(int to_pct);

/**
 * Freeze the panel's io traffic while the wake path's on-demand WiFi sync
 * storm runs. esp_wifi_init loads the full Wi-Fi firmware into RAM and
 * calibrates the RF; the storm takes in-flight QSPI DMA completions
 * (bench-verified on the S3 AMOLED: a wake sync with live tick flushes
 * stalls the GPSPI bus lock ~6 s into the wake -- lost
 * completion, panel-io watchdog restart). While held, EVERY panel write
 * is DROPPED, not blocked (LVGL flush strips, 0x51
 * brightness writes; fade/dim ramps are skipped whole): writers never
 * park and LVGL keeps drawing into its buffers.
 * The LVGL flushes complete their hand through the hold's stub flush_cb
 * (swapped in/out by the UI-tick poll in moon_ui_panel_repaint_pending):
 * dropping a flush inside the panel draw_bitmap wrapper instead would
 * strand the adapter's flush wait forever.
 * action_start_wifi_sync arms it right before wifi_sync_kickoff; on the
 * LVGL task the arm additionally QUIESCES -- it installs the flush guard
 * immediately (a tick early, so the sync screen's own switch flush rides
 * the bus before the storm, never into it) and drains one in-flight DMA
 * under the panel mutex (an already-queued flush strip is the storm's
 * victim; the hold flags cannot drop it).
 * wifi_sync_task_fn first takes the radio fully DOWN (wifi_manager_deinit
 * -- an ASSOCIATED station's ~300 ms DTIM beacon wake steals completions
 * just like the init storm; the airplane screen means modem-down), then
 * releases it on BOTH outcomes (and the kickoff's task-create-failure
 * branch) -- but the release only STAMPS a 4 s radio-tail drain (the
 * driver teardown churn that deinit kicks off), which the UI-tick poll
 * then unfreezes at; a 15 s deadline self-heals a missed release. On
 * release a whole-screen repaint is queued -- the screen self-heals.
 * No-op seam on the knob (LEDC backlight, no
 * shared bus lock to wedge).
 */
void moon_ui_panel_traffic_hold(bool hold);

/** Hold variant with an explicit self-heal cap and release tail (ms).
 *  The wake-sync path uses the defaults above. The WiFi-scan session
 *  (screens.c, task #30) arms with a cap covering the 0-AP four-retry
 *  scan and releases with a teardown tail from the GENERIC ui tick's
 *  poll (screens_wifi_scan_hold_poll, actions.c) once the scan task is
 *  done -- the hold follows the radio session, not the screen that
 *  started it, so leaving mid-scan cannot cut the guard; the tail covers
 *  the esp_wifi_stop/deinit churn the same way the sync path's does.
 *  Refcounted: nested arms (list re-entry while an older hold is live)
 *  extend the cap; one release per arm, the cap self-heals a leak. */
void moon_ui_panel_traffic_hold_ext(bool hold, int cap_ms, int tail_ms);

/** Consume the queued whole-screen repaint after a traffic-hold release
 * (LVGL task; clears the flag, true once). Call from the UI tick: on
 * true, lv_obj_invalidate(lv_screen_active()) and let the tick repaint. */
bool moon_ui_panel_repaint_pending(void);

/**
 * Hang-diagnostic context dump: print the volatile pipeline flags around
 * the fade / flush path (TEST_HANG_WATCH aid; safe from any task).
 * Cross-reference with the task-state dump to identify which lock a stuck
 * task is parked on.
 */
void moon_ui_dump_hang_context(void);

#ifdef __cplusplus
}
#endif

#endif /* MOON_UI_H */
