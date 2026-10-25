/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk in the FIXED
 *     Earth viewport, only when the phase changes (~daily or during the
 *     boot animation) or when the tilt moves the orbit (sphere model,
 *     below);
 *   - a straight gray -> RGB565 LUT conversion of the baked frame into
 *     the panel buffer (no in-plane rotation).
 *
 * Tilt input: the on-board QMI8658 (components/imu) is the ONLY input
 * to the moon (touch is the core-UI wake input, never a moon input).
 * The moon itself keeps the FIXED Earth viewport (the sub-Earth point
 * is up, posangle applied) and never rotates in place; the tilt
 * changes only which side of the sphere the camera sees:
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
 * frame row by row (~2.3 s with the relief DEM) and sweeps the new
 * frame into the display 25-row strips top-to-bottom at a frozen
 * in-plane angle -- the viewport moves as a sweep, never a pop --
 * while the in-plane spin is held for the ~3 s bake+sweep so no seam
 * crosses the sweep line. Latest-wins: a target that moves mid-bake
 * starts a fresh bake right after.
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
 * The boot animation is the same full-resolution bake as the static frame,
 * run as a row-parallel producer/consumer pipeline so every computed frame
 * is drawn, in order, with no gaps (see the animation section below): a
 * dedicated task on core 1 bakes each frame row by row and converts each
 * row to RGB565 as it goes, while the LVGL tick shows finished 25-row
 * strips top-to-bottom -- each new frame sweeps in over ~1 s instead of
 * popping in, and the next frame is already being baked while the last
 * strips of the current one are still flushing. The sweep holds the
 * idle->sleep timer for its duration (see moon_ui_start), so it completes
 * even when the device would otherwise sleep mid-boot.
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

/* Boot animation: a sweep over the last synodic month, rendered at FULL
 * resolution (MOON_PX) through the same moon_bake_epoch path as the static
 * frame -- no separate animation buffers or dsc, so the sweep shows the
 * same-size, same-quality moon the device settles on, and the final frame
 * at "now" IS the static frame (no size jump, no quality jump). The sweep
 * holds the idle->sleep timer for its duration (see moon_ui_start). */
#ifndef CONFIG_UI_MOON_ANIM_ENABLE
#define CONFIG_UI_MOON_ANIM_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_ANIM_FRAMES
#define CONFIG_UI_MOON_ANIM_FRAMES 13
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
#define SYNODIC_S (29.53 * 86400.0)

static uint8_t *s_frame;     /* 8-bit baked frame, MOON_PX^2 */
static uint16_t *s_rgb;      /* RGB565 display buffer, MOON_PX^2 */
static lv_image_dsc_t s_dsc; /* (MOON_PX), points at s_rgb */
static lv_obj_t *s_images[4];
static int s_n_images;
static lv_timer_t *s_tick;
static bool s_ready;
static bool s_started; /* moon_ui_start() called; ticks may run */

static int64_t s_baked_day;   /* calendar day of the last static bake */
static bool s_anim;           /* boot animation in progress */
static int64_t s_anim_end;    /* epoch the animation ends on (now) */
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

/* Row-sweep pipeline state. Producer = the moon_anim task (pinned to
 * core 1), consumer = the LVGL tick (50 ms). The producer publishes one
 * packed 32-bit value per converted row -- (frame << 16) | rows_done --
 * so the consumer reads both halves in a single atomic load and can
 * never see a half-updated (frame, row) pair. Strips (25 rows) are shown
 * only once fully converted, which also keeps the display from tearing
 * mid-strip. */
static volatile uint32_t s_anim_pub;
static int s_anim_frame_shown;          /* consumer: frame index currently shown */
static volatile int s_anim_strip_shown; /* consumer: next strip to show */
static volatile bool s_anim_running;    /* producer task alive */
static volatile bool s_anim_cancel;     /* stop the producer (sleep transition) */
static TaskHandle_t s_anim_task;        /* NULL once the task has exited */

