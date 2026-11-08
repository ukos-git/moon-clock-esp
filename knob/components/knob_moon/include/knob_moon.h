/**
 * @file knob_moon.h
 * @brief The knob's moon display core (K7, task #24): the clock's portable
 *        moon core (components/moon/core, verbatim) + the ESP partition asset
 *        backend, wrapped for the knob. Physics-only -- no lvgl/esp_lcd in
 *        here (the lv_image glue is knob_ui), so the frame path stays
 *        identical to the host gate (host_sim --target knob).
 *
 * Event policy lives HERE, not in the view: the view never includes a core
 * header (the moon_bake_ev_t type stays private), it only drives
 * display_begin()/classify_signature()/bake_display(). That mirrors the
 * clock's sweep LUT authority: classify the display epoch ONCE, arm the
 * gray->565 tint LUT, then let every frame convert through it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Eagerly load the assets (albedo to PSRAM, relief gradient cache
 *        built from the moon_assets partition) and report readiness.
 *
 * Call from app_main BEFORE the LVGL task is created (the proven
 * moon_esp_assets.c posture: one eager build on the main task, no
 * concurrent-build race). Idempotent -- the backend caches internally.
 *
 * @return false if the partition is missing/too small or PSRAM failed
 *         (the view then shows its "moon unavailable" caption instead
 *         of a wrong moon).
 */
bool knob_moon_init(void);

/**
 * @brief Arm the display epoch's sky event (the LUT authority, K7c).
 *
 * Classifies the instant, sets the gray->RGB565 tint LUT
 * (moon_gray565_set_event) and remembers the event so bake_display() can
 * pass the eclipse occlusion when asked. The sweep calls this once for
 * the whole sweep (the special moon's color foreshadows as the disk
 * brightens -- the clock's deliberate posture); the day tick calls it
 * again when the signature changes.
 *
 * @return the event's 5-bit signature (0 = an ordinary instant)
 */
int knob_moon_display_begin(int64_t utc_epoch);

/**
 * @brief The instant's event signature WITHOUT touching the LUT
 *        (moon_event_signature of a fresh classification). The day tick
 *        polls this at 1 Hz and re-arms via display_begin() on change
 *        (an eclipse starts at the hour it happens, not at UTC
 *        midnight).
 */
int knob_moon_classify_signature(int64_t utc_epoch);

/**
 * @brief Bake one display frame: gray bake (relief-displaced Lambert,
 *        COMPUTED backlit dark side, dk_sup 0 -- the wake/home posture)
 *        at bake_px, then gray->RGB565 through the armed LUT into out,
 *        nearest-neighbour upscale when bake_px < size (the clock's
 *        sweep upcase: src = (2i+1)*S/(2*size), exact int mapping).
 *
 * bake_px == size is the crisp path; a sweep's FINAL frame and every
 * static frame MUST go through it so the sweep end frame is
 * byte-identical to the FRAMES=0 static build (same scratch, same LUT,
 * same conversion). Row-by-row bake (moon_bake_rows) with a yield every
 * 16 rows keeps the idle/WDT fed and honors the abort flag mid-frame.
 *
 * @param out         size*size RGB565 (caller-owned PSRAM)
 * @param size        square edge in px (360 on this panel)
 * @param radius      disk radius in px
 * @param cx,cy       disk center in px
 * @param utc_epoch   UTC seconds since 1970 (the caller owns the time
 *                    source: KNOB_MOON_TEST_EPOCH until knob_time, K7d)
 * @param bake_px     bake edge (<= size; 0/>size = full size)
 * @param pass_event  pass the armed event to the bake (the current
 *                    phase's frame; intermediate past phases pass false
 *                    and bake byte-identically to the pre-event bake)
 * @param abort_flag  may flip true at any time from any task (touch);
 *                    the row loop checks it per row
 * @param bake_ms_out optional, filled with the bake wall time
 * @return false on allocation failure or abort (out then undefined /
 *         partially written -- never publish an aborted frame)
 *
 * Every CURRENT-phase frame (pass_event: the static frame, the sweep
 * final, the day tick) logs the bake line the bench acceptance reads
 * (illum/ss/dist).
 */
bool knob_moon_bake_display(uint16_t *out, int size, float radius, int cx, int cy, int64_t utc_epoch, int bake_px,
                            bool pass_event, const volatile bool *abort_flag, int64_t *bake_ms_out);

/**
 * @brief The last NEW MOON at or before utc_epoch: coarse 6 h then fine
 *        20 min illumination-minimum scan over one synodic month (the
 *        clock's new_moon_epoch, verbatim logic). ~hundreds of
 *        moon_geometry calls; yields periodically, honors the abort
 *        flag (the partial best is good enough when the sweep dies).
 */
int64_t knob_moon_new_moon_epoch(int64_t utc_epoch, const volatile bool *abort_flag);

#ifdef __cplusplus
}
#endif
