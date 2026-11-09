/**
 * @file knob_battery_ui.h
 * @brief P4 battery gauge overlay on the shared home screen (knob-side;
 *        the shared screens.c stays clock-identical).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Post ui_init, pre the LVGL task (the single-threaded creation phase,
 * like knob_dial_start). No-op unless the gauge is configured AND the
 * ADC came up (knob_battery_available). */
void knob_battery_ui_attach(void);

#ifdef __cplusplus
}
#endif