/* Animation perf instrumentation (temporary, task #22 -- measure before
 * optimizing the ~50% CPU). The producer (core 1) fills these at frame
 * end with its own per-stage timing; the consumer (LVGL tick, core 0)
 * accumulates the lv_refr_now time and, once the frame is fully shown,
 * logs one line with both cores' per-frame cost and a self-measured duty
 * (work/wall) for each -- the moon pipeline's own CPU, not the system's. */
static volatile uint32_t s_perf_geo_us;
static volatile uint32_t s_perf_bake_us;
static volatile uint32_t s_perf_conv_us;
static volatile uint32_t s_perf_batch_max_us; /* slowest 16-row bake block */
static volatile uint32_t s_perf_wall_us;      /* frame: bake start -> last row */
static uint32_t s_perf_refr_us;               /* consumer: sum of lv_refr_now */

static void moon_invalidate_all(void) {
    for (int i = 0; i < s_n_images; i++)
        lv_obj_invalidate(s_images[i]);
}

/* The moon keeps the fixed Earth viewport (no in-plane rotation), so the
 * 8-bit bake maps straight to the RGB565 buffer through the LUT -- no
 * gather. The only tilt-driven change is the orbit re-bake (camera
 * basis), which lands in s_rgb the same way. */
static void moon_convert_to_rgb(void) {
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
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
    /* Diagnostic (temporary): time the relief-free pass alongside the
     * DEM pass to settle whether the 2048x1024 gather (or PSRAM
     * contention) is what makes a bake ~2.3 s. relief-free ~0.5 s vs
     * with-DEM ~2.3 s would be the smoking gun. Runs once per bake, so
     * the cost is one extra full-frame bake (~0.5 s). */
    int64_t td0 = esp_timer_get_time();
    moon_bake(&g, alb, NULL, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H);
    int64_t td1 = esp_timer_get_time();
    moon_bake(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H);
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | phase %.1f%% illum, subsolar (%.1f, %.1f) | geo %lldms relief-free %lldms with-DEM %lldms",
             g.illum * 100.0f, g.subsolar_lon, g.subsolar_lat, (long long)((t1 - t0) / 1000),
             (long long)((td1 - td0) / 1000), (long long)((t2 - td1) / 1000));
    moon_convert_to_rgb();
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | rgb565 %lldms (total %lldms)", (long long)((t3 - t2) / 1000), (long long)((t3 - t0) / 1000));
}

/* ---------------- boot animation (row-sweep pipeline) ----------------
 *
 * A producer/consumer pipeline so the sweep is smooth AND every computed
 * frame is drawn, one after the other, even though one full-resolution
 * frame takes ~1 s to bake:
 *
 *   producer (dedicated task, pinned to core 1 -- the LVGL task stays
 *   responsive on the other core):
 *     for each of CONFIG_UI_MOON_ANIM_FRAMES frames (evenly spaced over
 *     the last synodic month):
 *       compute the geometry for that instant (~50 ms)
 *       for each row: bake the row (moon_bake_rows), convert the row to
 *       RGB565 in s_rgb, publish (frame, rows_done)
 *       wait until the consumer has shown all strips of this frame
 *       (catch-up guard, capped so a paused tick cannot deadlock)
 *
 *   consumer (LVGL tick, every MOON_TICK_MS):
 *     invalidate the strips of the current frame that are fully
 *     converted, top to bottom, then drain the redraw synchronously
 *     (lv_refr_now) so the strips' pixels are out of s_rgb and into
 *     LVGL's draw buffers before the tick returns; the QSPI flush then
 *     runs asynchronously from the draw buffers.
 *
 * Visible result: each new frame appears as a ~1 s top-to-bottom sweep
 * (a strip roughly every 3 ms) instead of a hard pop, and the frame
 * after the one on screen is being baked while its predecessor's last
 * strips are still flushing -- the QSPI flush reads LVGL's draw buffers,
 * never s_rgb, so the two stages cannot race.
 */
