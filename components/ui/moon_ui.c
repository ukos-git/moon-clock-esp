/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk, only when the
 *     phase changes (~daily or during the boot animation);
 *   - per-frame in-plane rotation (moon_rotate, Q15.15 fixed-point gather)
 *     of the baked frame into the RGB565 panel buffer -- the "ball in hand".
 *
 * Tilt input: the real QMI8658C IMU is a follow-up (Phase 4). Until then the
 * in-plane angle is driven by touch drag: the finger's angular velocity about
 * the disk center spins the ball (drag in a circle = spin; drag radially =
 * no spin). Sign convention (DRAG_SIGN) is the one knob to flip if the spin
 * feels mirrored on the panel.
 *
 * All buffers are octal PSRAM. The bake is the expensive pass (one
 * equirect texture sample + Lambert per pixel) and runs off the UI tick
 * only when needed; the rotation is a few int ops per pixel
 * (nearest-neighbor gather, 1 PSRAM read/pixel, x4-unrolled).
 *
 * The boot animation renders AND displays at a smaller size (MOON_ANIM_PX)
 * than the static frame (MOON_PX): ~1/4 the bake work per frame and less
 * QSPI traffic, so the synodic sweep stays a few seconds long. The sweep
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "clock.h"
#include "clock_idle.h"
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
/* Boot-animation display size. 200x200 (disk fills it, radius 100) is a
 * quarter of the static 400x400, so each animation bake is ~1/4 the work
 * and pushes ~1/4 the QSPI traffic. Centered on the 466x466 panel. */
#define MOON_ANIM_PX 200
#define MOON_ANIM_R (float)(MOON_DISK_RADIUS_PX * MOON_ANIM_PX / MOON_PX) /* 100.0 */
#define MOON_ANIM_IMG_POS ((466 - MOON_ANIM_PX) / 2)                      /* 133 */
/* Animation albedo: a 128x64 box-average (8x8) downsample of the full
 * 1024x512 PSRAM texture, kept in INTERNAL SRAM (8 KB). SRAM, not PSRAM,
 * so the animation bake's per-pixel texture reads do not share the
 * SPI2_HOST bus with the LCD QSPI DMA. 128 wide is ~0.65 texture px per
 * 200px screen px, so the maria (the animation's dominant feature) stay
 * smooth and craters are sub-pixel at the animation's display size. */
#define MOON_ANIM_ALB_W 128
#define MOON_ANIM_ALB_H 64

/* Boot animation: synodic month, ending on the current phase. */
#ifndef CONFIG_UI_MOON_ANIM_ENABLE
#define CONFIG_UI_MOON_ANIM_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_ANIM_FRAMES
#define CONFIG_UI_MOON_ANIM_FRAMES 29
#endif
#define SYNODIC_S (29.53 * 86400.0)

/* Touch-drag spin: +1 = clockwise spin for a clockwise finger drag. */
#define DRAG_SIGN 1.0f

static uint8_t *s_frame;          /* 8-bit baked frame, MOON_PX^2 */
static uint8_t *s_anim_frame;     /* 8-bit animation frame, MOON_ANIM_PX^2 */
static uint8_t *s_anim_albedo;    /* 256x128 SRAM albedo for the animation */
static uint16_t *s_rgb;           /* RGB565 static buffer, MOON_PX^2 */
static uint16_t *s_anim_rgb;      /* RGB565 animation buffer, MOON_ANIM_PX^2 */
static lv_image_dsc_t s_dsc;      /* full-res (MOON_PX), points at s_rgb */
static lv_image_dsc_t s_anim_dsc; /* low-res (MOON_ANIM_PX), points at s_anim_rgb */
static lv_obj_t *s_images[4];
static int s_n_images;
static lv_timer_t *s_tick;
static bool s_ready;
static bool s_started; /* moon_ui_start() called; ticks may run */

static float s_theta; /* in-plane angle, rad (ball-in-hand) */
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
    int32_t c15 = moon_cos15(s_theta);
    int32_t s15 = moon_sin15(s_theta);
    moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, c15, s15);
    moon_invalidate_all();
}

