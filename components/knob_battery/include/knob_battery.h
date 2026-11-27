/**
 * @file knob_battery.h
 * @brief P4 battery gauge: GPIO1 sense through the 10K/10K divider,
 *        linear percentage on the placeholder LiPo calibration.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Init the ADC1 channel + the fuse calibration scheme. Safe to call
 * once from app_main; with CONFIG_KNOB_BATTERY_GAUGE=n everything
 * below reports unavailable and the overlay never appears. */
void knob_battery_init(void);

/* True once init landed an ADC handle (n-gauge: always false). */
bool knob_battery_available(void);

/* Calibrated cell millivolts on the CELL side of the divider
 * (sense * KNOB_BATTERY_DIV_NUM / DEN); -1 on a failed read. */
int knob_battery_cell_mv(void);

/* 0..100 between KNOB_BATTERY_MV_EMPTY..MV_FULL (clamped); -1 when
 * no reading is available. */
int knob_battery_percent(void);

/* Heuristic "on the dock" verdict: true when a fresh cell read sits at
 * or above KNOB_BATTERY_CHARGING_MV (0 -> always false). There is no
 * charge-state line on the board; the sense ADC reads the USB rail
 * when plugged (bench ~4522 mV, far over the cell ceiling), so this
 * means "charging", not "charging right now" -- the same honesty class
 * as the placeholder percentage. False with no reading / no gauge. */
bool knob_battery_charging(void);

#ifdef __cplusplus
}
#endif
