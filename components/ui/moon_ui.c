/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk in the FIXED
 *     Earth viewport, only when the phase changes (~daily) or when the
 *     tilt moves the orbit (sphere model, below);
 *   - the burn-in spin: the baked frame is then slowly rotated in place
 *     on the panel (moon_rotate, a nearest-neighbor gather of ~6 ms per
 *     frame -- no re-bake), one full turn every UI_MOON_SPIN_PERIOD_S.
 *     The lit pixels move around the panel over time, which is the
 *     AMOLED burn-in protection. The spin render runs on core 1, the
 *     display (LVGL) on core 0; they are serialized on the frame buffer
 *     (a mutex + a rendered/shown handshake) so the display never tears
 *     and the angular speed is constant (time-based angle) no matter
 *     how many frames the display manages.
 *
 * The spin replaces the previous boot animation (a month-long phase
 * sweep): that was display-bound and showed a visible top-to-bottom
 * redraw (the top half of the new frame over the bottom half of the old
 * one). A rotation needs no sweep line at all -- the whole frame is one
 * cheap gather displayed whole, so the redraw matches the compute by
 * construction.
 *
 * Tilt input: the on-board QMI8658 (components/imu) is the ONLY input
 * to the moon (touch is the core-UI wake input, never a moon input).
 * The moon keeps the FIXED Earth viewport (the sub-Earth point is up,
 * posangle applied); the tilt changes only which side of the sphere the
 * camera sees (the in-plane rotation is the time-driven burn-in spin
 * above, not an input response):
 *
 *   - out-of-plane orbit (the sphere model): the angle between the
 *     screen normal and vertical, asin(-gz) in screen coordinates --
 *     0 deg when the device is vertical (wall mount: the DEFAULT
 *     viewport), 90 deg flat on a table. It rotates the camera around
 *     the moon, so the dark side can be peeked at (on a near-full moon
 *     that reveals the unilluminated limb). The chip's z axis is the
 *     out-of-screen axis (calibrated (0, 0, -1) at rest), so the raw
 *     reading is used as-is. The orbit is low-passed
 *     (UI_MOON_ORBIT_LPF_ALPHA), dead-zoned (UI_MOON_ORBIT_DEADZONE_DEG,
 *     above the measured at-rest jitter floor), clamped to +/-
 *     UI_MOON_ORBIT_MAX_DEG (default 30) and quantized to
 *     UI_MOON_ORBIT_STEP_DEG (default 1); only the quantized target
 *     triggers a re-bake.
 *
 * With the IMU absent or UI_IMU_ENABLE off the moon is static. The IMU
 * angle sign is a Kconfig knob (UI_IMU_ANGLE_SIGN) because the board's
 * IMU mounting orientation is not known at code level; it applies to
 * the orbit and to the tilt activity/wake threshold.
 *
 * The orbit re-bake: changing the camera basis cannot be gathered in
 * place, so the current phase is baked again with the basis pre-rotated
 * (moon_orbit_geometry rotates the (right, up, E) basis; the core
 * bake is unchanged and stays gate-safe). A dedicated task bakes the
 * whole frame off screen (~1.7-2.4 s with the relief DEM, row by row
 * so a cancel is honored quickly); when it is done, the tick sweeps
 * the finished frame into the display with the SAME 2-strip batches
 * as the burn-in spin, so the viewport change lands as one ~400 ms
 * frame -- the same sequential update as the spin -- instead of a
 * progressive top-to-bottom wipe over the bake. The in-plane spin is
 * held for the bake+sweep so no rotated frame crosses the sweep.
 * Latest-wins: a target that moves mid-bake starts a fresh bake right
 * after.
 *
 * Tilt is also the activity/wake input: a 1 Hz monitor measures the
 * angle between successive raw gravity samples and reports sustained
 * motion (above UI_IMU_ACTIVITY_DEG, ~deg/s) to the idle system, which
 * holds the idle timer while the user manipulates the ball and wakes
 * from sleep (touch was the only wake input before -- a tilt-only
 * session stranded the device asleep until the RTC alarm fired).
 *
 * All buffers are octal PSRAM. The bake is the expensive pass (one
 * equirect texture sample + Lambert per pixel) and runs off the UI tick
 * only when needed; the rotation is a few int ops per pixel
 * (nearest-neighbor gather, 1 PSRAM read/pixel, x4-unrolled).
 *
 * The spin pipeline (see the spin section below): the render task
 * (core 1) waits until the display has drained the previous frame out of
 * s_rgb, computes the angle from wall clock time, runs moon_rotate over
 * the whole frame (under the buffer mutex) and hands the frame to the
 * display; the LVGL tick (core 0) invalidates the whole moon image,
 * drains the redraw synchronously (lv_refr_now) and hands the buffer
 * back. The display paces the spin; the time-based angle keeps the
 * angular speed constant and a dropped frame becomes a bigger step,
 * never a repeat or a stall. While the orbit owns the buffer (its
 * re-bake + sweep, ~3 s) the render blocks, and the tick re-anchors the
 * angle at the end of the sweep so the first post-orbit frame is the
 * unrotated one the sweep just showed.
 *
 * Display-pipeline constraint (measured on this board): the LVGL draw
 * buffer is PSRAM, and octal PSRAM is NOT DMA-capable for SPI2_HOST
 * (esp_ptr_dma_capable() is false below SOC_DMA_LOW=0x3FC88000). So
 * esp_driver_spi's setup_priv_desc stages every flush strip into an
 * internal-RAM DMA copy, and an S3 SPI DMA transaction is capped at
 * 32 KiB (SPI_LL_DMA_MAX_BIT_LEN). The vendored BSP therefore uses
 * buffer_height=25 rows (466*25*2 = 23.3 KiB strips): a 50-row strip's
 * 32 KiB first chunk does not fit the largest DMA-capable internal block
 * (~31 KiB) and every full-panel flush fails with ESP_ERR_NO_MEM.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "clock.h"
#include "clock_idle.h"
#include "imu.h"
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"
#include "moon_ui.h"

static const char *TAG = "moon_ui";

/* The moon image is the 400x400 disk (radius MOON_DISK_RADIUS_PX = 200,
 * the asset pipeline's native size), centered on the 466x466 panel with a
 * 33 px black margin -- 26% less bake/rotate/display traffic than baking
 * the whole panel. The bake/rotate math works in image coordinates
 * (center 200,200); the touch math works in panel coordinates
 * (center 233,233 -- the same point, so the drag math is unchanged). */
