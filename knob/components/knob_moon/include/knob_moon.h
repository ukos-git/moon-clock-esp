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
 * clock's LUT authority (the sweep-era name): classify the display epoch
 * ONCE, arm the gray->565 tint LUT, then let every frame convert through
 * it.
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
 * (moon_gray565_set_event) and remembers the event so bake_display()
 * passes the eclipse occlusion on it. The producer arms it before the
 * opening presentation and the day tick re-arms it when the signature
 * changes.
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
 * @brief Bake one display frame crisp at `size` (the only posture since
 *        the boot sweep retired 2026-11-14): gray bake (relief-displaced
 *        Lambert, COMPUTED backlit dark side, dk_sup 0 -- the wake/home
 *        posture) then gray->RGB565 through the armed LUT into out.
 *
 * The armed event always passes (every frame here is a current-phase or
 * canonical-demo frame; the sweep's past-phase intermediates that baked
 * without the occlusion are gone). Row-by-row bake (moon_bake_rows) with
 * a yield every 16 rows keeps the idle/WDT fed.
 *
 * @param out         size*size RGB565 (caller-owned PSRAM)
 * @param size        square edge in px (360 on this panel)
 * @param radius      disk radius in px
 * @param cx,cy       disk center in px
 * @param utc_epoch   UTC seconds since 1970 (the caller owns the time
 *                    source: KNOB_MOON_TEST_EPOCH until knob_time, K7d)
 * @param bake_ms_out optional, filled with the bake wall time
 * @return false on allocation failure (out then undefined)
 *
 * Every frame logs the bake line the bench acceptance reads
 * (illum/ss/dist).
 */
bool knob_moon_bake_display(uint16_t *out, int size, float radius, int cx, int cy, int64_t utc_epoch,
                            int64_t *bake_ms_out);

/**
 * @brief The last NEW MOON at or before utc_epoch: coarse 6 h then fine
 *        20 min illumination-minimum scan over one synodic month (the
 *        clock's new_moon_epoch, verbatim logic). ~hundreds of
 *        moon_geometry calls; yields periodically. The demo tour's only
 *        helper (the boot sweep, its other user, retired 2026-11-14).
 */
int64_t knob_moon_new_moon_epoch(int64_t utc_epoch);

#ifdef __cplusplus
}
#endif
