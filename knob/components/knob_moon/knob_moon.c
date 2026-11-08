/**
 * @file knob_moon.c
 * @brief The knob's moon display core (K7b bake path + K7c sweep/event
 *        plumbing) over the verbatim ported clock core.
 *
 * The bake call sequence mirrors host_sim/main.c render_one() (the gate's
 * own path) 1:1 -- moon_geometry, then row-by-row moon_bake_rows (the
 * abort/yield variant, same per-pixel math) with the relief gradient
 * cache, then the gray->RGB565 conversion through moon_gray565. That is
 * the EXACT LUT table the identity moon_rotate consults (cos(0) = 32768
 * in Q15.15, sin(0) = 0 -- an identity gather), so every frame path here
 * is pixel-identical to the K7b rotate path, and a sweep's final frame
 * (crisp bake_px == size, event passed) is byte-identical to the
 * FRAMES=0 static build -- K7c's acceptance by construction, not hope.
 * The dark side is the COMPUTED backlit surface (dark_zero = 1, the
 * clock's wake/home posture; dk_sup stays 0.0f -- that bound is
 * dusk-only).
 *
 * K7c event policy (the clock's sweep LUT authority, verbatim): the
 * sweep classifies the display instant ONCE via display_begin(), which
 * arms the global 256-entry tint LUT before the first frame -- the
 * special moon's color foreshadows as the disk brightens. bake_display()
 * passes the eclipse OCCLUSION only on the current-phase frame (the
 * intermediate frames are past phases up to a month back, not
 * eclipsed -- they bake byte-identically to the pre-event bake).
 */
#include "knob_moon.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"
#include "moon_event.h"

static const char *TAG = "knob_moon";

/* Bake scratch (the 8-bit gray frame), PSRAM, kept for the LARGEST edge
 * ever asked for (the sweep's 128 and the panel's 360 share it; the
 * realloc only ever happens upward). */
static uint8_t *s_gray;
static int s_gray_size;

/* The armed sky event (K7c): display_begin writes it, bake_display reads
 * it on pass_event. Single-writer discipline: only the boot core before
 * the producer task starts, then only the producer task. */
static moon_event_t s_ev;
static bool s_ev_armed;

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

static bool ensure_gray(int edge) {
    if (s_gray && s_gray_size >= edge) {
        return true;
    }
    free(s_gray);
    s_gray = heap_caps_malloc((size_t)edge * edge, MALLOC_CAP_SPIRAM);
    s_gray_size = s_gray ? edge : 0;
    if (!s_gray) {
        ESP_LOGE(TAG, "PSRAM bake scratch (%d KB) failed", (int)((size_t)edge * edge / 1024));
        return false;
    }
    return true;
}

int knob_moon_display_begin(int64_t utc_epoch) {
    moon_event_classify(utc_epoch, &s_ev);
    moon_gray565_set_event(&s_ev);
    s_ev_armed = true;
    const int sig = moon_event_signature(&s_ev);
    ESP_LOGI(TAG, "display epoch %lld armed: sig 0x%02x%s%s%s%s%s", (long long)utc_epoch, sig,
             s_ev.blood ? " blood" : "", s_ev.harvest ? " harvest" : "", s_ev.blue ? " blue" : "",
             s_ev.super_moon ? " super" : "", s_ev.micro_moon ? " micro" : "");
    return sig;
}

int knob_moon_classify_signature(int64_t utc_epoch) {
    moon_event_t ev;
    moon_event_classify(utc_epoch, &ev);
    return moon_event_signature(&ev);
}