#define MOON_PX 400
#define MOON_IMG_POS 33 /* (466 - MOON_PX) / 2 */
#define MOON_CX 233
#define MOON_CY 233
#define MOON_R (float)MOON_DISK_RADIUS_PX
#define MOON_TICK_MS 50
/* Animation display strip = the BSP draw-buffer height (25 rows): one
 * invalidated strip = exactly one draw buffer + one QSPI transfer. */
#define MOON_STRIP_ROWS 25
#define MOON_NSTRIPS (MOON_PX / MOON_STRIP_ROWS)
#if (MOON_PX % MOON_STRIP_ROWS) != 0
#error "MOON_PX must be a multiple of MOON_STRIP_ROWS"
#endif

/* Burn-in spin: the current phase is baked once (moon_bake_epoch), then
 * rotated in place on the panel -- one full turn every
 * CONFIG_UI_MOON_SPIN_PERIOD_S (see the spin section below). No
 * animation buffers or dsc: the spin renders straight into the shared
 * frame buffer the static moon uses, at the same MOON_PX resolution. */
#ifndef CONFIG_UI_MOON_SPIN_ENABLE
#define CONFIG_UI_MOON_SPIN_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_SPIN_PERIOD_S
#define CONFIG_UI_MOON_SPIN_PERIOD_S 180
#endif
#ifndef CONFIG_UI_IMU_ENABLE
#define CONFIG_UI_IMU_ENABLE 1
#endif
#ifndef CONFIG_UI_IMU_ANGLE_SIGN
#define CONFIG_UI_IMU_ANGLE_SIGN 1
#endif
#ifndef CONFIG_UI_MOON_ORBIT_ENABLE
#define CONFIG_UI_MOON_ORBIT_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_ORBIT_MAX_DEG
#define CONFIG_UI_MOON_ORBIT_MAX_DEG 30
#endif
#ifndef CONFIG_UI_MOON_ORBIT_STEP_DEG
#define CONFIG_UI_MOON_ORBIT_STEP_DEG 1
#endif
#ifndef CONFIG_UI_MOON_ORBIT_DEADZONE_DEG
#define CONFIG_UI_MOON_ORBIT_DEADZONE_DEG 2
#endif
#ifndef CONFIG_UI_MOON_ORBIT_LPF_ALPHA
#define CONFIG_UI_MOON_ORBIT_LPF_ALPHA 30
#endif
static uint8_t *s_frame;     /* 8-bit baked frame, MOON_PX^2 */
static uint16_t *s_rgb;      /* RGB565 display buffer, MOON_PX^2 */
static lv_image_dsc_t s_dsc; /* (MOON_PX), points at s_rgb */
static lv_obj_t *s_images[4];
static int s_n_images;
static lv_timer_t *s_tick;
static bool s_ready;
static bool s_started; /* moon_ui_start() called; ticks may run */

static int64_t s_baked_day;   /* calendar day of the last static bake */
static int64_t s_orbit_epoch; /* utc the last static bake was for (0 = none) */

/* Out-of-plane orbit (sphere model) state. The quantized target is what
 * drives re-bakes; the smoothed angle is what the in-plane hold tracks.
 * The task publishes (epoch_ms, rows_done) like the anim pipeline. */
static volatile bool s_orbit_baking;    /* bake task running */
static volatile bool s_orbit_cancel;    /* stop the running bake */
static volatile bool s_orbit_sweeping;  /* the tick is showing the baked frame's strips */
static volatile int32_t s_orbit_target; /* quantized orbit angle, deg (signed) */
static volatile int32_t s_orbit_baked;  /* orbit angle the on-screen frame was last baked at */
static volatile uint32_t s_orbit_pub;   /* rows fully converted (0..MOON_PX) */
static volatile int s_orbit_strip_shown;
static TaskHandle_t s_orbit_task;
static float s_orbit_lpf; /* low-passed orbit angle, rad */
static bool s_orbit_have; /* s_orbit_lpf holds a value */

/* Burn-in spin pipeline (see the spin section below). Render task
 * (core 1): waits for the display to drain the previous frame
 * (s_spin_shown), runs moon_rotate over the WHOLE frame under the
 * buffer mutex (a ~6 ms nearest-neighbor gather) and hands the frame to
 * the display (s_spin_ready). Display (the LVGL tick, core 0): shows
 * the frame in MOON_SPIN_BATCH-row batches -- a few hundred ms of
 * lv_refr_now per tick, short enough to keep core 0's idle task fed for
 * the Task WDT -- and gives s_spin_shown once the last batch is out of
 * s_rgb. The render paces to the display; the time-based angle (wall
 * clock vs s_spin_anchor_ms) keeps the angular speed constant no matter
 * how many frames the display manages -- a dropped frame is a bigger
 * step, never a repeat or a stall. The mutex guards every reader/writer
 * of s_rgb (the render, the tick's batched display, the orbit's row
 * sweep, the static LUT convert) so the display never tears. */
/* Strips (25 rows) displayed per tick. TUNABLE (task #24): the measured
 * lv_refr_now cost is ~190-410 ms per 25-row strip, so a batch must
 * stay short enough that each tick's drain is a few hundred ms (core 0
 * idle gap for the Task WDT + touch responsiveness) -- 2 strips at the
 * measured rate. If the display proves faster, raise this. */
#define MOON_SPIN_BATCH 2
static volatile bool s_spin_running;      /* render task alive */
static volatile bool s_spin_cancel;       /* stop the render (sleep transition) */
static volatile int64_t s_spin_anchor_ms; /* wall ms (esp_timer) the angle was 0 */
static volatile bool s_spin_ready;        /* a rendered frame is in s_rgb */
static volatile int s_spin_strip;         /* display: next strip of the frame */
static volatile int s_spin_deg;           /* last displayed angle, deg (perf log) */
static TaskHandle_t s_spin_task;
static SemaphoreHandle_t s_spin_shown; /* the display has drained s_rgb */
static SemaphoreHandle_t s_rgb_mtx;    /* guards s_rgb (+ s_frame while baking) */

