/**
 * @file knob_power.h
 * @brief Power posture for the knob moon display (K7e, task #24): the
 *        idle ladder active -> backlight dim -> deep sleep, woken by a
 *        dial detent (EXT0 on encoder-A GPIO 8, active-LOW).
 *
 * The supervisor owns the only esp_deep_sleep_start() in the firmware.
 * "Activity" = a touch contact or a dial detent (polled), plus the
 * presentation hold: while the cold-boot bake presents the display
 * must not nap (the clock's idle-hold-during-presentation posture;
 * the boot sweep that once set it retired 2026-11-14).
 *
 * EXT0 caveat (documented, bench-verified): the EC1 detents pulse ONE
 * channel each, alternating A/B -- a detent that happens to pulse only
 * B (GPIO 7, not the wake pad) is MISSED; the next detent in either
 * direction pulses A and wakes. One missed first detent, never a dead
 * device. The power button is a hardware rail switch (back of the
 * board), not a firmware input.
 */
#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h" /* must precede semphr.h (IDF rule) */
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Start the idle-ladder supervisor (call once from app_main,
 *         after the backlight and the dial exist). */
void knob_power_init(void);

/** @brief Mark "the user just did something": resets the idle timer and
 *         un-dims. Cheap -- callable from the touch read callback. */
void knob_power_notify_activity(void);

/** @brief Presentation hold (the boot bake, the demo tour): while held,
 *         the ladder freezes (and the panel stays un-dimmed); on
 *         release the idle clock restarts from now. */
void knob_power_set_hold(bool hold);

/** @brief Ladder dim/undim observer (P1 dim parity): called with true
 *         when the backlight drops to KNOB_DIM_DUTY and with false when
 *         activity brings it back up (supervisor task context). main.c
 *         registers knob_render_set_home_dim so the shared clock widget
 *         dims its opacity alongside the backlight, matching the clocks'
 *         dusk. NULL (default) = backlight dim only. */
typedef void (*knob_power_dim_cb_t)(bool dimmed);
void knob_power_set_dim_cb(knob_power_dim_cb_t cb);

/** @brief Hand the ladder the UI's LVGL mutex (shared-UI path): the
 *         light/deep-sleep rungs load the family sleep screens from the
 *         supervisor task and take it around the screen load so the
 *         screen_switched atomic flush + the object writes are one
 *         critical section (the same handle the boot arm and the
 *         knob_ui seam use; main passes it before the ladder runs).
 *         Legacy own-screen path (KNOB_UI_ENABLE=n): not needed. */
void knob_power_set_lvgl_lock(SemaphoreHandle_t mux);

/** @brief Epoch arithmetic for the fixed-time alarm rule (the TIMER
 *         deep-sleep arm and the wake arbitration share this one truth).
 *         Fills prev (last rule occurrence at or before now_epoch) and/or
 *         next (first occurrence after it) with LOCAL-time-correct
 *         epochs (mktime, tm_isdst=-1 — the scheduler's own mirror).
 *         @return false when no fixed rule is loaded (interval/pomodoro
 *         only) or the weekday mask is empty; outputs stay 0. */
bool knob_power_rule_epochs_around(int64_t now_epoch, int64_t *prev, int64_t *next);

#ifdef __cplusplus
}
#endif
