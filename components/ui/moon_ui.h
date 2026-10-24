#ifndef MOON_UI_H
#define MOON_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Moon phase UI layer (device side of components/moon).
 *
 * Owns the PSRAM frame buffers, the shared lv_image_dsc_t the moon screens
 * display, the in-plane "ball in hand" rotation (device tilt via the
 * QMI8658 IMU + touch drag), and the boot animation over the last synodic
 * month.
 *
 * Call order (app_main, under the display lock, before ui_init):
 *   moon_ui_init()   -- PSRAM buffers + shared image descriptor + timers
 *   create_screens() -- each moon screen calls moon_ui_create_image(parent)
 *                       FIRST so the moon sits below the clock (z-order)
 *   moon_ui_start()  -- bake the current phase + run the boot animation
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

/** Bake the current phase and start the boot animation (last synodic month). */
void moon_ui_start(void);

/**
 * Cheap day-change probe (call at most once per minute, e.g. from the clock
 * display update): re-bakes when the calendar day rolls over.
 */
void moon_ui_day_tick(void);

/** Pause/resume the moon tick (sleep screens show a static moon). */
void moon_ui_pause(bool pause);

#ifdef __cplusplus
}
#endif

#endif /* MOON_UI_H */