/* Spin perf instrumentation (temporary, task #24): the render task
 * (core 1) fills s_perf_render_us (mutex + gather) per frame; the tick
 * (core 0) accumulates s_perf_refr_us over the frame's batches and logs
 * one line per displayed frame. Self-measured (work/wall), so the
 * numbers are the moon pipeline's own cost on each core, not the
 * system's. */
static volatile uint32_t s_perf_render_us;
static volatile uint32_t s_perf_refr_us;
static volatile uint32_t s_perf_orbit_us; /* orbit sweep display cost */

static void moon_invalidate_all(void) {
    for (int i = 0; i < s_n_images; i++)
        lv_obj_invalidate(s_images[i]);
}

/* Static (angle-0) display of the baked frame: the 8-bit bake maps
 * straight to the RGB565 buffer through the LUT -- no gather. Used for
 * the first display and after a re-bake (day rollover / wake); the spin
 * re-anchors at angle 0 right after, so the first spin frame matches
 * what the LUT just showed. Under the buffer mutex like every other
 * s_rgb writer, so it cannot race the spin render's gather. */
static void moon_convert_to_rgb(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
    /* Re-anchor inside the critical section: the spin render computes
     * its angle under this mutex, so the next frame is the identity
     * gather of the fresh frame -- exactly what the LUT just displayed
     * (no orientation jump after a re-bake). */
    s_spin_anchor_ms = esp_timer_get_time();
    xSemaphoreGive(s_rgb_mtx);
    moon_invalidate_all();
}

/* Sphere-model camera: rotate the camera basis about the right
 * (screen-horizontal) axis by the orbit angle -- "move the camera around
 * the sphere" (the moon is fixed in the body frame, so equivalently
 * rotate the moon sphere the way the device tilts it). E is the body
 * direction the camera looks at, so the orbit mixes (up, E) and leaves
 * right unchanged; the light source S stays put (body frame), so the lit
 * side of the moon does not move -- only which side of the disk the
 * camera sees, which is what reveals the dark limb.
 *
 * Sign convention (verified against the device frame, screen coords x
 * right / y down / z toward the viewer, gravity calibrated (0,0,-1) at
 * rest): orbit angle = asin(-gz) = 0 deg vertical (wall, default
 * viewport), positive when the top of the device tilts away from the
 * viewer (toward the table side) -- then the BOTTOM limb of the moon
 * moves toward the viewer and the visible pole moves to -up, i.e.
 * E_new = cos(o) E - sin(o) up (and up_new = cos(o) up + sin(o) E).
 * The moon keeps the fixed Earth viewport (no in-plane rotation), so
 * this basis rotation is the only tilt-driven view change.
 * UI_IMU_ANGLE_SIGN is applied to the quantized target, so this takes
 * the target in radians as-is. */
static void moon_orbit_geometry(const moon_geom_t *src, float orbit_rad, moon_geom_t *dst) {
    *dst = *src;
    float c = cosf(orbit_rad), s = sinf(orbit_rad);
    float e[3], u[3];
    e[0] = c * src->E[0] - s * src->up[0];
    e[1] = c * src->E[1] - s * src->up[1];
    e[2] = c * src->E[2] - s * src->up[2];
    u[0] = c * src->up[0] + s * src->E[0];
    u[1] = c * src->up[1] + s * src->E[1];
    u[2] = c * src->up[2] + s * src->E[2];
    dst->E[0] = e[0];
    dst->E[1] = e[1];
    dst->E[2] = e[2];
    dst->up[0] = u[0];
    dst->up[1] = u[1];
    dst->up[2] = u[2];
    /* right unchanged (orbit axis). */
}

static void moon_bake_epoch(int64_t epoch) {
    moon_geom_t g;
    int64_t t0 = esp_timer_get_time();
    moon_geometry(epoch, &g);
    int64_t t1 = esp_timer_get_time();
    s_orbit_epoch = epoch; /* orbit re-bakes reuse the same phase */
    s_orbit_baked = 0;     /* a static bake shows the default viewport */
    const uint8_t *alb = moon_albedo();
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; moon stays black");
        return;
    }
    const int8_t *dem = moon_dem();
    if (!dem)
        ESP_LOGW(TAG, "no relief DEM; baking relief-free");
    /* Under the buffer mutex: this re-bake replaces the WHOLE frame
     * (s_frame + the s_rgb display) while the spin render may be
     * mid-gather on the old one. The render blocks on the mutex for the
     * bake duration -- the spin pauses -- and draws the new frame at
     * the current time-based angle once it resumes (the caller
     * re-anchors). */
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    moon_bake(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H);
    xSemaphoreGive(s_rgb_mtx);
    int64_t t2 = esp_timer_get_time();
    /* (The relief-free vs with-DEM split that ran here while diagnosing
     * the ~2.3 s bake settled the question: the 4-neighbor DEM gather
     * is the dominant cost, ~1.2 s of the ~1.9 s frame at 2048x1024.) */
    ESP_LOGI(TAG, "bake | phase %.1f%% illum, subsolar (%.1f, %.1f) | geo %lldms bake %lldms", g.illum * 100.0f,
             g.subsolar_lon, g.subsolar_lat, (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000));
    moon_convert_to_rgb();
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | rgb565 %lldms (total %lldms)", (long long)((t3 - t2) / 1000), (long long)((t3 - t0) / 1000));
}