static void moon_bake_epoch(int64_t epoch) {
    moon_geom_t g;
    moon_geometry(epoch, &g);
    const uint8_t *alb = moon_albedo();
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; moon stays black");
        return;
    }
    moon_bake(&g, alb, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W, MOON_ALB_H);
    ESP_LOGI(TAG, "bake | phase %.1f%% illum, subsolar (%.1f, %.1f)", g.illum * 100.0f, g.subsolar_lon, g.subsolar_lat);
    moon_rotate_into_rgb();
}

/* One-time: box-average the full PSRAM albedo (MOON_ALB_W x MOON_ALB_H)
 * down to the MOON_ANIM_ALB_W x MOON_ANIM_ALB_H SRAM texture the animation
 * bake reads (8x8 average). */
static bool anim_albedo_build(void) {
    const uint8_t *full = moon_albedo();
    if (!full)
        return false;
    const int fx = MOON_ALB_W / MOON_ANIM_ALB_W; /* 1024/128 = 8 */
    const int fy = MOON_ALB_H / MOON_ANIM_ALB_H; /* 512/64   = 8 */
    for (int y = 0; y < MOON_ANIM_ALB_H; y++) {
        for (int x = 0; x < MOON_ANIM_ALB_W; x++) {
            uint32_t sum = 0;
            for (int dy = 0; dy < fy; dy++)
                for (int dx = 0; dx < fx; dx++)
                    sum += full[(y * fy + dy) * MOON_ALB_W + x * fx + dx];
            s_anim_albedo[y * MOON_ANIM_ALB_W + x] = (uint8_t)(sum / (fx * fy));
        }
    }
    return true;
}

/* Boot-animation frame: bake at MOON_ANIM_PX (~1/4 the work of a full-res
 * bake) from the SRAM albedo into the small animation buffer and display
 * it (no rotate -- the animation runs at zero in-plane angle). */
static void moon_bake_epoch_anim(int64_t epoch) {
    moon_geom_t g;
    moon_geometry(epoch, &g);
    if (!s_anim_albedo) {
        if (!anim_albedo_build()) {
            ESP_LOGE(TAG, "no albedo; moon stays black");
            return;
        }
    }
    moon_bake(&g, s_anim_albedo, s_anim_frame, MOON_ANIM_PX, MOON_ANIM_R, (float)(MOON_ANIM_PX / 2),
              (float)(MOON_ANIM_PX / 2), MOON_ANIM_ALB_W, MOON_ANIM_ALB_H);
    for (int y = 0; y < MOON_ANIM_PX; y++)
        for (int x = 0; x < MOON_ANIM_PX; x++)
            s_anim_rgb[y * MOON_ANIM_PX + x] = moon_gray565(s_anim_frame[y * MOON_ANIM_PX + x]);
    moon_invalidate_all();
}

/* Point every moon image at either the low-res animation dsc (small,
 * centered) or the full-res static dsc (MOON_PX), and resize/reposition
 * the objects to match. Called when the animation starts and ends. */
static void moon_set_image_state(bool anim) {
    lv_image_dsc_t *dsc = anim ? &s_anim_dsc : &s_dsc;
    int px = anim ? MOON_ANIM_PX : MOON_PX;
    int pos = anim ? MOON_ANIM_IMG_POS : MOON_IMG_POS;
    for (int i = 0; i < s_n_images; i++) {
        lv_obj_set_pos(s_images[i], pos, pos);
        lv_obj_set_size(s_images[i], px, px);
        lv_image_set_src(s_images[i], dsc);
        lv_obj_invalidate(s_images[i]);
    }
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

/* Finish the animation early and show the current phase at full
 * resolution (animation done, or a sleep transition interrupts it). */
static void moon_anim_skip_to_now(void) {
    s_anim = false;
    /* The boot animation holds the idle timer (see moon_ui_start). Release
     * it only when the animation ends on its own; if a sleep transition
     * drove this path we are already idle and must not re-arm it. */
    if (!clock_is_idle())
        clock_idle_set_enabled(true);
    /* Grow the images back to the full-res dsc before baking the static
     * frame, so the full frame displays at MOON_PX. */
    moon_set_image_state(false);
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
        /* Final frame is exactly "now"; re-bake at full resolution so the
         * static phase the UI shows matches the animation's end. */
        moon_anim_skip_to_now();
        return;
    }
    /* Evenly spaced over the last synodic month, t_k -> now as k -> frames. */
    double t = (double)(frames - 1 - s_anim_i) / (double)(frames - 1);
    int64_t epoch = (int64_t)(s_anim_end - t * SYNODIC_S);
    moon_bake_epoch_anim(epoch);
    s_anim_i++;
    /* One-tick yield so IDLE0 (and the 5 s TWDT) is fed across the back-to-back
     * animation bakes. */
    vTaskDelay(1);
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
    /* A resting ball is static: only rotate while the finger is dragging,
     * so the 30fps path runs only during a spin. (The real IMU will feed a
     * continuous angle in Phase 4.) */
    moon_drag_update();
    if (s_drag)
        moon_rotate_into_rgb();
}

