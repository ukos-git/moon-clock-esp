/**
 * @file knob_power.h
 * @brief Power posture for the knob moon display (K7e, task #24): the
 *        idle ladder active -> backlight dim -> deep sleep, woken by a
 *        dial detent (EXT0 on encoder-A GPIO 8, active-LOW).
 *
 * The supervisor owns the only esp_deep_sleep_start() in the firmware.
 * "Activity" = a touch contact or a dial detent (polled), plus the
 * sweep hold: while the cold-boot sweep plays the display is presenting
 * and must not nap (the clock's idle-hold-during-sweep posture).
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

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Start the idle-ladder supervisor (call once from app_main,
 *         after the backlight and the dial exist). */
void knob_power_init(void);

/** @brief Mark "the user just did something": resets the idle timer and
 *         un-dims. Cheap -- callable from the touch read callback. */
void knob_power_notify_activity(void);

/** @brief Presentation hold (the sweep): while held, the ladder freezes
 *         (and the panel stays un-dimmed); on release the idle clock
 *         restarts from now. */
void knob_power_set_hold(bool hold);

#ifdef __cplusplus
}
#endif