/* ---------------- burn-in spin ----------------
 *
 * The current phase is baked ONCE (moon_bake_epoch, ~2 s) and then the
 * frame is slowly rotated in place on the panel -- one full turn every
 * CONFIG_UI_MOON_SPIN_PERIOD_S -- so the lit pixels move around the
 * panel and the AMOLED cannot burn in. The rotation is a cheap
 * nearest-neighbor gather (moon_rotate, ~6 ms over the whole frame, one
 * PSRAM read/pixel): it is display-bound, not compute-bound, which is
 * what makes it usable. The old month-long sweep re-baked every frame
 * and showed a visible top-to-bottom redraw (the new frame's top half
 * over the old frame's bottom half); a rotation needs no sweep line at
 * all -- the whole frame is one cheap gather displayed whole, so the
 * redraw matches the compute by construction.
 *
 *   render (dedicated task, pinned to core 1 -- the LVGL task is pinned
 *   to core 0, see the BSP):
 *     wait for the display to drain the previous frame (s_spin_shown;
 *     a finite poll so a cancel is honored while blocked)
 *     compute the angle from wall clock time (time-based: the angular
 *     speed is constant even when frames drop -- a slow display is a
 *     bigger step, never a repeat or a stall)
 *     moon_rotate(s_frame -> s_rgb) under the buffer mutex, then hand
 *     the frame to the display (s_spin_ready)
 *
 *   display (the LVGL tick, core 0, every MOON_TICK_MS):
 *     invalidate MOON_SPIN_BATCH strips (25 rows each) of the pending
 *     frame, top to bottom, and drain the redraw synchronously
 *     (lv_refr_now, under the mutex) -- a full 400-row frame in one
 *     tick would be ~4 s of continuous core-0 work (25-row QSPI
 *     flushes at ~250 ms each), long enough to starve core 0's idle
 *     task into the Task WDT; batched, each tick is a few hundred ms
 *     with an idle gap between batches.
 *     give s_spin_shown once all MOON_NSTRIPS strips are out of s_rgb
 *     (the QSPI flush then runs asynchronously from LVGL's draw
 *     buffers, never s_rgb).
 *
 * While the orbit owns the buffer (its re-bake + row sweep) the render
 * is paused (not stopped): it blocks on the orbit flags, and the tick
 * re-anchors the angle at the end of the sweep -- the sweep shows the
 * unrotated frame of the new viewport, which IS the angle-0 frame, so
 * the spin picks up seamlessly at the time-based angle.
 */
static void moon_spin_task(void *arg) {
    (void)arg;
    for (;;) {
        /* Paused (not stopped) while the orbit owns the buffer: its
         * bake rewrites s_frame (row by row, off screen) and the tick
         * then sweeps s_rgb in the spin's 2-strip batches. The tick
         * re-anchors the angle at the end of the sweep. */
        while (!s_spin_cancel && (s_orbit_baking || s_orbit_sweeping))
            vTaskDelay(pdMS_TO_TICKS(50));
        if (s_spin_cancel)
            break;
        /* Handshake: the display has drained the previous frame out of
         * s_rgb (or this is the first frame -- s_spin_shown is primed
         * at start). Finite poll so a sleep-transition cancel is
         * honored while blocked; a portMAX_DELAY wait would sleep
         * through it. */
        if (xSemaphoreTake(s_spin_shown, pdMS_TO_TICKS(100)) != pdPASS)
            continue;
        if (s_spin_cancel)
            break;
        /* The angle is computed UNDER the buffer mutex, after the
         * frame's source is fixed: a re-bake (moon_bake_epoch) and the
         * orbit sweep re-write s_frame and re-anchor the angle inside
         * their own critical sections, so the render can never mix a
         * new frame with a stale pre-re-bake angle (an orientation
         * jump). The cost is a few FPU ops per frame. Time-based angle
         * (see the section header): wall clock vs the anchor, wrapped
         * to [0, 2pi) -- the displayed frame is always "where the
         * angle is NOW", so a slow display costs smoothness (bigger
         * steps), never direction or speed. int64 ms -> double is
         * exact (2^53 ms ~ 285,000 years), so no precision loss at
         * any device uptime. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time(); /* at render time, post-wait */
        double sec = (double)(t0 - s_spin_anchor_ms) / 1e6;
        float rad = (float)(fmod(sec, (double)CONFIG_UI_MOON_SPIN_PERIOD_S) * 6.283185307179586 /
                            (double)CONFIG_UI_MOON_SPIN_PERIOD_S);
        moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2,
                    moon_cos15(rad), moon_sin15(rad));
        s_spin_ready = true; /* the display shows it in batches */
        xSemaphoreGive(s_rgb_mtx);
        s_perf_render_us = (uint32_t)(esp_timer_get_time() - t0);
        s_spin_deg = (int)(rad * 57.29577951308232f + 0.5f);
        /* Pace: the display is the bottleneck (a batch takes longer than
         * the gather); a short sleep between frames keeps the render from
         * busy-spinning on the mutex while the display drains. The
         * time-based angle keeps the angular speed exact either way. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_spin_running = false;
    s_spin_task = NULL;
    vTaskDelete(NULL); /* a FreeRTOS task must never return */
}

/* Anchor the spin: angle 0 at this instant, under the buffer mutex so
 * the render's next angle read (which is also under the mutex) is
 * guaranteed to see the new anchor. The orbit sweep's final frame calls
 * this before it clears the sweep flag the render is polling on, so the
 * render cannot read the old anchor. */
static void moon_spin_reanchor(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    s_spin_anchor_ms = esp_timer_get_time();
    xSemaphoreGive(s_rgb_mtx);
}

/* Start the render task (a previous one, if any, is already joined by
 * the caller -- moon_spin_stop). No re-anchor here on purpose: after a
 * sleep the angle is computed from wall clock time against the existing
 * anchor, so the first frame is exactly where the spin would have been
 * had it kept running -- no orientation snap on wake. */
static void moon_spin_begin(void) {
    if (!CONFIG_UI_MOON_SPIN_ENABLE || s_spin_running)
        return;
    s_spin_cancel = false;
    s_spin_ready = false;
    s_spin_strip = 0;
    s_perf_refr_us = 0;
    /* Prime the handshake: the first frame may be rendered immediately
     * (the on-screen frame is the angle-0 LUT display). */
    (void)xSemaphoreTake(s_spin_shown, 0); /* consume a stale signal */
    (void)xSemaphoreGive(s_spin_shown);
    /* PSRAM task stack (8 KB): internal RAM is tighter than it looks
     * (the DMA staging copy needs a ~23 KB contiguous internal block).
     * The spin gather is a few int ops per pixel -- the default
     * internal-RAM stack would work too, but the PSRAM stack keeps the
     * internal heap free for the display path. */
    if (xTaskCreatePinnedToCoreWithCaps(moon_spin_task, "moon_spin", 8192, NULL, configMAX_PRIORITIES - 3, &s_spin_task,
                                        1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "spin task allocation failed; moon stays static");
        return;
    }
    s_spin_running = true;
    ESP_LOGI(TAG, "burn-in spin started (%d s per full turn)", CONFIG_UI_MOON_SPIN_PERIOD_S);
}

/* Stop the render task (sleep transition): the screen goes off anyway,
 * so the moon can freeze at its current orientation. */