static void moon_anim_task(void *arg) {
    (void)arg;
    int frames = CONFIG_UI_MOON_ANIM_FRAMES;
    const uint8_t *alb = moon_albedo();
    const int8_t *dem = moon_dem();
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; aborting boot animation");
        s_anim_pub = ((uint32_t)frames << 16);
        goto done;
    }
    if (!dem)
        ESP_LOGW(TAG, "no relief DEM; animating relief-free");
    /* Perf instrumentation (temporary, task #22): per-frame cost
     * breakdown. The producer (pinned core 1) fills s_perf_* at frame
     * end; the consumer (LVGL tick, core 0) logs one line per frame
     * once it has shown all strips. Self-measured duty (work/wall), so
     * the percentages are what the moon pipeline itself burns on each
     * core, not the whole system. */
    for (int f = 0; f < frames && !s_anim_cancel; f++) {
        /* Evenly spaced over the last synodic month, t_k -> 0 (now) as
         * k -> frames - 1. Full-resolution bake at zero in-plane angle. */
        double t = (double)(frames - 1 - f) / (double)(frames - 1);
        int64_t epoch = (int64_t)(s_anim_end - t * SYNODIC_S);
        moon_geom_t g;
        int64_t t0 = esp_timer_get_time();
        moon_geometry(epoch, &g);
        int64_t t1 = esp_timer_get_time();
        uint32_t bake_us = 0, conv_us = 0, batch_us = 0, batch_max_us = 0;
        for (int y = 0; y < MOON_PX && !s_anim_cancel; y++) {
            int64_t rb0 = esp_timer_get_time();
            moon_bake_rows(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2),
                           MOON_ALB_W, MOON_ALB_H, y, y + 1);
            int64_t rb1 = esp_timer_get_time();
            const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
            uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
            for (int x = 0; x < MOON_PX; x++)
                rrow[x] = moon_gray565(frow[x]);
            int64_t rb2 = esp_timer_get_time();
            bake_us += (uint32_t)(rb1 - rb0);
            conv_us += (uint32_t)(rb2 - rb1);
            batch_us += (uint32_t)(rb2 - rb0);
            s_anim_pub = ((uint32_t)f << 16) | (uint32_t)(y + 1);
            if ((y & 15) == 15) {
                if (batch_us > batch_max_us)
                    batch_max_us = batch_us;
                batch_us = 0;
                /* Yield so the idle task on this core (and the 5 s
                 * TWDT) is fed across the back-to-back bakes. */
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        if (batch_us > batch_max_us)
            batch_max_us = batch_us; /* final partial 16-row block */
        int64_t t2 = esp_timer_get_time();
        s_perf_geo_us = (uint32_t)(t1 - t0);
        s_perf_bake_us = bake_us;
        s_perf_conv_us = conv_us;
        s_perf_batch_max_us = batch_max_us;
        s_perf_wall_us = (uint32_t)(t2 - t1);
        ESP_LOGI(TAG, "anim %d/%d | phase %.1f%% illum | bake %lldms", f + 1, frames, g.illum * 100.0f,
                 (long long)((t2 - t0) / 1000));
        if (s_anim_cancel)
            break;
        /* Catch-up guard: wait until the consumer has shown every strip of
         * this frame (it runs on the 50 ms tick, so this is at most one
         * tick) before the next frame's conversion starts reusing s_rgb.
         * The 200 ms cap keeps a paused tick from deadlocking the pipeline. */
        for (int i = 0; i < 40 && s_anim_strip_shown < MOON_NSTRIPS; i++)
            vTaskDelay(pdMS_TO_TICKS(5));
    }
done:
    s_anim_pub = ((uint32_t)frames << 16); /* frames done, 0 rows */
    s_anim_running = false;
    s_anim_task = NULL;
    vTaskDelete(NULL); /* a FreeRTOS task must never return */
}