bool knob_moon_bake_display(uint16_t *out, int size, float radius, int cx, int cy, int64_t utc_epoch, int bake_px,
                            bool pass_event, const volatile bool *abort_flag, int64_t *bake_ms_out) {
    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = moon_relief();
    if (!out || size <= 0 || !alb || !grel || !grel->pack) {
        return false;
    }
    int S = bake_px;
    if (S <= 0 || S > size) {
        S = size; /* crisp path (final/current-phase frames, static builds) */
    }
    if (!ensure_gray(S)) {
        return false;
    }

    moon_geom_t g;
    moon_geometry(utc_epoch, &g);
    /* The S-square holds the DISPLAY disk scaled down (radius/cx/cy by
     * S/size), so the nearest-neighbour upscale maps it back to the
     * exact display geometry: sweep frames differ from the crisp frame
     * in sharpness only, never in disk size/position. (The clock's sweep
     * bakes a full-square disk and never needed this -- its moon fills
     * MOON_PX; the knob's r150@cy168 disk does not.) */
    const float k = (float)S / (float)size;
    const moon_bake_ev_t *bev = (pass_event && s_ev_armed && moon_event_any(&s_ev)) ? &s_ev : NULL;
    const int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < S; y++) {
        if (abort_flag && *abort_flag) {
            return false; /* out left partially written -- caller must not publish */
        }
        moon_bake_rows(&g, alb, grel, s_gray, S, radius * k, (float)cx * k, (float)cy * k, MOON_ALB_W, MOON_ALB_H, y,
                       y + 1, /*dark_zero=*/1, /*dk_sup=*/0.0f, bev);
        if ((y & 15) == 15) {
            vTaskDelay(1); /* feed the idle task + TWDT (the clock sweep's cadence) */
        }
    }
    if (abort_flag && *abort_flag) {
        return false;
    }
    if (pass_event) {
        /* Bench acceptance line (illum must match the day's published
         * phase): every CURRENT-phase frame -- static, sweep final, day
         * tick -- logs its bake. */
        ESP_LOGI(TAG, "bake %dx%d r=%.0f in %lld ms (illum %.1f%%, ss=(%.2f,%.2f), dist %.0f km)", size, size, radius,
                 (long long)((esp_timer_get_time() - t0) / 1000), g.illum * 100.0, (double)g.subsolar_lon,
                 (double)g.subsolar_lat, (double)g.distance_km);
    }

    /* gray -> RGB565 through the armed LUT (what the identity rotate's
     * gather reduces to); reduced frames map display pixel centers to
     * src = (2i+1)*S/(2*size), exact int math (the clock's upcase). */
    if (S == size) {
        for (int y = 0; y < size; y++) {
            const uint8_t *frow = &s_gray[(size_t)y * size];
            uint16_t *rrow = &out[(size_t)y * size];
            for (int x = 0; x < size; x++) {
                rrow[x] = moon_gray565(frow[x]);
            }
        }
    } else {
        int sm[size]; /* VLA: 360 ints ~1.4 KB on the producer's 12 KB stack */
        for (int i = 0; i < size; i++) {
            int s_ = ((2 * i + 1) * S) / (2 * size);
            if (s_ >= S) {
                s_ = S - 1;
            }
            sm[i] = s_;
        }
        for (int Y = 0; Y < size; Y++) {
            const uint8_t *frow = &s_gray[(size_t)sm[Y] * S];
            uint16_t *rrow = &out[(size_t)Y * size];
            for (int x = 0; x < size; x++) {
                rrow[x] = moon_gray565(frow[sm[x]]);
            }
        }
    }
    if (bake_ms_out) {
        *bake_ms_out = (esp_timer_get_time() - t0) / 1000;
    }
    return true;
}

/* The last NEW MOON at or before utc_epoch -- the clock's new_moon_epoch
 * (components/ui/moon_ui.c) verbatim: coarse 6 h grid backwards over at
 * most one synodic month brackets the illumination minimum, the fine
 * 20 min pass refines only the coarse minimum and its two neighbors
 * (a full 20-min grid over 29.53 d would be ~2200 ephemeris calls). The
 * 1e-6 slack keeps a flat/tied coarse step from drifting the fine pass
 * to a neighboring cycle's minimum. Returns < utc_epoch by construction;
 * the caller clamps the span. Yields every 32 calls and honors the abort
 * flag inside both passes (the clock joins its scan from the sleep
 * transition; here a touch abort must die quickly too). */
int64_t knob_moon_new_moon_epoch(int64_t utc_epoch, const volatile bool *abort_flag) {
    moon_geom_t g;
    int64_t best = utc_epoch - (int64_t)(29.53 * 86400);
    float best_illum = 1.0f;
    int n = 0;
    for (int64_t e = utc_epoch - (int64_t)(6 * 3600); e >= utc_epoch - (int64_t)(29.53 * 86400); e -= 6 * 3600) {
        if (abort_flag && *abort_flag) {
            return best; /* partial best: the sweep is dying anyway */
        }
        if (++n >= 32) {
            n = 0;
            vTaskDelay(1);
        }
        /* moon_geometry() is void and no-ops on the (never-seen) ephemeris
         * failure: preset illum so a failed call can never win. */
        g.illum = 1.0f;
        moon_geometry(e, &g);
        if (g.illum < best_illum - 1e-6f) {
            best_illum = g.illum;
            best = e;
        }
    }
    int64_t fine_best = best;
    float fine_illum = best_illum;
    n = 0;
    for (int64_t e = best + 6 * 3600; e >= best - 6 * 3600; e -= 20 * 60) {
        if (abort_flag && *abort_flag) {
            return fine_best;
        }
        if (++n >= 32) {
            n = 0;
            vTaskDelay(1);
        }
        g.illum = 1.0f;
        moon_geometry(e, &g);
        if (g.illum < fine_illum - 1e-6f) {
            fine_illum = g.illum;
            fine_best = e;
        }
    }
    return fine_best;
}