/* ---------------- public API ---------------- */

void moon_ui_init(void) {
    if (s_ready)
        return;

    s_frame = heap_caps_malloc(MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
    s_anim_frame = heap_caps_malloc(MOON_ANIM_PX * MOON_ANIM_PX, MALLOC_CAP_SPIRAM);
    s_rgb = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    s_anim_rgb = heap_caps_malloc((size_t)MOON_ANIM_PX * MOON_ANIM_PX * 2, MALLOC_CAP_SPIRAM);
    /* Animation albedo in INTERNAL SRAM (see MOON_ANIM_ALB_* note): the
     * animation bake reads it per pixel, so it must not share the SPI2_HOST
     * bus with the LCD. Filled lazily from the PSRAM texture on first frame. */
    s_anim_albedo = heap_caps_malloc((size_t)MOON_ANIM_ALB_W * MOON_ANIM_ALB_H, MALLOC_CAP_INTERNAL);
    if (!s_frame || !s_anim_frame || !s_rgb || !s_anim_rgb || !s_anim_albedo) {
        ESP_LOGE(TAG, "allocation failed (PSRAM/SRAM); moon disabled");
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
    memset(s_anim_frame, 0, MOON_ANIM_PX * MOON_ANIM_PX);
    memset(s_rgb, 0, (size_t)MOON_PX * MOON_PX * 2);
    memset(s_anim_rgb, 0, (size_t)MOON_ANIM_PX * MOON_ANIM_PX * 2);

    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.flags = 0;
    s_dsc.header.w = MOON_PX;
    s_dsc.header.h = MOON_PX;
    s_dsc.header.stride = MOON_PX * 2;
    s_dsc.data_size = (uint32_t)MOON_PX * MOON_PX * 2;
    s_dsc.data = (const uint8_t *)s_rgb;

    s_anim_dsc = s_dsc; /* then override the size fields below */
    s_anim_dsc.header.w = MOON_ANIM_PX;
    s_anim_dsc.header.h = MOON_ANIM_PX;
    s_anim_dsc.header.stride = MOON_ANIM_PX * 2;
    s_anim_dsc.data_size = (uint32_t)MOON_ANIM_PX * MOON_ANIM_PX * 2;
    s_anim_dsc.data = (const uint8_t *)s_anim_rgb;

    s_tick = lv_timer_create(moon_tick_cb, MOON_TICK_MS, NULL);
    lv_timer_pause(s_tick);
    s_ready = true;
    ESP_LOGI(TAG, "moon UI ready (frame=%p rgb=%p)", (void *)s_frame, (void *)s_rgb);
}

lv_obj_t *moon_ui_create_image(lv_obj_t *parent) {
    if (!s_ready)
        return NULL;
    /* Respect the current state: an image created mid-animation (a screen
     * rebuild) must show the low-res dsc, not the stale full-res frame. */
    bool anim = s_anim;
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_pos(img, anim ? MOON_ANIM_IMG_POS : MOON_IMG_POS, anim ? MOON_ANIM_IMG_POS : MOON_IMG_POS);
    lv_obj_set_size(img, anim ? MOON_ANIM_PX : MOON_PX, anim ? MOON_ANIM_PX : MOON_PX);
    lv_image_set_src(img, anim ? &s_anim_dsc : &s_dsc);
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
        moon_set_image_state(true); /* show the small animation image */
        lv_timer_resume(s_tick);
        /* The sweep takes ~9s (29 frames x ~300ms). Hold the idle->sleep
         * timer for its duration so the startup animation is never cut off
         * by the 10s idle timeout; moon_anim_skip_to_now re-arms it on
         * natural completion (a real sleep transition leaves it off). */
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