/* Stop the producer task (sleep transition / early exit) and show the
 * current phase instead (animation done, or interrupted). */
static void moon_anim_skip_to_now(void) {
    if (s_anim_running) {
        s_anim_cancel = true;
        int waited = 0;
        while (s_anim_running && waited < 400) /* the task exits at its next yield (~40 ms) */
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
    }
    s_anim = false;
    /* The boot animation holds the idle timer (see moon_anim_begin).
     * Release it only when the animation ends on its own; if a sleep
     * transition drove this path we are already idle and must not
     * re-arm it. */
    if (!clock_is_idle())
        clock_idle_set_enabled(true);
    int64_t now;
    if (clock_get_utc_epoch(&now) == ESP_OK) {
        s_baked_day = now / 86400;
        moon_bake_epoch(now);
        ESP_LOGI(TAG, "boot animation done, now static");
    }
}

/* Forward decl: defined in the orbit section below (the sweep pipeline
 * must be cancelled before the animation takes over s_frame/s_rgb). */
static void moon_orbit_interrupt(void);

/* Start the animation pipeline (shared by moon_ui_start and the
 * wake-from-sleep replay hook). Holds the idle->sleep timer for the
 * sweep so it is never cut off mid-animation. */
static void moon_anim_begin(void) {
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK) {
        /* No trustworthy time (RTC not set / no NTP): show nothing rather
         * than a wrong phase. The daily day-tick will pick it up later. */
        ESP_LOGW(TAG, "no UTC time yet; moon stays blank until the clock syncs");
        return;
    }
    moon_orbit_interrupt(); /* the sweep owns the buffers; default viewport */
    s_anim_end = now;
    s_baked_day = now / 86400;
    s_anim = true;
    s_anim_cancel = false;
    s_anim_pub = 0;
    s_anim_frame_shown = 0;
    s_anim_strip_shown = 0;
    s_anim_running = true;
    lv_timer_resume(s_tick);
    clock_idle_set_enabled(false);
    if (xTaskCreatePinnedToCore(moon_anim_task, "moon_anim", 8192, NULL, configMAX_PRIORITIES - 3, &s_anim_task, 1) !=
        pdPASS) {
        /* Out of internal RAM for the task stack: fall back to baking the
         * current phase directly (one ~1 s full-frame bake on the tick). */
        ESP_LOGE(TAG, "anim task allocation failed; showing the current phase directly");
        s_anim = false;
        s_anim_running = false;
        clock_idle_set_enabled(true);
        moon_bake_epoch(now);
        return;
    }
    ESP_LOGI(TAG, "boot animation: %d frames over the last synodic month (row-sweep pipeline)",
             CONFIG_UI_MOON_ANIM_FRAMES);
}

/* ---------------- out-of-plane orbit (sphere model) ---------------- */