static void moon_spin_stop(void) {
    if (!s_spin_running)
        return;
    s_spin_cancel = true;
    int waited = 0;
    /* The render exits at its next poll: <= 100 ms shown-poll or 50 ms
     * orbit-pause plus at most one ~6 ms gather -- 500 ms is plenty. */
    while (s_spin_running && waited < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if (s_spin_running)
        ESP_LOGW(TAG, "spin task did not exit within 500 ms");
}

/* Forward decl: defined in the orbit section below (the orbit pipeline
 * must be cancelled before a re-bake takes over s_frame/s_rgb). */
static void moon_orbit_interrupt(void);

/* ---------------- out-of-plane orbit (sphere model) ---------------- */

/* Bake the current phase at the given orbit angle -- the WHOLE frame,
 * off screen (row by row, per-row critical sections so a cancel is
 * honored quickly). When it is done the tick sweeps the finished frame
 * into the display with the same 2-strip batches as the burn-in spin,
 * so the viewport change lands as one ~400 ms frame -- the same
 * sequential update as the spin -- instead of a progressive
 * top-to-bottom wipe over the ~2.4 s bake. The task owns s_frame and
 * s_rgb's row content, but never the images' validity. */
static void moon_orbit_task(void *arg) {
    (void)arg;
    int64_t epoch = s_orbit_epoch;
    const uint8_t *alb = moon_albedo();
    const int8_t *dem = moon_dem();
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; orbit bake aborted");
        s_orbit_baking = false;
        vTaskDelete(NULL);
    }
    if (!dem)
        ESP_LOGW(TAG, "no relief DEM; orbit bake relief-free");
    int32_t target_deg = s_orbit_target;
    /* The target is signed (UI_IMU_ANGLE_SIGN applied at quantization),
     * so the basis rotation takes it as-is. */
    float orbit = (float)target_deg * (float)M_PI / 180.0f;
    moon_geom_t g;
    moon_geometry(epoch, &g);
    moon_orbit_geometry(&g, orbit, &g);
    int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < MOON_PX && !s_orbit_cancel; y++) {
        /* One lock per row: this replaces the WHOLE frame (s_frame + the
         * s_rgb sweep), so the spin render -- which blocks on the mutex
         * for the bake duration -- sees each new row only after it is
         * complete, and can never tear the sweep. The short per-row
         * critical sections also keep the render task from waiting more
         * than a row (~6 ms) at a time, so a cancel is honored quickly. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        moon_bake_rows(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
                       MOON_ALB_H, y, y + 1);
        /* Fixed viewport: the row maps straight to RGB565 (no rotation). */
        const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
        uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
        for (int x = 0; x < MOON_PX; x++)
            rrow[x] = moon_gray565(frow[x]);
        xSemaphoreGive(s_rgb_mtx);
        if ((y & 15) == 15)
            vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
    }
    int64_t t1 = esp_timer_get_time();
    if (!s_orbit_cancel)
        s_orbit_pub = MOON_PX; /* the tick sweeps the finished frame in */
    s_orbit_baking = false;
    if (!s_orbit_cancel)
        s_orbit_baked = target_deg; /* the on-screen frame IS this viewport */
    ESP_LOGI(TAG, "orbit %+d deg %s in %lldms (sweep follows)", target_deg, s_orbit_cancel ? "interrupted" : "baked",
             (long long)((t1 - t0) / 1000));
    vTaskDelete(NULL);
}

/* Kick off the orbit bake for s_orbit_target (latest-wins: the task
 * reads s_orbit_target at its start, so a target that moved mid-bake
 * starts a fresh bake right after the running one finishes). */
static void moon_orbit_start(void) {
    int64_t epoch = s_orbit_epoch;
    if (epoch <= 0)
        return; /* nothing baked yet (anim running or no time) */
    if (xTaskCreatePinnedToCore(moon_orbit_task, "moon_orbit", 8192, NULL, configMAX_PRIORITIES - 3, &s_orbit_task,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "orbit task allocation failed; viewport stays at the last bake");
        return;
    }
    s_orbit_baking = true;
    s_orbit_sweeping = true;
    s_orbit_strip_shown = 0;
    s_orbit_pub = 0;
    /* Hold the idle->sleep timer for the bake+sweep (~3 s) so the device
     * cannot sleep into the middle of a viewport change. */
    if (!clock_is_idle())
        clock_idle_set_enabled(false);
}

/* Start a bake if the quantized target is set and no bake/sweep is in
 * flight. Called from the tick (cheap flag checks); latest-wins falls
 * out of the target being read at task start. The orbit is only
 * meaningful once a static bake exists (s_orbit_epoch > 0, set by the
 * first bake) -- it bakes THE current phase at a peeked viewport, and
 * the spin keeps rotating the swept frame (the tick re-anchors the
 * angle at the end of the sweep). */
static void moon_orbit_drain(void) {
    if (!CONFIG_UI_MOON_ORBIT_ENABLE || !s_started || s_orbit_epoch <= 0 || s_orbit_baking || s_orbit_sweeping)
        return;
    if (s_orbit_target == 0 || s_orbit_target == s_orbit_baked)
        return; /* default viewport, or the on-screen frame is this viewport */
    moon_orbit_start();
}

/* Cancel the running orbit bake/sweep and leave a clean default-viewport
 * state. Called on the sleep transition and when the boot animation
 * (re)starts, both of which take over s_frame/s_rgb. */
static void moon_orbit_interrupt(void) {
    /* Joining the orbit task only matters while the spin render may
     * still be alive (it blocks on the orbit's per-row mutex, so it
     * must be quiescent before a re-bake touches s_rgb): that is once
     * the moon has started. Before moon_ui_start there is no render
     * task, and the boot bake runs on the display core before it is
     * spawned, so a still-running orbit task cannot race it. */
    if (s_started && s_orbit_baking) {
        s_orbit_cancel = true;
        int waited = 0;
        /* The task exits at its next yield (~one row, a few ms). */
        while (s_orbit_baking && waited < 400) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (s_orbit_baking)
            ESP_LOGW(TAG, "orbit task did not exit within 400 ms; forcing state clear");
    }
    s_orbit_cancel = false;
    s_orbit_baking = false;
    s_orbit_sweeping = false;
    s_orbit_strip_shown = 0;
    s_orbit_pub = 0;
    s_orbit_target = 0; /* sleep shows the default viewport */
    if (!clock_is_idle())
        clock_idle_set_enabled(true);
}

