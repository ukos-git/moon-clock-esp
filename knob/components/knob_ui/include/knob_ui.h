/**
 * @file knob_ui.h
 * @brief The knob's single view (K7b static + K7c sweep/day tick): the
 *        physical moon disk + a caption row. Glue only — the bake path
 *        lives in knob_moon (the verbatim ported core); the dial stays
 *        idle until K7e (settings/power) gives it a job.
 *
 * Threading (K7c): a producer task bakes into the back PSRAM buffer and
 * publishes a frame counter; a swap task (holding the LVGL mutex handed
 * in via knob_ui_set_lvgl_lock) swaps the image descriptor, runs ONE
 * atomic lv_refr_now and acks. The producer never touches a buffer the
 * panel might still be reading (it waits for the ack before reusing).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h" /* must precede semphr.h (IDF rule) */
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hand the view the mutex that guards LVGL (the one lvgl_task
 *         holds around lv_timer_handler in main.c), BEFORE knob_ui_create.
 *         The swap task takes it around every descriptor swap + flush. */
void knob_ui_set_lvgl_lock(SemaphoreHandle_t mux);

/** @brief Build the screen (image objects + caption), before the LVGL task
 *         starts — same context as the old K1 test screen. Allocates the
 *         PSRAM double buffer; the caption reads "moon unavailable" until
 *         the first frame lands. */
void knob_ui_create(void);

/** @brief Cold-boot sweep capability (Kconfig KNOB_MOON_SWEEP_FRAMES > 0). */
bool knob_ui_sweep_enabled(void);

/**
 * @brief Show one UTC instant statically and drop into the idle day-tick
 *        loop (1 Hz UTC check; re-bakes on date change or event-signature
 *        change, K7c). Also the deep-sleep-wake posture: current phase,
 *        no sweep (the clock's wake policy).
 *
 * The first bake runs in the CALLER's task — main.c calls this once on
 * the boot core before the LVGL task is created, so a multi-second bake
 * never blocks rendering. The producer + swap tasks start here.
 *
 * @return false if PSRAM or the assets failed (caption stays honest)
 */
bool knob_ui_show_moon(int64_t utc_epoch);

/**
 * @brief Cold-boot-only synodic sweep (new moon -> now), then the same
 *        idle day-tick loop. Never-black: the caption counts
 *        "sweeping n/N" from frame 1, and a screen press aborts the
 *        sweep — the display then lands on the true current phase.
 *
 * Returns immediately after arming the sweep (the boot core must go on
 * starting the LVGL task; the bake runs on the producer).
 */
void knob_ui_start_sweep(int64_t now_epoch);

#ifdef __cplusplus
}
#endif
