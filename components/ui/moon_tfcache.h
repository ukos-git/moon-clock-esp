/*
 * TF-card bake cache for the synodic boot sweep.
 *
 * The boot sweep (moon_ui.c, UI_MOON_ANIM_*) computes one gray8 bake per
 * frame at UI_MOON_ANIM_BAKE_PX; at the native defaults that is ~0.7 s
 * (S3 @400 px) to ~2 s (P4 @650 px) per frame, and the bake paces the
 * sweep. This module persists those bakes on the TF card as one small
 * gray JPEG per HOUR of the synodic cycle, bounded to one cycle (~709
 * files), so a warm replay reads frames instead of computing them --
 * the knob demo's "precomputed frames, constant cadence" lesson applied
 * to our pipeline (docs: sweep section, TF cache subsection).
 *
 * Contract (user spec 2026-11-07):
 *   - Cache lives under /moon_cache/<calcver>_<target>_<bakepx>/ ; the
 *     folder name carries the calculation version (MOON_CALC_VER, from
 *     git describe; repo versioned with commitizen), so a new bake build
 *     always starts from an EMPTY folder. A self-describing meta.txt in
 *     the folder additionally records the effective bake resolution (and
 *     albedo width / relief / JPEG q): ANY mismatch -- above all a
 *     resolution switch -- wipes and recreates the folder, so frames can
 *     never be served at the wrong resolution.
 *   - Missing card / failed mount / failed decode / detached mid-run:
 *     tfcache_ready() goes false, the sweep silently recomputes every
 *     frame on-device (the card-less path, unchanged code).
 *   - Write-through: a frame the sweep had to compute is stored, so the
 *     next boot within the cycle replays it.
 *   - One image per hour; adding an item evicts entries older than one
 *     synodic cycle from the newest cached hour.
 *
 * Threading: fetch() runs on the prefetch task, store() on the sweep
 * task; mount + scratch buffers are set up once in init (sweep task,
 * after the clock fix). All errors are silent-by-design -- they flip
 * s_ready and the sweep never blocks on the card.
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
    int64_t read_us, dec_us;    /* totals (prefetch task)                  */
    int64_t enc_us, wr_us;      /* totals (sweep task)                     */
    int64_t evict_us;           /* eviction scan total                     */
    int evict_del;              /* files evicted as older-than-one-cycle   */
    int enc_fmt;                /* probe result: 1 GRAY, 2 RGB565_LE, 3 RGB888 */
    const char *disable_reason; /* NULL while ready                        */
} tfcache_stats_t;

/* Mount the card (bsp_sdcard_mount via the board shim), create/select the
 * version folder (wiping siblings and any meta-mismatched folder), probe
 * the gray encoder (falling back to 565/888 duplicate), and allocate the
 * codec scratch sized for bake_px (clamped to moon_px by the caller).
 * Call ONCE, after the clock is fixed and before the sweep loop.
 * Never formats; never fails the caller -- !tfcache_ready() is normal. */
esp_err_t tfcache_init(int bake_px, int moon_px, int albedo_w, bool relief);

/* true while the cache is usable (mounted, folder ready, scratch up). */
bool tfcache_ready(void);

/* The sweep rejected the TF path this boot (card-less fallback): return
 * the codec scratch (the two eager PSRAM buffers, ~392 KB at 400px/q90
 * -- dead weight the classic path never touches while it starves the
 * spin mask's 434 KB lv_snapshot; the 3*px*px decode buffer joins them
 * if a hit ever allocated it). fetch/store then fail like any !ready
 * state. Only the sweep task may call it, after its TF decision (the
 * prefetch task is never started on the card-less path). */
void tfcache_scratch_free(void);

/* Allocate the persistent fetch-input buffer (the size a valid cached
 * file can reach). Call from the prefetch owner BEFORE allocating the
 * slot queue: with a full queue the PSRAM pool is down to a ~100 KB
 * head (8 MB S3), and a per-call fetch buffer then OOM'd at q90 file
 * sizes -- "cache off for this boot: no psram (fetch)", 2026-11-08.
 * false = no memory: stay on the card-less path. Idempotent. */
bool tfcache_fetch_buf_alloc(void);

/* true when the version folder already held cached hours at init. The
 * prefetch budget must reserve the deferred 3*px*px decode out-buffer
 * (allocated by the first real hit inside tfcache_fetch) while this is
 * true; a cold folder never decodes, so its PSRAM may go to slots. */
bool tfcache_folder_warm(void);

/* Decode /hour.3600.jpg into gray_out (S*S gray8, S == bake_px from
 * init). false = miss (no file / corrupt file -- the caller bakes and
 * stores); a hard IO error also flips ready to false. Records read/dec
 * time into the stats. Called from the prefetch task only. */
bool tfcache_fetch(int64_t hour, uint8_t *gray_out, int S);

/* Encode gray (S*S, S == bake_px) as a JPEG and write it under the hour
 * key; may run the periodic eviction scan. false = the write was skipped
 * or failed (stats counted); a card-gone error flips ready to false.
 * Records enc/write time. Called from the sweep task only. */
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
