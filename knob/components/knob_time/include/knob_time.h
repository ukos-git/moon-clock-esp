/**
 * @file knob_time.h
 * @brief The knob's wall clock (K7d, task #24): one-shot esp_sntp over the
 *        existing knob_link Wi-Fi join. The board has no RTC -- SNTP truth
 *        or nothing.
 *
 * Trust posture (the clock's s_time_trusted lesson, kept small): valid
 * comes ONLY from an SNTP answer with a plausible epoch (> 2021-01-01).
 * Until then knob_time_valid() stays false and the moon view refuses to
 * bake a "current" phase -- the glass shows "no time" rather than a
 * silently wrong moon. While awake, lwIP's POLL mode keeps re-syncing
 * (CONFIG_LWIP_SNTP_UPDATE_DELAY, hourly), which also covers the "daily
 * re-sync" the plan asks for; K7e's power manager will bound the Wi-Fi
 * window and re-arm the sync per wake.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure + start the SNTP client (POLL mode, KNOB_NTP_SERVER).
 *
 * Idempotent -- later calls are no-ops once started (a started POLL
 * client re-transmits and rotates on its own; no restart is needed).
 * Call it when an IP is held: a client started pre-association can park
 * on a failed DNS, so the boot waiter re-polls the join and calls this
 * again until it takes.
 */
void knob_time_start_sync(void);

/** @brief True once an SNTP answer with a plausible epoch landed (the
 *         system clock is set at that moment by the lwIP client). */
bool knob_time_valid(void);

/** @brief Wall-clock seconds since 1970, or 0 when no trusted time --
 *         never a 1970 or an invented default an arm path could bake. */
int64_t knob_time_now_epoch(void);

/** @brief Block (yielding) until valid or timeout; returns knob_time_valid.
 *         Safe before start_sync (just polls the flag until the timeout). */
bool knob_time_wait_synced(uint32_t timeout_ms);

/**
 * @brief Persist the current trusted instant to NVS (no-op without one).
 *
 * The SNTP callback stores every plausible answer itself; the power
 * supervisor calls this right before esp_deep_sleep_start so the next
 * boot can arm the static moon without waiting for Wi-Fi + NTP.
 */
void knob_time_snapshot_save(void);

/** @brief The last snapshotted plausible epoch (> 2021-01-01), or 0.
 *         Its age is unverifiable offline -- callers arm on it only as a
 *         "last known" fallback and must let a late SNTP fix re-bake. */
int64_t knob_time_snapshot_epoch(void);

#ifdef __cplusplus
}
#endif