/* Bake the current phase at the given orbit angle, row by row, sweeping
 * each converted row into the display (s_rgb). The sweep is what makes
 * the viewport change a top-to-bottom motion instead of a pop. The task
 * owns s_frame and s_rgb's row content, but never the images' validity
 * -- the tick shows strips like the boot animation. */
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
        moon_bake_rows(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
                       MOON_ALB_H, y, y + 1);
        /* Fixed viewport: the row maps straight to RGB565 (no rotation). */
        const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
        uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
        for (int x = 0; x < MOON_PX; x++)
            rrow[x] = moon_gray565(frow[x]);
        /* rows_done (one atomic 32-bit load for the consumer). */
        s_orbit_pub = (uint32_t)(y + 1);
        if ((y & 15) == 15)
            vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
    }
    int64_t t1 = esp_timer_get_time();
    s_orbit_baking = false;
    if (!s_orbit_cancel)
        s_orbit_baked = target_deg; /* the on-screen frame IS this viewport */
    ESP_LOGI(TAG, "orbit %+d deg %s in %lldms", target_deg, s_orbit_cancel ? "interrupted" : "baked+swept",
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
 * out of the target being read at task start. */
static void moon_orbit_drain(void) {
    if (!CONFIG_UI_MOON_ORBIT_ENABLE || s_anim || s_orbit_baking || s_orbit_sweeping)
        return;
    if (s_orbit_target == 0 || s_orbit_target == s_orbit_baked)
        return; /* default viewport, or the on-screen frame is this viewport */
    moon_orbit_start();
}

/* Cancel the running orbit bake/sweep and leave a clean default-viewport
 * state. Called on the sleep transition and when the boot animation
 * (re)starts, both of which take over s_frame/s_rgb. */
static void moon_orbit_interrupt(void) {
    if (s_orbit_baking) {
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
    s_orbit_target = 0; /* sleep/anim shows the default viewport */
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
    ESP_LOGI(TAG, "DIAG imu %s roll %.1f | indev %d | anim %d | orbit t%d b%d sweep%d", ok ? "ok" : "FAIL",
             ok ? roll * 57.29577951308232f : -1000.0f, st, (int)s_anim, (int)s_orbit_target, (int)s_orbit_baked,
             (int)(s_orbit_baking || s_orbit_sweeping));
}

/* ---------------- tick ---------------- */

static void moon_tick_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ready || !s_started)
        return;
    if (s_anim) {
        if (!s_anim_running) {
            /* Producer done: the last baked frame is on screen; bake the
             * final "now" frame (identical to the static frame) and
             * release the idle timer. */
            moon_anim_skip_to_now();
            return;
        }
        /* Consumer side of the row-sweep pipeline: show every strip of
         * the current frame that the producer has fully converted. The
         * packed (frame, row) pair is one atomic 32-bit load. */
        uint32_t pub = s_anim_pub;
        int fr = (int)(pub >> 16);
        int row = (int)(pub & 0xFFFF);
        if (fr != s_anim_frame_shown) {
            s_anim_frame_shown = fr;
            s_anim_strip_shown = 0;
        }
        int strips_ready = row / MOON_STRIP_ROWS;
        if (strips_ready > MOON_NSTRIPS)
            strips_ready = MOON_NSTRIPS;
        if (strips_ready > s_anim_strip_shown) {
            for (int s = s_anim_strip_shown; s < strips_ready; s++) {
                /* Strip area in the moon image's (screen) coordinates. */
                lv_area_t a = {.x1 = (lv_coord_t)MOON_IMG_POS,
                               .y1 = (lv_coord_t)(MOON_IMG_POS + s * MOON_STRIP_ROWS),
                               .x2 = (lv_coord_t)(MOON_IMG_POS + MOON_PX - 1),
                               .y2 = (lv_coord_t)(MOON_IMG_POS + (s + 1) * MOON_STRIP_ROWS - 1)};
                for (int i = 0; i < s_n_images; i++)
                    lv_obj_invalidate_area(s_images[i], &a);
                s_anim_strip_shown = s + 1;
            }
            /* Drain the redraw now (the adapter's own flush path uses the
             * same lv_refr_now call): after this returns, the strips'
             * pixels are already copied out of s_rgb into LVGL's draw
             * buffers (the QSPI transfer then runs asynchronously from
             * the draw buffers). That is what lets the producer's
             * catch-up guard reuse s_rgb for the next frame without the
             * display tearing two frames together. */
            int64_t r0 = esp_timer_get_time();
            lv_refr_now(NULL);
            s_perf_refr_us += (uint32_t)(esp_timer_get_time() - r0);
        }
        /* Perf (task #22): the frame is fully on screen -- log both
         * cores' per-frame cost. "producer %%" is core-1 duty (work /
         * wall of the bake+convert), "core0 %%" is core-0 duty (the
         * summed lv_refr_now over the same wall window). */
        if (s_anim_strip_shown >= MOON_NSTRIPS) {
            int wall_ms = (int)(s_perf_wall_us / 1000);
            int refr_ms = (int)(s_perf_refr_us / 1000);
            ESP_LOGI(TAG,
                     "perf anim: geo %dms bake %dms conv %dms batchMax %dus | core0 refr %dms | "
                     "duty core1 %d%% core0 %d%%",
                     (int)(s_perf_geo_us / 1000), (int)(s_perf_bake_us / 1000), (int)(s_perf_conv_us / 1000),
                     (int)s_perf_batch_max_us, refr_ms,
                     wall_ms ? 100 * ((int)(s_perf_bake_us + s_perf_conv_us) / 1000) / wall_ms : 0,
                     wall_ms ? 100 * refr_ms / wall_ms : 0);
            s_perf_refr_us = 0;
        }
        return;
    }
    /* Orbit sweep consumer: show the strips of the freshly baked frame
     * the task has fully converted, top to bottom (same strip math as
     * the boot animation), so the new viewport sweeps in instead of
     * popping in. */
    if (s_orbit_sweeping) {
        int strips_ready = (int)((uint32_t)s_orbit_pub / MOON_STRIP_ROWS);
        if (strips_ready > MOON_NSTRIPS)
            strips_ready = MOON_NSTRIPS;
        if (strips_ready > s_orbit_strip_shown) {
            for (int s = s_orbit_strip_shown; s < strips_ready; s++) {
                lv_area_t a = {.x1 = (lv_coord_t)MOON_IMG_POS,
                               .y1 = (lv_coord_t)(MOON_IMG_POS + s * MOON_STRIP_ROWS),
                               .x2 = (lv_coord_t)(MOON_IMG_POS + MOON_PX - 1),
                               .y2 = (lv_coord_t)(MOON_IMG_POS + (s + 1) * MOON_STRIP_ROWS - 1)};
                for (int i = 0; i < s_n_images; i++)
                    lv_obj_invalidate_area(s_images[i], &a);
                s_orbit_strip_shown = s + 1;
            }
            /* Same drain as the boot animation: pixels out of s_rgb into
             * LVGL's draw buffers before the next strip may be reused. */
            lv_refr_now(NULL);
        }
        if (s_orbit_strip_shown >= MOON_NSTRIPS) {
            s_orbit_sweeping = false;
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
    /* Full size during the animation too (the sweep is full-resolution,
     * see the file header) so there is no size jump when it ends. */
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
    if (!s_ready || s_anim)
        return;
    s_baked_day = utc_epoch / 86400;
    moon_bake_epoch(utc_epoch);
}

void moon_ui_start(void) {
    if (!s_ready)
        return;
    s_started = true;
    if (CONFIG_UI_MOON_ANIM_ENABLE) {
        /* Row-sweep pipeline (see moon_anim_begin); the idle timer is held
         * for the whole sweep so the 10 s idle timeout cannot cut the
         * startup animation off mid-sweep. */
        moon_anim_begin();
    } else {
        int64_t now;
        if (clock_get_utc_epoch(&now) == ESP_OK) {
            s_baked_day = now / 86400;
            moon_bake_epoch(now);
        }
        lv_timer_resume(s_tick);
    }
}

void moon_ui_replay_animation(void) {
    /* Test hook (wake-from-sleep, see CONFIG_UI_MOON_ANIM_ON_WAKE): run
     * the sweep again over the last synodic month. No-op at boot order
     * (before moon_ui_start) and while a sweep is already running. */
    if (!s_ready || !s_started || s_anim)
        return;
    moon_anim_begin();
}

void moon_ui_day_tick(void) {
    if (!s_ready || s_anim)
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
    }
}

void moon_ui_pause(bool pause) {
    if (!s_tick)
        return;
    if (pause) {
        if (s_anim)
            moon_anim_skip_to_now(); /* jump straight to the current phase */
        moon_orbit_interrupt();      /* sleep shows the default viewport */
        lv_timer_pause(s_tick);
    } else
        lv_timer_resume(s_tick);
}