/* The tilt is the moon's input: the in-plane roll sets the ball angle,
 * The out-of-plane orbit (sphere model) comes from the gravity vector:
 * angle from vertical asin(-gz) (chip z is the out-of-screen axis,
 * calibrated (0,0,-1) at rest), low-passed, dead-zoned, clamped to
 * +/- UI_MOON_ORBIT_MAX_DEG, quantized to UI_MOON_ORBIT_STEP_DEG --
 * only a moved quantized target triggers a re-bake. */
static void moon_imu_tick(void) {
    if (!CONFIG_UI_IMU_ENABLE || !CONFIG_UI_MOON_ORBIT_ENABLE)
        return;
    float g[3];
    if (!imu_read_gravity(g))
        return;
    /* Orbit angle from the wall (vertical) position (rad): 0 when the
     * screen is vertical (wall-mounted: the DEFAULT viewport), 90 deg
     * flat on a table (clamped to the max). The chip's z axis is the
     * out-of-screen axis, so gravity-z is -cos of that angle. UI_IMU_ANGLE_SIGN
     * flips the direction with the board's mounting orientation.
     *
     * The driver's |g| band (0.5-1.5) does not bound the COMPONENTS:
     * near-flat (g[2] ~ -0.998) a single sample can read |g[2]| just
     * over 1, and asinf(|x| > 1) is NaN, which would poison the EMA
     * below for the rest of the boot. Clamp the argument instead. */
    float gz = -g[2];
    if (gz > 1.0f)
        gz = 1.0f;
    else if (gz < -1.0f)
        gz = -1.0f;
    float raw = CONFIG_UI_IMU_ANGLE_SIGN * asinf(gz);
    /* Low-pass (EMA) at the 20 Hz UI tick: kills sensor stutter. */
    if (s_orbit_have)
        s_orbit_lpf += (float)CONFIG_UI_MOON_ORBIT_LPF_ALPHA / 100.0f * (raw - s_orbit_lpf);
    else {
        s_orbit_lpf = raw;
        s_orbit_have = true;
    }
    int32_t deg = (int32_t)lrintf(s_orbit_lpf * 57.29577951308232f);
    int32_t sgn = (deg < 0) ? -1 : 1;
    int32_t mag = (deg < 0) ? -deg : deg;
    /* Dead zone: inside the band the viewport is the default one (the
     * measured at-rest jitter floor cannot walk it around). */
    if (mag <= (int32_t)CONFIG_UI_MOON_ORBIT_DEADZONE_DEG)
        mag = 0;
    else if (mag > (int32_t)CONFIG_UI_MOON_ORBIT_MAX_DEG)
        mag = CONFIG_UI_MOON_ORBIT_MAX_DEG; /* clamp: "not act above 30 deg" */
    /* Quantize to the re-bake step, then restore the sign: a negative
     * target peeks the opposite limb. */
    int32_t step = CONFIG_UI_MOON_ORBIT_STEP_DEG;
    int32_t target = (mag / step) * step;
    if (target > 0)
        target = sgn * target;
    if (target != s_orbit_target) {
        int32_t old = s_orbit_target;
        s_orbit_target = target;
        moon_orbit_drain(); /* starts a bake if none in flight; a bake in
                             * flight re-reads the target at its start */
        ESP_LOGI(TAG, "orbit viewport %+d -> %+d deg (smoothed %.1f)", old, target, s_orbit_lpf * 57.29577951308232f);
    }
}

/* Tilt activity / wake monitor (1 Hz). Tilt is the moon's input, so
 * sustained device motion must count as user activity: hold the idle
 * timer while the user manipulates the ball, and wake from sleep (touch
 * is the core-UI wake input, but a tilt-only session must not strand
 * the device asleep).
 *
 * Motion test: the angle between successive RAW gravity samples (deg),
 * which is ~deg/s at this 1 Hz rate. The at-rest noise floor measured
 * on this board (device on a table, 1 Hz) has p90 ~3.3 deg/s and a
 * quietest 30 s window spanning 3.9 deg, so the threshold sits above
 * that (UI_IMU_ACTIVITY_DEG, default 5). 1 Hz is plenty: a hand tilt
 * that should wake the device moves far more than 5 deg in a second,
 * and the wake is all-or-nothing so per-second granularity is fine.
 *
 * This callback lives on its own timer (not the moon tick) because the
 * moon tick is PAUSED in sleep -- the wake path must run there too. */
#ifndef CONFIG_UI_IMU_ACTIVITY_DEG
#define CONFIG_UI_IMU_ACTIVITY_DEG 5
#endif
static float s_last_g[3];
static bool s_have_g;

static void moon_tilt_monitor_cb(lv_timer_t *timer) {
    (void)timer;
    float g[3];
    if (imu_read_gravity(g)) {
        if (s_have_g) {
            float c = g[0] * s_last_g[0] + g[1] * s_last_g[1] + g[2] * s_last_g[2];
            if (c < -1.0f)
                c = -1.0f;
            else if (c > 1.0f)
                c = 1.0f;
            float ang_deg = acosf(c) * 57.29577951308232f;
            if (ang_deg >= (float)CONFIG_UI_IMU_ACTIVITY_DEG)
                clock_idle_notify_tilt_activity();
        }
        s_last_g[0] = g[0];
        s_last_g[1] = g[1];
        s_last_g[2] = g[2];
        s_have_g = true;
    }

    /* Diagnostic (temporary, remove with the 92faee2 instrumenting):
     * 1 Hz tilt/input-state line. The IMU roll read here is an extra
     * bus transaction that shows whether the chip answers while asleep
     * -- the "stuck in sleep after tilting" hypothesis. The indev state
     * (0=RELEASED 1=PRESSED 2=PRESSING 3=WAIT_RELEASE 4=GESTURE) shows
     * whether the touch-wake path can still fire LV_EVENT_PRESSED. */
    float roll;
    bool ok = imu_read_roll(&roll);
    lv_indev_t *ind = lv_indev_get_next(NULL);
    int st = ind ? (int)lv_indev_get_state(ind) : -1;
    ESP_LOGI(TAG, "DIAG imu %s roll %.1f | indev %d | spin %d %d | orbit t%d b%d sweep%d", ok ? "ok" : "FAIL",
             ok ? roll * 57.29577951308232f : -1000.0f, st, (int)s_spin_running, s_spin_deg, (int)s_orbit_target,
             (int)s_orbit_baked, (int)(s_orbit_baking || s_orbit_sweeping));
}

