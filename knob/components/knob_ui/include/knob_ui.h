/**
 * @file knob_ui.h
 * @brief The knob's single view (K7b): the physical moon disk + a caption
 *        row. Glue only — the bake path lives in knob_moon (the verbatim
 *        ported core); the dial and touch stay idle until K7c/K7e give
 *        them jobs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Build the screen (image object + caption), before the LVGL task
 *         starts — same context as the old K1 test screen. The caption
 *         reads "moon unavailable" until the first frame lands. */
void knob_ui_create(void);

/**
 * @brief Bake one UTC instant into the PSRAM frame buffer and show it.
 *
 * Runs the full device path (knob_moon_render) in the CALLER's task —
 * main.c calls this once on the boot core before the LVGL task is
 * created, so a multi-second bake never blocks rendering (the screen
 * shows the caption first, then the disk; never a black-only screen).
 * Call again later for re-bakes (day tick, K7c) — then from the LVGL
 * task with the mutex held.
 *
 * @return false if PSRAM or the assets failed (caption stays honest)
 */
bool knob_ui_show_moon(int64_t utc_epoch);

#ifdef __cplusplus
}
#endif
