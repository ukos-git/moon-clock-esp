/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk, only when the
 *     phase changes (~daily or during the boot animation);
 *   - per-frame in-plane rotation (moon_rotate, Q15.15 fixed-point gather)
 *     of the baked frame into the RGB565 panel buffer -- the "ball in hand".
 *
 * Tilt input: the on-board QMI8658 (components/imu) feeds the screen-plane
 * roll (gravity vector, EMA-smoothed); in normal modes the in-plane angle
 * follows the tilt -- theta = UI_IMU_ANGLE_SIGN * (offset - roll) -- so
 * turning the device turns the moon "ball in hand". Touch drag adds to the
 * angle (drag in a circle = spin; drag radially = no spin); both input
 * paths compose. With the IMU absent or UI_IMU_ENABLE off the moon is
 * static and only drag spins it. The IMU angle sign/offset are Kconfig
 * knobs (UI_IMU_ANGLE_SIGN / UI_IMU_ROLL_OFFSET_DEG) because the board's
 * IMU mounting orientation is not known at code level.
 *
 * All buffers are octal PSRAM. The bake is the expensive pass (one
 * equirect texture sample + Lambert per pixel) and runs off the UI tick
 * only when needed; the rotation is a few int ops per pixel
 * (nearest-neighbor gather, 1 PSRAM read/pixel, x4-unrolled).
 *
 * The boot animation is the same full-resolution bake as the static frame
 * (one moon_bake_epoch per frame over the last synodic month); the sweep
 * holds the idle->sleep timer for its duration (see moon_ui_start), so it
 * completes even when the device would otherwise sleep mid-boot.
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
#ifndef CONFIG_UI_IMU_ROLL_OFFSET_DEG
#define CONFIG_UI_IMU_ROLL_OFFSET_DEG 0
#endif
#define SYNODIC_S (29.53 * 86400.0)

/* Touch-drag spin: +1 = clockwise spin for a clockwise finger drag. */
#define DRAG_SIGN 1.0f

static uint8_t *s_frame;     /* 8-bit baked frame, MOON_PX^2 */
static uint16_t *s_rgb;      /* RGB565 display buffer, MOON_PX^2 */
static lv_image_dsc_t s_dsc; /* (MOON_PX), points at s_rgb */
static lv_obj_t *s_images[4];
static int s_n_images;
static lv_timer_t *s_tick;
static bool s_ready;
static bool s_started; /* moon_ui_start() called; ticks may run */

static float s_theta;              /* in-plane angle, rad (ball-in-hand) */
static float s_last_rotated_theta; /* last angle the RGB buffer was built at */
static bool s_drag;
static int32_t s_last_x, s_last_y;

static int64_t s_baked_day; /* calendar day of the last static bake */
static bool s_anim;         /* boot animation in progress */
static int s_anim_i;        /* animation frame counter */
static int64_t s_anim_end;  /* epoch the animation ends on (now) */

static void moon_invalidate_all(void) {
    for (int i = 0; i < s_n_images; i++)
        lv_obj_invalidate(s_images[i]);
}

static void moon_rotate_into_rgb(void) {
    /* The boot animation (and the initial bake) run at zero in-plane
     * angle: skip the inverse-rotation gather and convert directly
     * (160k PSRAM gathers saved per frame). theta==0.0f holds for the
     * animation; a dragged spin leaves theta != 0 and takes the full path. */
    if (s_theta == 0.0f) {
        for (int i = 0; i < MOON_PX * MOON_PX; i++)
            s_rgb[i] = moon_gray565(s_frame[i]);
    } else {
        int32_t c15 = moon_cos15(s_theta);
        int32_t s15 = moon_sin15(s_theta);
        moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, c15, s15);
    }
    moon_invalidate_all();
}

static void moon_bake_epoch(int64_t epoch) {
    moon_geom_t g;
    int64_t t0 = esp_timer_get_time();
    moon_geometry(epoch, &g);
    int64_t t1 = esp_timer_get_time();
    const uint8_t *alb = moon_albedo();
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; moon stays black");
        return;
    }
    const int8_t *dem = moon_dem();
    if (!dem)
        ESP_LOGW(TAG, "no relief DEM; baking relief-free");
    moon_bake(&g, alb, dem, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H);
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | phase %.1f%% illum, subsolar (%.1f, %.1f) | geo %lldms bake %lldms", g.illum * 100.0f,
             g.subsolar_lon, g.subsolar_lat, (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000));
    moon_rotate_into_rgb();
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | rgb565 %lldms (total %lldms)", (long long)((t3 - t2) / 1000), (long long)((t3 - t0) / 1000));
}

/* ---------------- touch-drag "ball in hand" ---------------- */

