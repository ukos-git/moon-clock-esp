/**
 * @file knob_moon.h
 * @brief The knob's moon display core (K7): the clock's portable moon core
 *        (components/moon/core, verbatim) + the ESP partition asset backend,
 *        wrapped for the knob. Physics-only -- no lvgl/esp_lcd in here (the
 *        lv_image glue is knob_ui/knob_moon_view.c), so the frame path stays
 *        identical to the host gate (host_sim --target knob).
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
 * @brief Bake the moon for one UTC instant into an RGB565 square.
 *
 * The full device path: moon_geometry -> moon_bake (relief-displaced
 * Lambert, computed backlit dark side, tone LUT) -> identity moon_rotate
 * through the warm gray->565 LUT. Runs in the caller's task (the LVGL
 * task for the static frame); the bake is pure CPU, no panel access.
 *
 * @param out   size*size RGB565 (caller-owned; the view keeps one PSRAM
 *              buffer and hands it to lv_image)
 * @param size  square edge in px (360 on this panel)
 * @param radius disk radius in px
 * @param cx,cy disk center in px
 * @param utc_epoch UTC seconds since 1970 (the caller owns the time
 *              source: KNOB_MOON_TEST_EPOCH until knob_time, K7d)
 * @return false on allocation failure (out is then untouched)
 */
bool knob_moon_render(uint16_t *out, int size, float radius, int cx, int cy, int64_t utc_epoch);

#ifdef __cplusplus
}
#endif
