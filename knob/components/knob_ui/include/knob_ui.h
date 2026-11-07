/**
 * @file knob_ui.h
 * @brief The knob's two mirror modes (K4): KEYBOARD (26-letter ring,
 *        dwell-to-commit) and STEPPER (field list, dial steps the clock's
 *        rollers). The clock is authoritative — the knob is a dumb
 *        remote; frames are gated against the clock's CURRENT screen.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Build both views + the input timer (call after display/touch/
 *         encoder init, before the LVGL task starts — same context as the
 *         old K1 test screen). */
void knob_ui_create(void);

#ifdef __cplusplus
}
#endif