static void moon_drag_update(void) {
    /* First pointer indev (the CST9217 touch, registered by the BSP via the
     * LVGL adapter). Querying LVGL directly keeps this free of the BSP header. */
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
        return;
    lv_indev_state_t st = lv_indev_get_state(indev);
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (st != LV_INDEV_STATE_PRESSED) {
        s_drag = false;
        return;
    }
    if (!s_drag) {
        s_drag = true;
        s_last_x = p.x;
        s_last_y = p.y;
        return;
    }
    int32_t dx = p.x - s_last_x;
    int32_t dy = p.y - s_last_y;
    s_last_x = p.x;
    s_last_y = p.y;
    if (dx == 0 && dy == 0)
        return;

    /* Angular velocity of the finger about the disk center:
     *   dtheta = (r x v) . z / |r|^2  (screen coords, y down)
     * A small dead zone near the center keeps the spin stable. */
    int32_t rx = p.x - MOON_CX;
    int32_t ry = p.y - MOON_CY;
    float r2 = (float)rx * rx + (float)ry * ry;
    float rmin = MOON_R * 0.2f;
    if (r2 < rmin * rmin)
        r2 = rmin * rmin;
    s_theta += DRAG_SIGN * ((float)rx * (float)dy - (float)ry * (float)dx) / r2;
}

/* ---------------- boot animation ---------------- */

/* Finish the animation early and show the current phase (animation done,
 * or a sleep transition interrupts it). */
static void moon_anim_skip_to_now(void) {
    s_anim = false;
    /* The boot animation holds the idle timer (see moon_ui_start). Release
     * it only when the animation ends on its own; if a sleep transition
     * drove this path we are already idle and must not re-arm it. */
    if (!clock_is_idle())
        clock_idle_set_enabled(true);
    int64_t now;
    if (clock_get_utc_epoch(&now) == ESP_OK) {
        s_baked_day = now / 86400;
        moon_bake_epoch(now);
        ESP_LOGI(TAG, "boot animation done, now static");
    }
}

static void moon_anim_next(void) {
    int frames = CONFIG_UI_MOON_ANIM_FRAMES;
    if (s_anim_i >= frames) {
        /* Final frame is exactly "now" (the static frame). */
        moon_anim_skip_to_now();
        return;
    }
    /* Evenly spaced over the last synodic month, t_k -> now as k -> frames.
     * Full-resolution bake at zero in-plane angle (see moon_bake_epoch). */
    double t = (double)(frames - 1 - s_anim_i) / (double)(frames - 1);
    int64_t epoch = (int64_t)(s_anim_end - t * SYNODIC_S);
    moon_bake_epoch(epoch);
    s_anim_i++;
    /* One-tick yield so IDLE0 (and the 5 s TWDT) is fed across the
     * back-to-back animation bakes (~1s each at full resolution). */
    vTaskDelay(1);
}

/* ---------------- tilt (QMI8658) ---------------- */

/* The device's screen-plane roll sets the ball angle: theta = sign *
 * (offset - roll). The driver EMA-smooths the roll and drops implausible
 * samples; a missing IMU (or UI_IMU_ENABLE off) leaves the angle alone,
 * so the moon is static and only drag spins it. */
static void moon_imu_tick(void) {
    if (!CONFIG_UI_IMU_ENABLE)
        return;
    float roll;
    if (!imu_read_roll(&roll))
        return;
    static const float DEG = 0.017453292519943295f;
    float th = CONFIG_UI_IMU_ANGLE_SIGN * ((float)CONFIG_UI_IMU_ROLL_OFFSET_DEG * DEG - roll);
    while (th > 3.14159265f)
        th -= 6.2831853f;
    while (th < -3.14159265f)
        th += 6.2831853f;
    s_theta = th;
}

/* ---------------- tick ---------------- */

static void moon_tick_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ready || !s_started)
        return;
    if (s_anim) {
        s_theta = 0.0f;
        moon_anim_next();
        return;
    }
    moon_imu_tick();    /* tilt sets the angle (no-op without an IMU) */
    moon_drag_update(); /* touch drag adds to it */
    /* Rotate only when the angle actually moved: a resting ball is static
     * (the IMU EMA settles, so at rest the angle stops changing) and the
     * 30fps gather pass runs only during a tilt or a spin. */
    if (s_theta != s_last_rotated_theta) {
        s_last_rotated_theta = s_theta;
        moon_rotate_into_rgb();
    }
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
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK) {
        /* No trustworthy time (RTC not set / no NTP): show nothing rather
         * than a wrong phase. The daily day-tick will pick it up later. */
        ESP_LOGW(TAG, "no UTC time yet; moon stays blank until the clock syncs");
        return;
    }
    s_anim_end = now;
    s_baked_day = now / 86400;
    s_started = true;
    if (CONFIG_UI_MOON_ANIM_ENABLE) {
        s_anim = true;
        s_anim_i = 0;
        s_theta = 0.0f;
        lv_timer_resume(s_tick);
        /* The sweep is full-resolution bakes (~0.7s each + QSPI flush);
         * hold the idle->sleep timer for its duration so the startup
         * animation is never cut off by the 10s idle timeout;
         * moon_anim_skip_to_now re-arms it on natural completion (a real
         * sleep transition leaves it off). */
        clock_idle_set_enabled(false);
        ESP_LOGI(TAG, "boot animation: %d frames over the last synodic month", CONFIG_UI_MOON_ANIM_FRAMES);
    } else {
        moon_bake_epoch(now);
        lv_timer_resume(s_tick);
    }
}

void moon_ui_day_tick(void) {
    if (!s_ready || s_anim)
        return;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK)
        return;
    int64_t day = now / 86400;
    if (day != s_baked_day) {
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
        lv_timer_pause(s_tick);
    } else
        lv_timer_resume(s_tick);
}
