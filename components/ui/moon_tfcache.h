/*
 * TF-card hour cache for the once-per-hour moon refresh.
 *
 * The hourly refresh (moon_ui.c, UI_MOON_HOURLY_ENABLE) computes one
 * gray8 bake per hour at MOON_PX; at the native resolution that is ~1.3 s
 * (S3 @400 px) to ~2.4 s (P4 @650 px), and the produce pass runs while the
 * panel sits black. This module persists those bakes on the TF card as one
 * small gray JPEG per HOUR of the synodic cycle, bounded to one cycle
 * (~709 files), so a warm refresh decodes the hour instead of baking it --
 * the knob demo's "precomputed frames, constant cadence" lesson applied to
 * our pipeline (docs: hourly-refresh section, TF hour-cache subsection).
 *
 * Contract:
 *   - Cache lives under /moon_cache/<calcver>_<target>_<bakepx>/ ; the
 *     folder name carries the calculation version (MOON_CALC_VER, from
 *     git describe; repo versioned with commitizen), so a new bake build
 *     always starts from an EMPTY folder. A self-describing meta.txt in
 *     the folder additionally records the effective bake resolution (and
 *     albedo width / relief / JPEG q): ANY mismatch -- above all a
 *     resolution switch -- wipes and recreates the folder, so frames can
 *     never be served at the wrong resolution.
 *   - The bake key is the HOUR CENTRE (hour * 3600 + 1800): stable across
 *     boots and boards, so a card seeded by one boot serves the same byte-
 *     identical gray to the next.
 *   - Missing card / failed mount / failed decode / detached mid-run:
 *     tfcache_ready() goes false, the hourly refresh silently bakes every
 *     hour on-device (the card-less path, unchanged code).
 *   - Write-through: an hour the refresh had to compute is stored, so a
 *     later pass within the cycle decodes it.
 *   - One image per hour; adding an item evicts entries older than one
 *     synodic cycle from the newest cached hour.
 *
 * Threading: fetch() and store() run on the hourly producer task (core 1,
 * a single caller -- there is no prefetch task any more); mount + scratch
 * buffers are set up once in tfcache_init, called lazily by that producer
 * on its first refresh (boot never touches the SD bus). All errors are
 * silent-by-design -- they flip s_ready and the refresh never blocks on the
 * card.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int hits;                   /* fetch()s that decoded from the card    */
    int misses;                 /* fetch()s that found no/undecodable file */
    int puts, put_fails;        /* store() attempts / IO+encode failures   */
    long long bytes_read;       /* JPEG bytes read from the card           */
    long long bytes_written;    /* JPEG bytes written to the card          */
    int64_t read_us, dec_us;    /* totals (hourly producer, fetch)         */
    int64_t enc_us, wr_us;      /* totals (hourly producer, store)         */
    int64_t evict_us;           /* eviction scan total                     */
    int evict_del;              /* files evicted as older-than-one-cycle   */
    int enc_fmt;                /* probe result: 1 GRAY, 2 RGB565_LE, 3 RGB888 */
    const char *disable_reason; /* NULL while ready                        */
} tfcache_stats_t;

/* Mount the card (bsp_sdcard_mount via the board shim), create/select the
 * version folder (wiping siblings and any meta-mismatched folder), probe
 * the gray encoder (falling back to 565/888 duplicate), and allocate the
 * codec scratch sized for bake_px (clamped to moon_px by the caller).
 * Call ONCE from the hourly producer's first refresh (after the clock is
 * fixed; boot never mounts the card).
 * Never formats; never fails the caller -- !tfcache_ready() is normal. */
esp_err_t tfcache_init(int bake_px, int moon_px, int albedo_w, bool relief);

/* true while the cache is usable (mounted, folder ready, scratch up). */
bool tfcache_ready(void);

/* Allocate the persistent fetch-input buffer (the size a valid cached
 * file can reach). Call from the hourly producer right after init, before
 * the first fetch: a per-call fetch buffer OOM'd at q90 file sizes once
 * the PSRAM pool tightened -- "cache off for this boot: no psram
 * (fetch)", 2026-11-08. false = no memory: stay on the card-less path.
 * Idempotent. The scratch stays resident for the boot (the spin's mask
 * snapshot that once needed it back is gone). */
bool tfcache_fetch_buf_alloc(void);

/* Decode /hour.3600.jpg into gray_out (S*S gray8, S == bake_px from
 * init). false = miss (no file / corrupt file -- the caller bakes and
 * stores); a hard IO error also flips ready to false. Records read/dec
 * time into the stats. Called from the hourly producer task only. */
bool tfcache_fetch(int64_t hour, uint8_t *gray_out, int S);

/* Encode gray (S*S, S == bake_px) as a JPEG and write it under the hour
 * key; may run the periodic eviction scan. false = the write was skipped
 * or failed (stats counted); a card-gone error flips ready to false.
 * Records enc/write time. Called from the hourly producer task only. */
bool tfcache_store(int64_t hour, const uint8_t *gray, int S);

/* Evict files older than one synodic cycle relative to newest_hour
 * (hour * 3600). Cheap: runs a directory scan only when more than a day
 * of new hours accumulated since the last scan. */
void tfcache_evict(int64_t newest_hour);

/* Turn the cache off for the rest of the boot (reason logged once). */
void tfcache_disable(const char *reason);

const tfcache_stats_t *tfcache_stats(void);

/* One ESP_LOGI block of the stats for the perf table (hits/misses, KB,
 * ms, enc_fmt, disable reason). */
void tfcache_stats_log(void);

#ifdef __cplusplus
}
#endif