/* ---------------- tick ---------------- */

static void moon_tick_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ready || !s_started)
        return;
    /* Spin consumer (see the spin section): show the pending rendered
     * frame in MOON_SPIN_BATCH-strip batches, top to bottom, under the
     * buffer mutex, and hand the buffer back once the last batch is
     * out of s_rgb. Batching keeps each tick's lv_refr_now to a few
     * hundred ms of core-0 work (a full 400-row frame at once would be
     * ~4 s of continuous 25-row QSPI flushes -- too long for the Task
     * WDT margin and too coarse for touch responsiveness). */
    if (s_spin_ready) {
        int end = s_spin_strip + MOON_SPIN_BATCH;
        if (end > MOON_NSTRIPS)
            end = MOON_NSTRIPS;
        if (end > s_spin_strip) {
            /* Batch area in the moon image's (screen) coordinates. */
            lv_area_t a = {.x1 = (lv_coord_t)MOON_IMG_POS,
                           .y1 = (lv_coord_t)(MOON_IMG_POS + s_spin_strip * MOON_STRIP_ROWS),
                           .x2 = (lv_coord_t)(MOON_IMG_POS + MOON_PX - 1),
                           .y2 = (lv_coord_t)(MOON_IMG_POS + end * MOON_STRIP_ROWS - 1)};
            for (int i = 0; i < s_n_images; i++)
                lv_obj_invalidate_area(s_images[i], &a);
            /* Drain the redraw now, under the buffer mutex: after it
             * returns the batch's pixels are out of s_rgb (into LVGL's
             * draw buffers; the QSPI transfer then runs asynchronously
             * from the draw buffers). The mutex is what keeps the
             * render and the orbit sweep from reusing s_rgb mid-drain. */
            int64_t r0 = esp_timer_get_time();
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
            s_perf_refr_us += (uint32_t)(esp_timer_get_time() - r0);
            s_spin_strip = end;
        }
        if (s_spin_strip >= MOON_NSTRIPS) {
            s_spin_ready = false;
            s_spin_strip = 0; /* fresh counter for the next frame (was stuck at NSTRIPS, so
                               * every frame after the first displayed nothing) */
            /* Perf (task #24): frame fully on screen -- the render
             * (core 1) and display (core 0) cost of this frame. */
            ESP_LOGI(TAG, "perf spin: deg %d | render %dms | refr %dms (batch %d strips)", s_spin_deg,
                     (int)(s_perf_render_us / 1000), (int)(s_perf_refr_us / 1000), MOON_SPIN_BATCH);
            s_perf_refr_us = 0;
            xSemaphoreGive(s_spin_shown); /* the render may reuse s_rgb */
        }
    }
    /* Orbit sweep consumer: show the strips of the freshly baked frame
     * the task has fully converted, top to bottom, so the new viewport
     * sweeps in instead of popping in. The spin render is paused for
     * the whole orbit (its poll on the orbit flags), so the only
     * competing s_rgb user is the drain below -- serialized on the
     * mutex like the spin batches. */
    if (s_orbit_sweeping) {
        int strips_ready = (int)((uint32_t)s_orbit_pub / MOON_STRIP_ROWS);
        if (strips_ready > MOON_NSTRIPS)
            strips_ready = MOON_NSTRIPS;
        int show = strips_ready - s_orbit_strip_shown;
        if (show > MOON_SPIN_BATCH)
            show = MOON_SPIN_BATCH; /* the same 2-strip batches as the spin */
        if (show > 0) {
            int last = s_orbit_strip_shown + show;
            for (int s = s_orbit_strip_shown; s < last; s++) {
                lv_area_t a = {.x1 = (lv_coord_t)MOON_IMG_POS,
                               .y1 = (lv_coord_t)(MOON_IMG_POS + s * MOON_STRIP_ROWS),
                               .x2 = (lv_coord_t)(MOON_IMG_POS + MOON_PX - 1),
                               .y2 = (lv_coord_t)(MOON_IMG_POS + (s + 1) * MOON_STRIP_ROWS - 1)};
                for (int i = 0; i < s_n_images; i++)
                    lv_obj_invalidate_area(s_images[i], &a);
            }
            s_orbit_strip_shown = last;
            /* Same mutex-guarded drain as the spin batches: pixels out
             * of s_rgb into LVGL's draw buffers before the next strip
             * may be reused. */
            int64_t r0 = esp_timer_get_time();
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
            s_perf_orbit_us += (uint32_t)(esp_timer_get_time() - r0);
        }
        if (s_orbit_strip_shown >= MOON_NSTRIPS) {
            /* Perf (task #24): the sweep's display cost (the bake cost
             * is the task's log line). */
            ESP_LOGI(TAG, "orbit sweep shown in %dms", (int)(s_perf_orbit_us / 1000));
            s_perf_orbit_us = 0;
            /* Re-anchor (under the buffer mutex) BEFORE clearing the
             * sweep flag: the render polls s_orbit_sweeping and cannot
             * compute a post-sweep angle until it is false, and it
             * reads the anchor under this same mutex -- so the re-anchor
             * happens-before the render's next angle read, and the first
             * spin frame after the orbit is the identity gather (the
             * sweep's last frame, unrotated). No orientation jump. */
            moon_spin_reanchor();
            if (s_spin_ready) {
                /* A pre-orbit spin frame was still draining when the
                 * sweep overwrote the screen: drop it and hand the
                 * buffer back to the render. */
                s_spin_ready = false;
                s_spin_strip = 0;
                xSemaphoreGive(s_spin_shown);
            }
            s_orbit_sweeping = false; /* now the render may resume */
            if (!clock_is_idle())
                clock_idle_set_enabled(true);
        }
    }
    moon_imu_tick();    /* tilt drives the orbit target (no-op without an IMU) */
    moon_orbit_drain(); /* latest-wins: start a pending orbit bake */
}

/* ---------------- public API ---------------- */

