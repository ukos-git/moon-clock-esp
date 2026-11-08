/**
 * @file knob_moon.c
 * @brief Thin wrapper over the verbatim ported core (K7b): asset load
 *        readiness + the one-frame bake path.
 *
 * The bake call sequence mirrors host_sim/main.c render_one() (the gate's
 * own path) 1:1 -- moon_geometry, then moon_bake with the relief gradient
 * cache, then the RGB565 conversion through the identity moon_rotate. The
 * dark side is the COMPUTED backlit surface (dark_zero = 1, the clock's
 * wake/home posture; dk_sup stays 0.0f -- that bound is dusk-only), the
 * display LUT stays at its warm default (moon_gray565_set_warm's on-state;
 * the gate renders --neutral, the physics is unaffected).
 */
#include "knob_moon.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

static const char *TAG = "knob_moon";

/* Bake scratch (the 8-bit gray frame), PSRAM, kept for the largest size
 * asked for (360 on this panel -- realloc only if a caller changes it). */
static uint8_t *s_gray;
static int s_gray_size;

bool knob_moon_init(void) {
    const uint8_t *alb = moon_albedo();      /* loads + logs (partition -> PSRAM) */
    const moon_grel_t *grel = moon_relief(); /* builds + logs the cache */
    if (!alb || !grel || !grel->pack) {
        ESP_LOGE(TAG, "moon assets unavailable (albedo %s, relief %s) -- moon view will not render",
                 alb ? "ok" : "MISSING", grel && grel->pack ? "ok" : "MISSING");
        return false;
    }
    ESP_LOGI(TAG, "moon assets ready: albedo %dx%d, relief %dx%d, PSRAM free %d KB", MOON_ALB_W, MOON_ALB_H, MOON_DEM_W,
             MOON_DEM_H, (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return true;
}

bool knob_moon_render(uint16_t *out, int size, float radius, int cx, int cy, int64_t utc_epoch) {
    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = moon_relief();
    if (!out || size <= 0 || !alb || !grel || !grel->pack) {
        return false;
    }
    if (!s_gray || s_gray_size != size) {
        free(s_gray);
        s_gray = heap_caps_malloc((size_t)size * size, MALLOC_CAP_SPIRAM);
        s_gray_size = s_gray ? size : 0;
        if (!s_gray) {
            ESP_LOGE(TAG, "PSRAM bake scratch (%d KB) failed", (int)((size_t)size * size / 1024));
            return false;
        }
    }

    moon_geom_t g;
    moon_geometry(utc_epoch, &g);
    const int64_t t0 = esp_timer_get_time();
    moon_bake(&g, alb, grel, s_gray, size, radius, (float)cx, (float)cy, MOON_ALB_W, MOON_ALB_H,
              /*dark_zero=*/1, /*dk_sup=*/0.0f, /*bev=*/NULL);
    /* Identity rotate = the gray->RGB565 LUT pass through the exact panel
     * path the gate diffs (cos(0) = 32768 in Q15.15, sin(0) = 0). */
    moon_rotate(s_gray, size, out, size, cx, cy, cx, cy, 32768, 0);
    ESP_LOGI(TAG, "bake %dx%d r=%.0f in %lld ms (illum %.1f%%, ss=(%.2f,%.2f), dist %.0f km)", size, size, radius,
             (long long)(esp_timer_get_time() - t0) / 1000, g.illum * 100.0, (double)g.subsolar_lon,
             (double)g.subsolar_lat, (double)g.distance_km);
    return true;
}
