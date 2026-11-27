/**
 * @file knob_dial.h
 * @brief P2: the EC1 dial as a first-class input for the shared screens
 *        (docs/implementation/knob-display.rst "Dial semantics").
 *
 * The dial steps one per-screen edit cursor (screens.c's knob-only
 * dial surface does the per-screen dispatch); a tap commits it. A turn
 * is ALWAYS activity (the dim ladder un-dims), also on the moon screens
 * where the dial has nothing to step. Touch stays fully working: the
 * dial is additive, and every commit rides the native CLICKED/RELEASED
 * handlers.
 *
 * Only valid on the KNOB_UI_ENABLE path (call after ui_init — the
 * screen objects must exist; before the LVGL task is created, like
 * ui_init itself).
 */
#pragma once

/** Attach the tap-to-commit listener to every settings screen root,
 *  init the screens.c dial focus state and start the dial task.
 *  Call from app_main after ui_init (single-threaded UI-creation
 *  phase; the dial task takes the LVGL mutex for every step). */
void knob_dial_start(void);