void moon_ui_init(void) {
    if (s_ready)
        return;

    s_frame = heap_caps_malloc(MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
    s_rgb = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    if (!s_frame || !s_rgb) {
        ESP_LOGE(TAG, "allocation failed (PSRAM); moon disabled");
        return;
    }
    /* Report the RAM budget. The display path (esp_lcd_panel_io_spi ->
     * spi_master setup_priv_desc) stages every PSRAM color strip into an
     * internal-RAM DMA copy (PSRAM is not DMA-capable for SPI2_HOST): one
     * ~23.3 KB copy per 25-row strip (see the file header), up to
     * CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH in flight. The internal free heap
     * must stay above that peak or the flush returns ESP_ERR_NO_MEM. */
    ESP_LOGI(TAG, "heap internal: total %u, used %u, free %u, largest %u",
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "heap psram: free %u, largest %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    memset(s_frame, 0, MOON_PX * MOON_PX);
    memset(s_rgb, 0, (size_t)MOON_PX * MOON_PX * 2);

    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.flags = 0;
    s_dsc.header.w = MOON_PX;
    s_dsc.header.h = MOON_PX;
    s_dsc.header.stride = MOON_PX * 2;
    s_dsc.data_size = (uint32_t)MOON_PX * MOON_PX * 2;
    s_dsc.data = (const uint8_t *)s_rgb;

    s_tick = lv_timer_create(moon_tick_cb, MOON_TICK_MS, NULL);
    lv_timer_pause(s_tick);
    /* Spin sync (see the spin section): a buffer mutex serializing every
     * s_rgb / s_frame reader+writer (the render's gather, the tick's
     * batched display, the orbit's row sweep, the static LUT convert),
     * and a binary rendered/shown semaphore gating the next render on
     * the previous frame being fully displayed (no tearing; the display
     * paces the spin). */
    s_rgb_mtx = xSemaphoreCreateMutex();
    s_spin_shown = xSemaphoreCreateBinary();
    if (!s_rgb_mtx || !s_spin_shown) {
        ESP_LOGE(TAG, "spin sync allocation failed; moon disabled");
        s_ready = false;
        return;
    }
    /* 1 Hz tilt activity/wake monitor (+ temporary diagnostic line); runs
     * in every screen state because the moon tick itself is paused in
     * deep sleep. */
    lv_timer_create(moon_tilt_monitor_cb, 1000, NULL);
    s_ready = true;
    ESP_LOGI(TAG, "moon UI ready (frame=%p rgb=%p)", (void *)s_frame, (void *)s_rgb);
}

lv_obj_t *moon_ui_create_image(lv_obj_t *parent) {
    if (!s_ready)
        return NULL;
    /* Same 400x400 disk for the static frame and every spin frame (the
     * spin rotates in place, so there is no size change to absorb). */
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_pos(img, MOON_IMG_POS, MOON_IMG_POS);
    lv_obj_set_size(img, MOON_PX, MOON_PX);
    lv_image_set_src(img, &s_dsc);
    lv_obj_set_clickable(img, false);
    if (s_n_images < 4)
        s_images[s_n_images++] = img;
    return img;
}

void moon_ui_refresh(void) { moon_invalidate_all(); }

void moon_ui_bake_now(int64_t utc_epoch) {
    if (!s_ready)
        return;
    s_baked_day = utc_epoch / 86400;
    moon_bake_epoch(utc_epoch);
    moon_spin_begin(); /* the spin renders the fresh frame (re-anchored) */
}

void moon_ui_start(void) {
    if (!s_ready || s_started)
        return;
    s_started = true;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK) {
        /* No trustworthy time (RTC not set / no NTP): show nothing rather
         * than a wrong phase. The daily day-tick (re-bake + spin start)
         * picks it up once the clock syncs. */
        ESP_LOGW(TAG, "no UTC time yet; moon stays blank until the clock syncs");
        lv_timer_resume(s_tick);
        return;
    }
    moon_orbit_interrupt(); /* the boot bake owns the buffers; default viewport */
    s_baked_day = now / 86400;
    moon_bake_epoch(now); /* the one ~2 s bake of the session's phase */
    moon_spin_begin();    /* then the slow in-place spin (see the spin section) */
    lv_timer_resume(s_tick);
}

void moon_ui_replay_animation(void) {
    /* Test hook (wake-from-sleep, see CONFIG_UI_MOON_ANIM_ON_WAKE):
     * re-bake the current phase (the spin resumes automatically -- it
     * never stopped, and the bake re-anchors the angle under the buffer
     * mutex, so the first frame after the re-bake is the unrotated one
     * the LUT display just showed). No-op before moon_ui_start (no time
     * yet) and while the orbit pipeline owns the buffers. */
    if (!s_ready || !s_started || s_orbit_baking || s_orbit_sweeping)
        return;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK)
        return;
    ESP_LOGI(TAG, "wake re-bake (test hook)");
    s_baked_day = now / 86400;
    moon_bake_epoch(now);
    moon_spin_begin();
}

void moon_ui_day_tick(void) {
    if (!s_ready)
        return;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK)
        return;
    int64_t day = now / 86400;
    if (day != s_baked_day) {
        if (s_orbit_baking || s_orbit_sweeping) {
            /* The orbit pipeline owns s_frame/s_rgb until its sweep
             * finishes; defer (do NOT advance s_baked_day) so the next
             * tick re-tries and the two bakes never race on the buffers. */
            return;
        }
        ESP_LOGI(TAG, "new day; re-baking moon phase");
        s_baked_day = day;
        moon_bake_epoch(now);
        moon_spin_begin();
    }
}

void moon_ui_pause(bool pause) {
    if (!s_tick)
        return;
    if (pause) {
        moon_spin_stop();       /* the screen goes off; the moon can freeze */
        moon_orbit_interrupt(); /* sleep shows the default viewport */
        lv_timer_pause(s_tick);
    } else {
        lv_timer_resume(s_tick);
        /* Wake from sleep: restart the render task (stopped on the
         * sleep transition). The angle is wall-clock based against the
         * existing anchor, so the first frame is exactly where the spin
         * would have been had it kept running -- no snap on wake.
         * Guarded by s_started: at boot the screen actions run before
         * moon_ui_start, which does the first bake + spin start. */
        if (s_started)
            moon_spin_begin();
    }
}
