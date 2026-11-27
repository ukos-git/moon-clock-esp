/**
 * @file knob_time.h
 * @brief Knob time bridge: the boot arm's one-time access to the pre-seam
 *        firmware's NVS time snapshot. Time truth itself is the clock's
 *        clock_sntp.c (trust flag, "clock"/"epoch" snapshot, hourly POLL
 *        discipline) — see knob/main/main.c boot_arm_task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The legacy namespace's plausible (> 2021-01-01) epoch, or 0. */
int64_t knob_time_legacy_snapshot_epoch(void);

/** @brief settimeofday() from the legacy snapshot (untrusted). Returns
 *         true when applied (a plausible legacy value existed). */
bool knob_time_adopt_legacy_snapshot(void);

#ifdef __cplusplus
}
#endif
