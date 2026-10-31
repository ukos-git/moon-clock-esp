#ifndef MOON_UI_H
#define MOON_UI_H

#include <stdbool.h>

#include "lvgl.h"

#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Moon phase UI layer (device side of components/moon).
 *
 * Owns the PSRAM frame buffers, the shared lv_image_dsc_t the moon screens
 * display, the free spin (in-plane rotation of the baked phase: a
 * faster rate while in use, a slow settled rate at rest -- the AMOLED
 * burn-in protection; each frame is written straight to the panel by
 * the render task, compositing over LVGL's on-panel UI via a
 * per-pixel keep-mask), the tilt-driven
 * out-of-plane orbit (QMI8658 IMU sphere model), the synodic sweep (the
 * new-moon -> now phase animation, played at BOOT from a black screen),
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
 *   moon_ui_start()  -- mark started: the moon is on a BLACK screen and
 *                       moon_ui_start kicks off the synodic boot sweep
 *                       (new moon -> now, moon_ui_replay_animation)
 *                       itself; the free spin resumes from the sweep's
 *                       last frame
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

/** Bake the current phase and start the free spin. */
void moon_ui_start(void);

/**
 * Play the synodic wake sweep (the last 29.5 days in
 * CONFIG_UI_MOON_ANIM_FRAMES frames, ending on the current phase)
 * right now. No-op when the moon has not started yet or while the
 * orbit / sweep pipeline owns the buffers. Called on every wake from
 * sleep by the UI layer (UI_MOON_ANIM_ENABLE).
 */
void moon_ui_replay_animation(void);

/**
 * Cheap day-change probe (call at most once per minute, e.g. from the clock
 * display update): re-bakes when the calendar day rolls over.
 */
void moon_ui_day_tick(void);

/**
 * Pause/resume the moon tick (sleep screens show a static moon). On
 * pause the moon PARKS: it renders one final frame at the exact
 * current wall-clock spin pose and displays it atomically, so the
 * sleep screens show the pose the clock says the moon is in (sleep is
 * the preparation for the full stop). A pause also joins a running
 * dusk (the light-sleep pole spin); the deep-sleep transition after a
 * completed dusk keeps the dark side on screen (no re-park).
 */
void moon_ui_pause(bool pause);

/**
 * Start the light-sleep "dusk" (the pole-axis spin to the fully
 * unilluminated side, 1 degree/frame, shortest arc). Call AFTER
 * moon_ui_pause(true) so the parked pose (in-plane spin angle + tilt
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
 * True while the synodic boot sweep's bake task is running. The
 * on-demand sync flow (actions.c) waits for it before switching the
 * WiFi screen to airplane, so the boot sweep is never interrupted by
 * the sync hand-off.
 */
bool moon_ui_sweeping(void);

/**
 * Display the new screen's contents (moon image included) as ONE
 * atomic whole-panel flush after a plain lv_scr_load. Must run in the
 * LVGL task context. No-op while a pipeline owns the buffer (the spin
 * or a bake completion then displays the screen atomically itself).
 * When a screen-switch fade-out ran (s_fade_pending, loadScreen), the
 * new screen fades IN instead of plain-flushing (UI_SCREEN_SWITCH_FADE).
 */
void moon_ui_screen_switched(void);

/**
 * Fade the current screen to black (LVGL task; loadScreen calls it
 * before lv_scr_load when the switch crosses the moon / moon-less
 * boundary). No-op when the fade is off, the moon UI is not ready, or
 * a bake pipeline owns the buffers.
 */
void moon_ui_fade_out(void);

/** Does the screen with this id carry a moon image (loadScreen's
 * fade decision: a switch that crosses the moon boundary fades, a
 * both-moon switch stays atomic). */
bool moon_ui_screen_is_moon(enum ScreensEnum id);

/**
 * Publish the current 90-degree screen orientation (0..3) to the spin
 * render: its panel writes pre-rotate each frame by the step and the
 * UI keep-mask is re-rotated to match. Safe from any task (one volatile
 * write); the mask itself is rebuilt on the LVGL task only.
 */
void moon_ui_set_panel_step(int step);

/**
 * Rebuild the spin's UI keep-mask (LVGL task only -- the mask is a
 * snapshot of the current screen with the moon image hidden). Call
 * after anything changes what the moon must NOT clobber: screen
 * switches and the clock/alarm label updates do this already; call
 * explicitly on an orientation step change.
 */
void moon_ui_rebuild_ui_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* MOON_UI_H */
