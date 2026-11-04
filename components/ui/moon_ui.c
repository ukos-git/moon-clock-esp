/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk in the FIXED
 *     Earth viewport, only when the phase changes (~daily) or when the
 *     tilt moves the orbit (sphere model, below);
 *   - the free spin (burn-in protection): the baked frame is then
 *     rotated in place on the panel (moon_rotate, a nearest-neighbor
 *     gather of ~6 ms per frame -- no re-bake). The angle is a FREE
 *     integration (theta += omega*dt, see the spin section): the rate
 *     eases between an ACTIVE value (one turn per
 *     UI_MOON_SPIN_PERIOD_S while the device was used within the
 *     settle window) and a PHASE-TRUE COAST (one turn per synodic
 *     moon month when settled), so the on-screen pose tracks the real
 *     moon phase and the lit pixels keep moving around the panel. The
 *     spin render runs on core 1, the display (LVGL) on core 0; they
 *     are serialized on the frame buffer (a mutex + a rendered/shown
 *     handshake) so the display never tears and the angular speed is
 *     exact (time-based angle) no matter how many frames the display
 *     manages.
 *
 * The spin replaced the old boot animation (a month-long phase sweep):
 * that was display-bound and showed a visible top-to-bottom redraw (the
 * top half of the new frame over the bottom half of the old one). A
 * rotation needs no sweep line at all -- the whole frame is one cheap
 * gather displayed whole, so the redraw matches the compute by
 * construction. The month sweep returns as the SYNODIC WAKE SWEEP (see
 * the sweep section below), played on the same atomic whole-frame
 * pipeline: the last 29.5 days in N frames, on every wake from sleep.
 *
 * Display invariant (the "no top-to-bottom" rule): every frame the
 * pipeline produces -- spin frame, orbit sweep frame, wake sweep frame,
 * re-bake, and even a screen switch -- is displayed as ONE atomic
 * whole-frame / whole-panel lv_refr_now under the buffer mutex. A frame
 * must never be left for LVGL's async refresh to drain in 25-row strips
 * (that is the visible top-to-bottom crawl), and no pending full-screen
 * invalidation (e.g. a screen-load animation) may ride along in one of
 * these flushes -- a screen switch is a plain lv_scr_load + one atomic
 * whole-panel flush (moon_ui_screen_switched). The two sanctioned
 * exceptions: the burn-in spin's direct panel write (see the spin
 * section) and the screen-switch fade (UI_SCREEN_SWITCH_FADE) -- a
 * hardware panel-brightness ramp that writes no pixels, whose one
 * atomic flush of the new screen runs while the panel is dimmed to
 * black.
 *
 * Tilt input: the on-board QMI8658 (components/imu) is the ONLY input
 * to the moon (touch is the core-UI wake input, never a moon input).
 * The moon keeps the FIXED Earth viewport (the sub-Earth point is up,
 * posangle applied); the tilt changes only which side of the sphere the
 * camera sees (the in-plane rotation is the free spin above,
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
 * the finished frame into the display with the SAME atomic whole-frame
 * display (MOON_SPIN_BATCH = MOON_NSTRIPS) as the free spin, so the
 * viewport change lands as one ~150 ms frame -- the same sequential
 * update as the spin -- instead of a progressive top-to-bottom wipe
 * over the bake. The in-plane spin is held for the bake+sweep so no
 * rotated frame crosses the sweep.
 * Latest-wins: a target that moves mid-bake starts a fresh bake right
 * after.
 *
 * Tilt is also the activity/wake input: a 1 Hz monitor measures the
 * DEPARTURE of the current pose from a settled-pose reference (a slow
 * EMA that keeps absorbing the pose while settled -- it tracks the
 * measured at-rest drift, so at rest the departure stays ~0.2 deg or
 * less, far below either threshold) and reports sustained departure
 * to the idle system,
 * which holds the idle timer while the user manipulates the ball
 * (departure above UI_IMU_ACTIVITY_DEG while awake) and wakes from
 * sleep (departure above UI_IMU_WAKE_DEG, the deliberate-pick-up bar;
 * both gated by a two-consecutive-sample streak).
 *
 * All buffers are octal PSRAM. The bake is the expensive pass (one
 * equirect texture sample + Lambert per pixel) and runs off the UI tick
 * only when needed; the rotation is a few int ops per pixel
 * (nearest-neighbor gather, 1 PSRAM read/pixel, x4-unrolled).
 *
 * The spin pipeline (see the spin section below): the render task
 * (core 1) waits until the display has drained the previous frame out of
 * s_rgb, integrates the free spin angle (theta += omega*dt, the rate
 * smoothed toward active / phase-true coast), runs moon_rotate over the
 * whole frame (under the buffer mutex) and hands the frame to the
 * display; the LVGL tick (core 0) invalidates the whole moon image,
 * drains the redraw synchronously (lv_refr_now) and hands the buffer
 * back. The display paces the spin; the time-integrated angle keeps the
 * angular speed constant and a dropped frame becomes a bigger step,
 * never a repeat or a stall. While the orbit owns the buffer (its
 * re-bake + sweep, ~3 s) the render blocks, and the tick zeroes the
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
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "esp_lcd_panel_interface.h" /* the full vtable struct (s_panel->draw_bitmap) */
#include "esp_lcd_panel_ops.h"

#include "board_display.h"
#include "bsp/display.h"

#include "clock.h"
#include "clock_idle.h"
#include "clock_timer.h"
#include "imu.h"
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"
#include "moon_ui.h"
#include "settings_store.h"

static const char *TAG = "moon_ui";

/* The moon image is a disk (radius MOON_PX / 2, the asset pipeline's native
 * size on the S3) centered on the panel with a black margin -- 26% less
 * bake/rotate/display traffic than baking the whole panel. The disk fills
 * 85.8% of the panel on the S3 (400/466) and 90.3% on the P4 (650/720 --
 * the P4 default: gap ~= half the seconds-arc thickness to the ring), so
 * the moon stays proportional. Everything below derives from MOON_PX +
 * the panel size (BSP_LCD_H_RES), so the portable core (which takes
 * size/radius as arguments) is untouched. The bake/rotate math works in
 * image coordinates (center MOON_PX/2); the touch math works in panel
 * coordinates (center MOON_CX -- the same point, so the drag math is
 * unchanged). */
#if defined(CONFIG_BOARD_P4)
#define MOON_PX 650
#else
#define MOON_PX 400
#endif
#define MOON_IMG_POS ((BSP_LCD_H_RES - MOON_PX) / 2)
#define MOON_CX (BSP_LCD_H_RES / 2)
#define MOON_CY (BSP_LCD_V_RES / 2)
/* The bake/rotate sphere radius = the image half (the disk touches all four
 * edges): 200 = 400/2 on the S3 (the asset's native MOON_DISK_RADIUS_PX),
 * 325 = 650/2 on the P4 -- so this generalizes with no S3 change. */
#define MOON_R ((float)MOON_PX / 2.0f)
#define MOON_TICK_MS 50
/* Animation display strip = the rows per panel write. S3: 25 rows (= the BSP
 * QSPI draw-buffer height; one invalidated strip = one draw buffer + one
 * QSPI transfer; 16 strips tile the 400 px moon). P4: 130 rows (5 strips
 * tile the 650 px moon) -- the MIPI-DSI panel has no S3 32 KiB SPI-DMA
 * cap, so the strip height is a pacing choice, not a hard limit. (The
 * display strips here only pace LVGL invalidations; the flush itself
 * always writes the BSP's 50-row buffer strips.) */
#if defined(CONFIG_BOARD_P4)
#define MOON_STRIP_ROWS 130
#else
#define MOON_STRIP_ROWS 25
#endif
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
#define CONFIG_UI_MOON_SPIN_PERIOD_S 300
#endif
#ifndef CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S
#define CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S 600
#endif
#ifndef CONFIG_UI_MOON_SPIN_FPS
#define CONFIG_UI_MOON_SPIN_FPS 8
#endif
#ifndef CONFIG_UI_SCREEN_SWITCH_FADE
#define CONFIG_UI_SCREEN_SWITCH_FADE 0
#endif
#ifndef CONFIG_UI_IMU_ENABLE
#define CONFIG_UI_IMU_ENABLE 1
#endif
#ifndef CONFIG_UI_IMU_ANGLE_SIGN
#define CONFIG_UI_IMU_ANGLE_SIGN 1
#endif
/* Fallback 0, NOT 1: a Kconfig "n" leaves the macro UNDEFINED in
 * sdkconfig.h, so a #define-to-1 fallback would silently re-enable the
 * orbit (the same gotcha the UI_MOON_ANIM_ENABLE note documents). The
 * orbit (the ~2 s relief-DEM re-bake at the tilt-driven cadence) cannot
 * be served on the ESP32-S3 -- disabled by default; re-enable on the
 * ESP32-P4 via CONFIG_UI_MOON_ORBIT_ENABLE=y. */
#ifndef CONFIG_UI_MOON_ORBIT_ENABLE
#define CONFIG_UI_MOON_ORBIT_ENABLE 0
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
#ifndef CONFIG_UI_MOON_ANIM_ENABLE
#define CONFIG_UI_MOON_ANIM_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_DUSK_ENABLE
#define CONFIG_UI_MOON_DUSK_ENABLE 1
#endif
#ifndef CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC
#define CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC 30
#endif

/* The animation on/off flags are runtime settings (the sleep settings
 * screens override them and persist them); the Kconfig macros are only
 * the build-time defaults. The countdown is a runtime setting too (the
 * deep-sleep settings screen changes it). */
static bool boot_anim_enabled(void) { return CONFIG_UI_MOON_ANIM_ENABLE && settings_boot_anim_enable(); }
static bool dusk_anim_enabled(void) { return CONFIG_UI_MOON_DUSK_ENABLE && settings_dusk_anim_enable(); }
#ifndef CONFIG_UI_MOON_ANIM_FRAMES
#define CONFIG_UI_MOON_ANIM_FRAMES 168
#endif
#ifndef CONFIG_UI_MOON_ANIM_BAKE_PX
#define CONFIG_UI_MOON_ANIM_BAKE_PX 240
#endif
/* Perf A/B hook (unset bools are absent from sdkconfig.h; fall back to
 * 0 = relief ON, the normal path). */
#ifndef CONFIG_TEST_BAKE_NO_RELIEF
#define CONFIG_TEST_BAKE_NO_RELIEF 0
#endif
static uint8_t *s_frame;       /* 8-bit baked frame, MOON_PX^2 */
static uint16_t *s_rgb;        /* RGB565 display buffer, MOON_PX^2 */
static uint8_t *s_sweep_small; /* reduced-res intermediate sweep frame (see
                                * UI_MOON_ANIM_BAKE_PX); MOON_PX^2 if the
                                * reduction is disabled */
static uint8_t *s_shade_map;   /* the dusk's pre-shaded body-frame equirect
                                * map (MOON_ALB_W x MOON_ALB_H 8-bit; see the
                                * dusk section -- built once per dusk epoch,
                                * every dusk frame re-projects it instead of
                                * re-baking the lighting) */
static lv_image_dsc_t s_dsc;   /* (MOON_PX), points at s_rgb */
static lv_obj_t *s_images[8];  /* 4 moon screens today; headroom for more */
static int s_n_images;
static lv_obj_t *s_moon_roots[8];          /* the screen root each image was created in */
static volatile bool s_moon_screen = true; /* the active screen carries a moon image */
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

/* Spin pipeline (see the spin section below). Render task
 * (core 1): integrates the free spin (theta += omega*dt, the rate
 * smoothed toward active/settled -- see the section) under the buffer
 * mutex, runs moon_rotate over the WHOLE 400x400 frame (a ~8 ms
 * nearest-neighbor gather) and writes the moon area STRAIGHT to the
 * panel (moon_spin_write_panel: one continuous 400x400 QSPI window
 * write under the panel mutex, each frame atomic) -- the old LVGL whole-panel
 * lv_refr_now (~175 ms, ~6 fps, ~150 ms of LVGL per-strip work on the
 * touch core) is gone, and a dropped frame is a bigger angular step,
 * never a stall. The buffer mutex guards every reader/writer of s_rgb
 * (the render, the tick's batched orbit/sweep display, the static LUT
 * convert) so those paths never tear. */
/* Strips (25 rows) displayed per tick (MOON_NSTRIPS of them per atomic
 * whole frame). The orbit sweep reuses this constant, so a tilt
 * viewport change also lands as one atomic lv_refr_now frame (~175 ms
 * measured for the full 400-row frame, comfortably under the 5 s Task
 * WDT). The spin no longer displays through the tick -- its frames go
 * out on the direct panel write (see the spin section). */
#define MOON_SPIN_BATCH MOON_NSTRIPS
#ifndef CONFIG_UI_MOON_SPIN_SETTLE_S
#define CONFIG_UI_MOON_SPIN_SETTLE_S 10
#endif
static volatile bool s_spin_running;    /* render task alive */
static volatile bool s_spin_cancel;     /* stop the render (sleep transition) */
static double s_spin_theta;             /* free spin angle, rad, unbounded (the render
                                         * integrates it; 0 = the identity pose) */
static double s_spin_omega;             /* current spin rate, rad/s (smoothed) */
static volatile int64_t s_spin_last_ms; /* render time of the last frame */
static volatile int s_spin_deg;         /* last displayed angle, deg (park log) */
static volatile float s_spin_last_rad;  /* the on-screen orientation (the last
                                         * displayed spin frame; 0 after an
                                         * orbit sweep or a re-bake re-anchor).
                                         * Read on wake to resume the spin from
                                         * exactly where it paused. */
static TaskHandle_t s_spin_task;
static SemaphoreHandle_t s_rgb_mtx; /* guards s_rgb (+ s_frame while baking) */

/* The spin's DIRECT PANEL WRITE (see the spin section): the render task
 * (core 1) composites the rotated moon with LVGL's current on-panel
 * content (the clock labels + seconds arc the spin must not clobber)
 * and writes the moon area straight to the panel -- the whole 400x400
 * window in one panel draw_bitmap on the S3 (the bus chunks it at the
 * 466x25x2 LVGL strip size with CS held), 65-row DSI strips on the
 * P4, instead of LVGL's ~175 ms whole-panel re-render. The panel mutex serializes those writes with LVGL's own
 * flushes (which reach the panel through the wrapped draw_bitmap);
 * the front mirror (s_front) is what the wrapper copies on every
 * LVGL strip write, so the overlay always sees fresh label/arc
 * pixels. s_disp is the per-frame composite (MOON_PX x MOON_PX,
 * panel frame: the moon rotated by the ORIENTATION step, the UI
 * pixels kept from the front mirror). */
/* The strip pitch. On the S3 the production write is the whole 400x400
 * window (one CASET+RASET; 25 rows only sizes the io driver's 23,300 B
 * chunk and the TEST_SPIN_BANDS diagnostic). On the P4 the strips are
 * SMALL, not large: the panel (like the S3's PSRAM) is X16 and XIP --
 * both cores fetch instructions from the same PSRAM the spin strips read
 * out of -- so one 206-row (255 KB)
 * strip read occasionally starves the DPI controller's FIFO ("can't fetch
 * data from external memory fast enough, underrun" -- measured 2026-10-31,
 * one underrun per ~28 s of settled spin). 103 rows (6 strips, 127 KB)
 * halves the burst and the underrun goes away; for the 650 px disk the
 * strips are 65 rows (10 strips, 84.5 KB) -- an even smaller burst, the
 * same proven-safe class. */
#if defined(CONFIG_BOARD_P4)
#define MOON_SPIN_STRIP_ROWS 65
#else
#define MOON_SPIN_STRIP_ROWS 25
#endif
#define MOON_SPIN_NSTRIPS (MOON_PX / MOON_SPIN_STRIP_ROWS)
#if (MOON_PX % MOON_SPIN_STRIP_ROWS) != 0
#error "MOON_PX must be a multiple of MOON_SPIN_STRIP_ROWS"
#endif
#ifndef CONFIG_TEST_SPIN_BANDS
#define CONFIG_TEST_SPIN_BANDS 0
#endif
/* The UI keep-mask (per-pixel, one bit per moon-area pixel): the spin
 * writes the rotated moon everywhere EXCEPT where LVGL's own on-panel
 * UI lives. The UI lives INSIDE the disk (the clock label centred on
 * it, the indicator below) AND in the seconds-arc ring OUTSIDE it
 * (measured from the real render: ring r 211-222, knob r 206-229), so
 * a radius rule cannot separate moon from UI -- the mask is built from
 * the analytic ring band + a full-screen lv_snapshot with the moon
 * image hidden (the root is black everywhere, so every non-black pixel
 * is UI), rotated by the current orientation step (the snapshot is
 * screen-local; the panel is rotated by the root transform), and
 * refreshed on every screen switch, on a clock/alarm label change (the
 * only content that changes shape while a moon screen is active; the
 * ring/knob never leave the band, so their mask is static), and on an
 * orientation step change (the mask is screen-local). */
/* The panel vtable's draw_bitmap member (this IDF exposes the vtable
 * struct but no typedef for its member) -- the wrapper's type and the
 * saved original's type. */
typedef esp_err_t (*moon_panel_draw_t)(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                       const void *color_data);

static SemaphoreHandle_t s_panel_mtx;  /* the spin's panel writes vs LVGL's */
static esp_lcd_panel_handle_t s_panel; /* the CO5300 panel handle */
static moon_panel_draw_t s_orig_draw;  /* the panel's real draw_bitmap (the wrapper's callee) */
static uint16_t *s_front;              /* front mirror, MOON_PX^2 (the moon area) */
static uint16_t *s_disp;               /* the spin's composite buffer, MOON_PX^2 */
static volatile int s_panel_step;      /* the orientation step 0..3 CW (90 deg each) */
static volatile bool s_panel_wrap;     /* the draw_bitmap wrapper is installed */
static uint8_t *s_ui_mask;             /* the per-pixel UI keep-mask, MOON_PX^2 */
static volatile bool s_mask_dirty;     /* the mask must be re-rotated (step
                                          change); the LVGL task picks it up */
/* Forward decl: the screen-switch fade (defined later) holds the spin's panel
 * writes while its 0x51 brightness ramp runs (the fade section). */
static volatile bool s_fade_busy;

/* The panel-io watchdog. Every QSPI panel write goes through the panel io's
 * ONE spi_device and the GPSPI bus lock: if a queued DMA transfer's
 * completion is ever lost (the CO5300 + QSPI driver has lost one on this
 * board -- a deterministic repro: a whole-panel flush in flight while the
 * spin's strip writes race it), the bus lock's background (REQ/PEND) bit
 * is never cleared, and EVERY later spi_device_acquire_bus -- LVGL's
 * flushes, the spin's strips, the 0x51 brightness writes -- blocks forever
 * in dev_wait (portMAX_DELAY), with the mutex still held: a silent,
 * no-WDT, permanent freeze (two tasks parked on one bus lock, no holder).
 * s_panel_mtx is held only inside a write (a strip ~10 ms, the 16-strip
 * spin frame ~30-80 ms, the boot/refresh flush <500 ms), so a hold longer
 * than the limit below can ONLY mean the bus is stuck -- no legitimate
 * path waits that long (the spin task is the sole other holder, and it
 * holds no more than a frame). The watchdog takes the mutex and, when the
 * wait overruns, restarts: a clean reboot instead of a dead screen. */
#define PANEL_IO_WATCHDOG_LIMIT_MS 3000
static TaskHandle_t s_panel_watchdog;
static volatile bool s_panel_watchdog_armed; /* moon_ui_init succeeded */

/* Swap the bytes of a 16-bit pixel for the QSPI panel (the panel io
 * sends 16-bit pixels MSB-first, the OTHER interface sends them
 * little-endian; the direct write must match the panel, so LVGL-native
 * RGB565 is byte-swapped on the way in -- the same catch the old
 * reveal's snapshot write handled in place). The P4's MIPI-DSI panel
 * wants LVGL-native order instead: the adapter's
 * lv_draw_sw_rgb565_swap fires ONLY for PANEL_IF_OTHER, so its
 * MIPI_DSI flushes are unswapped and the spin's direct write must be
 * too. */
#if defined(CONFIG_BOARD_P4)
static inline uint16_t spin_bswap16(uint16_t v) { return v; }
#else
static inline uint16_t spin_bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
#endif

/* The draw_bitmap wrapper (installed on the panel vtable at init):
 * LVGL's flushes (and the moon pipeline's own lv_refr_now paths)
 * reach the panel here. Under the panel mutex it refreshes the front
 * mirror for the strip it is about to overwrite (the front content
 * AFTER this write, which is what the spin's composite keeps) and
 * then calls the real draw_bitmap. The buffer arrives in PANEL byte
 * order: the adapter bridge (ESP_LV_ADAPTER_PANEL_IF_OTHER) calls
 * lv_draw_sw_rgb565_swap on the flush buffer before draw_bitmap, so
 * the mirror stores it RAW -- no swap here, no swap in the spin's
 * step-rotate. This is what keeps the spin's direct writes and
 * LVGL's flushes serialized on ONE lock at the panel boundary. */
/* The real draw_bitmap, retried until the panel accepts the write. The P4's
 * DPI panel copies each strip with an ASYNC DMA2D (its draw semaphore is
 * held until the copy lands): back-to-back calls return
 * ESP_ERR_INVALID_STATE ("previous draw operation is not finished") and the
 * strip is DROPPED. The S3's QSPI panel writes synchronously, so the first
 * attempt always succeeds and the retry costs nothing there. Callers must
 * hold s_panel_mtx. */
static esp_err_t moon_panel_draw_wait(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                      const void *color_data) {
    for (int attempt = 0; attempt < 100; attempt++) {
        esp_err_t r = s_orig_draw(panel, x_start, y_start, x_end, y_end, color_data);
        if (r == ESP_OK)
            return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "panel draw not accepted after ~100 ms (y %d..%d)", y_start, y_end);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t moon_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                        const void *color_data) {
    (void)panel;
    xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
    if (s_front && x_end > MOON_IMG_POS && x_start < MOON_IMG_POS + MOON_PX && y_end > MOON_IMG_POS &&
        y_start < MOON_IMG_POS + MOON_PX) {
        const int x0 = x_start > MOON_IMG_POS ? x_start : MOON_IMG_POS;
        const int x1 = x_end < MOON_IMG_POS + MOON_PX ? x_end : MOON_IMG_POS + MOON_PX;
        const int y0 = y_start > MOON_IMG_POS ? y_start : MOON_IMG_POS;
        const int y1 = y_end < MOON_IMG_POS + MOON_PX ? y_end : MOON_IMG_POS + MOON_PX;
        const uint16_t *src = (const uint16_t *)color_data;
        for (int y = y0; y < y1; y++) {
            const int xw = x1 - x0;
            const int sx = x0 - x_start;
            uint16_t *dst = s_front + (size_t)(y - MOON_IMG_POS) * MOON_PX + (x0 - MOON_IMG_POS);
            const uint16_t *srow = src + (size_t)(y - y_start) * (x_end - x_start) + sx;
            memcpy(dst, srow, (size_t)xw * sizeof(uint16_t));
        }
    }
    esp_err_t r = moon_panel_draw_wait(panel, x_start, y_start, x_end, y_end, color_data);
    xSemaphoreGive(s_panel_mtx);
    return r;
}

/* Synodic wake sweep state (see the sweep section below). The task
 * (core 1) bakes whole frames off screen and publishes them one by one;
 * the tick (core 0) displays each as one atomic whole frame and paces
 * the task (backpressure) so the month unfolds at the bake rate. */
static volatile bool s_sweep_baking;    /* bake task running */
static volatile bool s_sweep_abort;     /* stop the running sweep */
static volatile uint32_t s_sweep_pub;   /* frames fully baked (0..s_sweep_frames) */
static volatile uint32_t s_sweep_shown; /* frames the tick has displayed */
static volatile int32_t s_sweep_frames; /* this sweep's total frame count */
static int64_t s_sweep_now;
static TaskHandle_t s_sweep_task;
static volatile bool s_frame_stale; /* s_frame/s_rgb left mid-bake by an
                                     * abort; a completed bake clears it.
                                     * The sleep park must not rotate a
                                     * half-baked frame onto the panel. */

/* Light-sleep "dusk" state (see the dusk section below). The task (core 1)
 * bakes whole frames off screen and publishes them one by one; the tick
 * (core 0) displays each as one atomic whole frame and arms the
 * deep-sleep RTC countdown when the LAST frame lands. */
static volatile bool s_dusk_baking;    /* bake task running */
static volatile bool s_dusk_abort;     /* stop the running dusk */
static volatile uint32_t s_dusk_pub;   /* frames fully baked (0..s_dusk_frames) */
static volatile uint32_t s_dusk_shown; /* frames the tick has displayed */
static volatile int32_t s_dusk_frames; /* this dusk's total frame count */
static int64_t s_dusk_epoch;           /* the parked frame's phase epoch */
static float s_dusk_theta;             /* the parked in-plane spin angle */
static int32_t s_dusk_orbit;           /* the parked orbit (peek) angle, deg */
static volatile bool s_dusk_done;      /* the dark-side frame is on screen and
                                        * s_frame holds the pre-baked static
                                        * phase: a wake must show the current
                                        * phase at the parked pose, atomically
                                        * (the wake display clears it) */
static volatile bool s_dusk_on_screen; /* an interrupted dusk left one of its
                                        * intermediate frames in s_rgb: the next
                                        * wake must show the parked pose
                                        * atomically (the wake display clears it) */
static TaskHandle_t s_dusk_task;

/* The LVGL task's handle, cached on the first tilt-monitor tick (which
 * runs on the LVGL task). The synchronous atomic displays (a whole
 * 466x466 lv_refr_now) need the 8 KB LVGL task stack; the boot path
 * (app_main / task main) has only a 3.5 KB stack and a full render
 * there overflows it -- the boot loadScreen and the boot day-tick
 * re-bake both land on that small stack. So the sync flush runs only
 * when we are on the LVGL task; off it we invalidate and let the LVGL
 * task's async refresh paint it (a crawl is acceptable at boot, before
 * the moon matters). */
static TaskHandle_t s_lvgl_task;
static bool moon_on_lvgl(void) { return s_lvgl_task && s_lvgl_task == xTaskGetCurrentTaskHandle(); }

static void moon_invalidate_all(void) {
    for (int i = 0; i < s_n_images; i++)
        lv_obj_invalidate(s_images[i]);
}

/* Static (angle-0) display of the baked frame: the 8-bit bake maps
 * straight to the RGB565 buffer through the LUT -- no gather. Used for
 * the first display and after a re-bake (day rollover / wake); the spin
 * re-anchors at angle 0 right after, so the first spin frame matches
 * what the LUT just showed. Under the buffer mutex like every other
 * s_rgb writer, so it cannot race the spin render's gather. The fresh
 * frame is DISPLAYED here, atomically (one whole-panel flush): the
 * invalidate alone would leave it to LVGL's async refresh, which drains
 * the 400-row image in 25-row QSPI strips -- a visible top-to-bottom
 * crawl of every re-bake. */
static void moon_convert_to_rgb(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
    /* Zero the free spin inside the critical section: the spin render
     * integrates its angle under this mutex, so the next frame is the
     * identity gather of the fresh frame -- exactly what the LUT just
     * displayed (no orientation jump after a re-bake). The on-screen
     * frame is now the unrotated one, so the tracked orientation is 0
     * too. The spin RATE is untouched (a settled coast keeps coasting
     * across a re-bake). */
    s_spin_theta = 0.0;
    s_spin_last_rad = 0.0f;
    moon_invalidate_all();
    /* Atomic flush only on the LVGL task (8 KB stack). This runs on the
     * 3.5 KB main task at boot (moon_ui_start -> moon_bake_epoch), where
     * a synchronous whole-panel lv_refr_now overflows the stack; off the
     * LVGL task the invalidated frame is painted by the LVGL task's
     * async refresh instead (the pre-atomic behavior, safe at boot).
     * The flush is inlined (not a helper) because we already hold
     * s_rgb_mtx, which is non-reentrant. */
    if (moon_on_lvgl())
        lv_refr_now(NULL);
    xSemaphoreGive(s_rgb_mtx);
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

/* In-plane fold: rotate the camera basis about the view axis E by theta
 * (right' = cos right + sin up, up' = -sin right + cos up, E unchanged).
 * The on-screen free-spin pose is a theta-rotated nearest-neighbor GATHER
 * of the baked frame (moon_rotate), so baking at the theta-folded basis
 * reproduces the on-screen pose -- the light-sleep dusk starts from the
 * exact pose the spin parked the moon at (the seam; the host probe
 * host_sim/pole_test.c verifies the sign: the residual is the inherent
 * 1-px gather mismatch, a wrong sign misaligns the terminator by 2*theta). */
static void moon_inplane_geometry(const moon_geom_t *src, float theta, moon_geom_t *dst) {
    *dst = *src;
    float c = cosf(theta), s = sinf(theta);
    float r[3] = {src->right[0], src->right[1], src->right[2]};
    float u[3] = {src->up[0], src->up[1], src->up[2]};
    dst->right[0] = c * r[0] + s * u[0];
    dst->right[1] = c * r[1] + s * u[1];
    dst->right[2] = c * r[2] + s * u[2];
    dst->up[0] = -s * r[0] + c * u[0];
    dst->up[1] = -s * r[1] + c * u[1];
    dst->up[2] = -s * r[2] + c * u[2];
    /* E unchanged (the in-plane axis). */
}

/* Pole spin: rotate the camera basis about the body-frame pole (+z). The
 * x/y components of every basis vector mix (right -> up for +phi), the z
 * components are invariant -- the pole stays at the top of the disk, the
 * visible longitude range moves (the far side is revealed), and the light
 * source S (body frame) stays put, so the terminator sweeps the disk.
 * The light-sleep "dusk" rotates the camera here by phi_k per frame until
 * E . S is minimized (the fully unilluminated view; see the dusk section). */
static void moon_pole_geometry(const moon_geom_t *src, float phi, moon_geom_t *dst) {
    *dst = *src;
    float c = cosf(phi), s = sinf(phi);
    float a[3] = {src->right[0], src->right[1], src->right[2]};
    float b[3] = {src->up[0], src->up[1], src->up[2]};
    float e[3] = {src->E[0], src->E[1], src->E[2]};
    dst->right[0] = c * a[0] - s * a[1];
    dst->right[1] = s * a[0] + c * a[1];
    dst->right[2] = a[2];
    dst->up[0] = c * b[0] - s * b[1];
    dst->up[1] = s * b[0] + c * b[1];
    dst->up[2] = b[2];
    dst->E[0] = c * e[0] - s * e[1];
    dst->E[1] = s * e[0] + c * e[1];
    dst->E[2] = e[2];
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
    const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief(); /* perf A/B hook */
    if (!grel)
        ESP_LOGW(TAG, "no relief cache; baking relief-free");
    /* Under the buffer mutex: this re-bake replaces the WHOLE frame
     * (s_frame + the s_rgb display) while the spin render may be
     * mid-gather on the old one. The render blocks on the mutex for the
     * bake duration -- the spin pauses -- and draws the new frame at
     * the current time-based angle once it resumes (the caller
     * re-anchors). */
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    moon_bake(&g, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H, 1,
              0.0f); /* dark_zero: the computed (backlit) night side; dk_sup 0.0f = the plain d < 0 terminator */
    xSemaphoreGive(s_rgb_mtx);
    s_frame_stale = false; /* a complete, consistent frame is in s_frame */
    s_dusk_done = false;   /* a fresh bake supersedes any dusk pre-bake */
    int64_t t2 = esp_timer_get_time();
    /* (The relief-free vs with-DEM split that ran here while diagnosing
     * the old ~2.3 s bake settled the question: the per-pixel 4-neighbor
     * DEM gather was the dominant cost, ~1.2 s of the ~1.9 s frame at
     * 2048x1024. It is now precomputed once into the relief gradient
     * cache (moon_relief / moon_relief_build) -- the bake reads ONE
     * packed 16-bit cache cell per pixel; the bake is ~1.3 s on this
     * board.) */
    ESP_LOGI(TAG, "bake | phase %.1f%% illum, subsolar (%.1f, %.1f) | geo %lldms bake %lldms", g.illum * 100.0f,
             g.subsolar_lon, g.subsolar_lat, (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000));
    moon_convert_to_rgb();
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "bake | rgb565 %lldms (total %lldms)", (long long)((t3 - t2) / 1000), (long long)((t3 - t0) / 1000));
}

/* ---------------- spin (free, phase-anchored) ----------------
 *
 * The current phase is baked ONCE (moon_bake_epoch, ~1.3 s with the
 * relief gradient cache; the hourly/dayly re-bake is the ONLY
 * re-calculation) and then the full 400x400 frame is rotated in place
 * by a FREE SPIN: a spin angle s_spin_theta (rad, unbounded)
 * integrated per frame (theta += omega*dt), so the on-screen pose is
 * the baked phase plus the accumulated free rotation. The rate
 * s_spin_omega (rad/s) is smoothed exponentially (time constant
 * ~SETTLE/3) toward a target:
 *   - ACTIVE (the device was used -- touched or tilted -- within
 *     CONFIG_UI_MOON_SPIN_SETTLE_S, which the idle system's activity
 *     timer tracks): 2*pi / CONFIG_UI_MOON_SPIN_PERIOD_S, the fast
 *     ball-in-hand feel (default 300 s per full turn).
 *   - SETTLED: 2*pi / CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S -- a
 *     VISIBLE slow creep (default 600 s/turn): the burn-in
 *     protection the moon must keep doing at rest. The old
 *     phase-true coast (one turn per synodic month) moved ~1 px per
 *     29 s, the rim-motion gate skipped it entirely, and it read as
 *     a freeze; this keeps the anti-burn-in motion perceptible
 *     while still slow.
 *
 * DISPLAY: the DIRECT PANEL WRITE (moon_spin_write_panel). The render
 * composites the rotated moon with LVGL's current on-panel content and
 * pushes the 400x400 moon area straight to the panel -- ONE
 * draw_bitmap of the whole window on the S3 (a single CASET+RASET, then
 * 23,300 B chunks with CS held; the 16 per-strip writes left a visible
 * seam line at each strip boundary, 2026-11-01), 65-row DSI strips on
 * the P4 -- under ONE panel-mutex hold per frame, so each frame lands
 * atomically (no strip crawl, no visible redraw). That replaces the old display path: one LVGL
 * whole-panel lv_refr_now per spin frame (~175 ms measured -> a ~6 fps
 * cap, with ~150 ms of LVGL per-strip bookkeeping charged to the
 * touch core). The panel write itself is ~16 ms of QSPI time (40 MHz
 * quad, 320 KB/frame) and the CPU work is one ~8 ms moon_rotate
 * gather plus the ~0.5 M-pixel composite -- display-bound, not
 * compute-bound, and none of it on core 0. The frame rate is paced
 * (CONFIG_UI_MOON_SPIN_FPS, default 8): a dropped frame is a bigger
 * angular step, never a stall, so the rotation stays smooth even when
 * a frame slips.
 *
 * The composite's keep-mask: the spin writes the rotated moon
 * everywhere EXCEPT the per-pixel UI keep-mask (s_ui_mask), where
 * LVGL's own on-panel content survives. The UI cannot be separated
 * from the moon by a radius: the clock label sits centred ON the disk
 * and the indicator inside it, while the seconds-arc ring + knob live
 * OUTSIDE it (r 206-231). The mask is built (LVGL task) from the
 * analytic ring band + a full-screen lv_snapshot with the moon image
 * hidden (root is black, so every non-black pixel is UI), rotated by
 * the current orientation step, and refreshed on screen switch, on
 * the clock/alarm label change, and on an orientation step change.
 * The kept pixels are copied over from s_front, the FRONT MIRROR --
 * a copy of the moon area in panel byte order, kept fresh by
 * moon_panel_draw_bitmap, the wrapper installed on the panel's
 * draw_bitmap vtable: every LVGL flush (which reaches the panel only
 * through that vtable) refreshes the mirror for the strip it writes
 * and calls the real draw_bitmap, all under s_panel_mtx, so spin
 * frames and LVGL flushes serialize on one lock at the panel
 * boundary and never interleave mid-write.
 *
 *   render (dedicated task, pinned to core 1 -- the LVGL task is
 *   pinned to core 0, see the BSP):
 *     poll the pause flags (orbit/sweep/dusk bakes own the buffer)
 *     integrate the free spin (theta += omega*dt, omega eased toward
 *     active/settled) under the buffer mutex
 *     moon_rotate(s_frame -> s_rgb) at full MOON_PX under the buffer
 *     mutex, then moon_spin_write_panel() (step-rotate into s_disp,
 *     overlay-composite from s_front, 16 panel strips), then pace to
 *     1000/UI_MOON_SPIN_FPS ms
 *
 * The orientation pipeline's whole-screen 90-degree steps (the root
 * transform_rotation, see actions.c) must be applied to the direct
 * write too: moon_ui_set_panel_step publishes the current step and
 * spin_step_rotate pre-rotates s_rgb by it, so the panel shows the
 * same pixels LVGL's own transform would.
 *
 * While a bake pipeline owns the buffer (the orbit's re-bake + row
 * sweep, the boot sweep, the dusk) the render is paused (not
 * stopped): it blocks on the pipeline flags, and the tick re-anchors
 * the angle at the end -- the pipeline's last frame is the unrotated
 * one, which IS the angle-0 frame, so the spin picks up seamlessly at
 * the time-based angle.
 */
/* Forward decl: the direct panel write (defined after the render task
 * that calls it -- see the spin composite section). */
static void moon_spin_write_panel(void);
static void moon_spin_task(void *arg) {
    (void)arg;
    for (;;) {
        /* Paused (not stopped) while the orbit or the wake sweep owns
         * the buffer: their bakes rewrite s_frame (row by row, off
         * screen) and the tick then displays s_rgb as one atomic whole
         * frame (the spin's display). The tick re-anchors the angle at
         * the end of each. */
        /* Not paused for a screen-switch fade: the fade dims the
         * panel's OWN brightness (no pixels written), so the spin's
         * direct writes keep landing and the moon keeps moving while
         * the screen dims -- exactly the motion the fade-out should
         * show. */
        while (!s_spin_cancel && (s_orbit_baking || s_orbit_sweeping || s_sweep_baking || s_dusk_baking))
            vTaskDelay(pdMS_TO_TICKS(50));
        if (s_spin_cancel)
            break;
        /* The free spin (see the section header) is integrated UNDER
         * the buffer mutex: theta += omega * dt, dt = the wall time
         * since the last frame (clamped, so a resume from a pause --
         * a sweep hold, a sleep/wake -- cannot step a whole turn).
         * omega is smoothed toward its target: ACTIVE (the device was
         * touched or tilted within the settle window -- the idle
         * system's activity timer tracks both) = the fast ball-in-hand
         * rate, else the SLOW SETTLED rate (one turn per
         * UI_MOON_SPIN_SETTLED_PERIOD_S -- the burn-in creep the moon
         * must keep doing at rest; the old phase-true coast was
         * effectively invisible and read as a freeze). A re-bake
         * zeroes theta inside its own critical section, so the render
         * can never mix a new frame with a stale pre-re-bake angle
         * (an orientation jump). dt << tau per frame, so the
         * exponential approach is a linear step. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time(); /* at render time, post-wait */
        double dt = (double)(t0 - s_spin_last_ms) / 1e6;
        s_spin_last_ms = t0;
        if (dt < 0.0)
            dt = 0.0;
        else if (dt > 0.5)
            dt = 0.5; /* a resume: step at most half a second */
        /* "In use" = a touch or tilt within the settle window. The idle
         * system's activity timer tracks both; note its getter returns
         * ELAPSED ms since the last activity (small when recently used),
         * not an absolute timestamp. */
        bool active = clock_idle_last_activity_ms() < (uint64_t)CONFIG_UI_MOON_SPIN_SETTLE_S * 1000ULL;
        double w_target = active ? (6.283185307179586 / (double)CONFIG_UI_MOON_SPIN_PERIOD_S)
                                 : (6.283185307179586 / (double)CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S);
        double alpha = 1.0 - exp(-(dt * 3.0) / (double)CONFIG_UI_MOON_SPIN_SETTLE_S); /* tau ~ SETTLE/3 */
        s_spin_omega += (w_target - s_spin_omega) * alpha;
        s_spin_theta += s_spin_omega * dt;
        float rad = (float)fmod(s_spin_theta, 6.283185307179586);
        /* Rotate the BAKED phase (full MOON_PX render, ~8 ms gather)
         * into the shared frame, then show it: the DIRECT PANEL WRITE
         * (moon_spin_write_panel) composites the frame with LVGL's
         * on-panel content (the front mirror) and pushes the moon area
         * straight to the panel (16 cheap QSPI RAMWR strips under the
         * panel mutex) -- no LVGL whole-panel re-render, so the touch
         * core (core 0) is not charged per spin frame. The time-based
         * angle keeps the angular speed exact: a dropped frame is a
         * bigger step, never a repeat or a stall. */
        moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2,
                    moon_cos15(rad), moon_sin15(rad));
        s_spin_last_rad = rad; /* the on-screen orientation once the
                                * panel write finishes; the wake path
                                * resumes the spin from exactly this
                                * angle. */
        xSemaphoreGive(s_rgb_mtx);
        /* The screen-switch fade's 0x51 ramp (s_fade_busy) is the only
         * panel-io traffic that may run: the spin integrates its angle
         * (above) but SKIPS the panel write, so a 0x51 is never queued
         * behind one of the spin's in-flight strip DMAs (a lost DMA
         * completion parks the whole bus lock forever -- the freeze
         * this project hit, and no interleaving of that class can
         * happen while the ramp is owed). The angle keeps running, so
         * the first frame after the switch lands from the exact pose.
         * (The angle integration runs every frame; the dt clamp keeps
         * a long pause a bounded step, not a turn.) */
        if (!s_fade_busy)
            moon_spin_write_panel();
        s_spin_deg = (int)(rad * 57.29577951308232f + 0.5f);
        /* Pace to the configured frame rate (UI_MOON_SPIN_FPS): the
         * frame budget is gather ~8 ms + panel write ~30-40 ms of
         * QSPI time (async, non-blocking for the CPU) -- a 60 ms frame
         * at 8 fps leaves the render task idle between frames, and the
         * panel's own SPI bus lock (inside the write) is the true
         * serialization with LVGL's flushes. */
        int64_t frame_ms = 1000 / (int64_t)CONFIG_UI_MOON_SPIN_FPS;
        int64_t elapsed = (esp_timer_get_time() - t0) / 1000;
        if (elapsed < frame_ms)
            vTaskDelay(pdMS_TO_TICKS(frame_ms - elapsed));
    }
    s_spin_running = false;
    s_spin_task = NULL;
    vTaskDelete(NULL); /* a FreeRTOS task must never return */
}

/* Zero the free spin angle at this instant, under the buffer mutex
 * (the render integrates theta under the same mutex, so it cannot see
 * the pre-zero value): the on-screen frame is the unrotated one (a
 * fresh bake, an orbit sweep, the wake sweep's last frame), so the
 * spin resumes from the identity pose. The spin RATE survives (a
 * settled coast keeps coasting across the re-bake). Called before the
 * caller clears the flag the render polls on (s_sweep_baking /
 * s_orbit_sweeping), so the render cannot integrate against the old
 * theta in the gap. */
static void moon_spin_reanchor(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    s_spin_theta = 0.0;
    s_spin_last_rad = 0.0f;
    xSemaphoreGive(s_rgb_mtx);
}

/* Rotate src (n x n row-major, y-down) into dst by `step` clockwise
 * 90-degree steps (the orientation pipeline's steps; 0 = copy). The
 * screen-frame buffer s_rgb is rotated into the panel frame before the
 * spin's direct panel write, so the write shows the same pixels LVGL's
 * own transform would (the panel holds the content rotated by the
 * root's transform_rotation; the base +90 deg mount offset is step 1).
 * No byte order work here: s_rgb is LVGL-native RGB565, the front
 * mirror is PANEL order (the adapter pre-swaps, see the wrapper), and
 * the panel write needs the moon in PANEL order too -- so the rotated
 * moon copy is byte-swapped on the way, and the kept (front-mirror)
 * pixels are already in the right order.
 *   90 CW:  O[i][j] = M[n-1-j][i]
 *   180:    O[i][j] = M[n-1-i][n-1-j]
 *   270 CW: O[i][j] = M[j][n-1-i]
 */
static void spin_step_rotate(const uint16_t *m, uint16_t *o, int n, int step) {
    switch (step) {
    case 1:
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                o[(size_t)i * n + j] = spin_bswap16(m[(size_t)(n - 1 - j) * n + i]);
        break;
    case 2:
        for (int i = 0; i < n; i++) {
            const uint16_t *sr = m + (size_t)(n - 1 - i) * n;
            uint16_t *or = o + (size_t)i * n;
            for (int j = 0; j < n; j++)
                or[j] = spin_bswap16(sr[n - 1 - j]);
        }
        break;
    case 3:
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                o[(size_t)i * n + j] = spin_bswap16(m[(size_t)j * n + (n - 1 - i)]);
        break;
    default:
        for (size_t p = 0; p < (size_t)n * n; p++)
            o[p] = spin_bswap16(m[p]);
        break;
    }
}

/* The static part of the UI keep-mask: the seconds-arc band (the ring
 * + the moving knob), analytic from the measured geometry (ring
 * r 211-222, knob r 206-229, both centred on the panel centre) with a
 * few px of margin: r in [204, 231]. The band covers the knob at every
 * second, so the only thing that must be refreshed is the TEXT (the
 * label bboxes change shape with the digits). Screen-local
 * coordinates (before the orientation rotation). The P4 values are the
 * same band scaled by 720/466 (~1.545), landing on the upscaled home
 * seconds arc (M4): the ring spans r 329-344 (UI_PX(213)-UI_PX(223),
 * the 10 px pad from the panel edge) and the knob reaches r ~350,
 * both inside [326, 357]. RMIN sits just OUTSIDE the 650 px moon edge
 * (r 325): a band pixel is kept from the front mirror (the on-panel UI),
 * not the rotated moon, so the band must never overlap the disk. */
#if defined(CONFIG_BOARD_P4)
#define SPIN_MASK_RING_RMIN 326
#define SPIN_MASK_RING_RMAX 357
#else
#define SPIN_MASK_RING_RMIN 204
#define SPIN_MASK_RING_RMAX 231
#endif
static void spin_mask_band(void) {
    memset(s_ui_mask, 0, (size_t)MOON_PX * MOON_PX);
    for (int y = 0; y < MOON_PX; y++) {
        int cy = y - MOON_PX / 2; /* the moon area is centred on the panel */
        for (int x = 0; x < MOON_PX; x++) {
            int cx = x - MOON_PX / 2;
            int d2 = cx * cx + cy * cy;
            if (d2 >= SPIN_MASK_RING_RMIN * SPIN_MASK_RING_RMIN && d2 <= SPIN_MASK_RING_RMAX * SPIN_MASK_RING_RMAX)
                s_ui_mask[(size_t)y * MOON_PX + x] = 1;
        }
    }
}

/* Rebuild the UI keep-mask for the active screen: the analytic ring
 * band (static) + the screen's own UI pixels, captured by a
 * full-screen lv_snapshot with the moon image HIDDEN (it points at
 * s_rgb -- the spin's own source -- so its disk would poison the mask;
 * hiding it leaves the black root + the text + the arc). The snapshot
 * renders in the screen's LOCAL coordinates (lv_snapshot ignores the
 * root's LVGL transform -- the same fact the old reveal's in-place
 * rotation compensated), so every non-black pixel (the text, the
 * icons, the arc) is rotated by the current orientation step into the
 * panel frame before its mask bit is set. Called on the LVGL task at
 * screen switch, on the clock/alarm label change (the only content
 * whose shape changes while a moon screen is active), and on an
 * orientation step change (s_mask_dirty, picked up by the tilt
 * monitor). The step can change from another task (the app_main
 * base-orientation push) while the build runs: the mask must match
 * the step it was built with, so the build re-checks the step and
 * retries if it moved (a step change is a 1 s-sustained tilt event,
 * so one retry suffices in practice; the cap bounds it). */
static void spin_mask_rebuild_once(lv_obj_t *screen, int step);
static void spin_mask_rebuild(void) {
    if (!s_ui_mask)
        return;
    lv_obj_t *screen = lv_screen_active();
    if (!screen)
        return;
    for (int tries = 0; tries < 4; tries++) {
        const int step0 = s_panel_step;
        spin_mask_rebuild_once(screen, step0);
        if (s_panel_step == step0) {
            s_mask_dirty = false;
            return; /* the mask matches its step */
        }
    }
    /* The step moved while every build ran: the mask is stale for the
     * current step. Leave s_mask_dirty -- the next monitor tick
     * rebuilds (a step change needs 1 s of sustained tilt, so this is
     * the rare-of-rare case). */
}

/* One mask build at the given step (see spin_mask_rebuild). */
static void spin_mask_rebuild_once(lv_obj_t *screen, int step) {
    /* Hide the active moon image for the snapshot (restore on every
     * path -- the snapshot is synchronous, no yield in between). */
    lv_obj_t *img = NULL;
    for (int i = 0; i < s_n_images; i++)
        if (s_moon_roots[i] == screen)
            img = s_images[i];
    if (img)
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    spin_mask_band();
    lv_draw_buf_t *shot = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    if (img)
        lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
    if (shot) {
        const int32_t w = (int32_t)shot->header.w, h = (int32_t)shot->header.h;
        const uint32_t stride = shot->header.stride;
        /* Full-screen 16bpp, screen == the panel-sized root at (0,0):
         * snapshot pixel (sx,sy) IS screen coord (sx,sy). Anything
         * else (a non-full-screen root, a padded stride) keeps the
         * band-only mask (the text goes unkept for one rebuild --
         * the next switch fixes it). */
        if (w == (int32_t)BSP_LCD_H_RES && h == (int32_t)BSP_LCD_V_RES && stride == (uint32_t)BSP_LCD_H_RES * 2 &&
            shot->data) {
            for (int sy = 0; sy < h; sy++) {
                const uint16_t *srow = (const uint16_t *)(shot->data + (size_t)sy * stride);
                for (int sx = 0; sx < w; sx++) {
                    if (srow[sx] == 0)
                        continue; /* the black root (the labels' transparent
                                   * background snapshots to black too) */
                    const int mx = sx - MOON_IMG_POS, my = sy - MOON_IMG_POS;
                    if (mx < 0 || my < 0 || mx >= MOON_PX || my >= MOON_PX)
                        continue; /* outside the moon area the spin never writes */
                    /* Rotate (mx,my) -> (ox,oy) by the orientation step
                     * (the inverse mapping of spin_step_rotate: 90 CW
                     * output row = source column, column = n-1-row). */
                    int ox, oy;
                    switch (step) {
                    case 1:
                        ox = MOON_PX - 1 - my;
                        oy = mx;
                        break;
                    case 2:
                        ox = MOON_PX - 1 - mx;
                        oy = MOON_PX - 1 - my;
                        break;
                    case 3:
                        ox = my;
                        oy = MOON_PX - 1 - mx;
                        break;
                    default:
                        ox = mx;
                        oy = my;
                        break;
                    }
                    s_ui_mask[(size_t)oy * MOON_PX + ox] = 1;
                }
            }
        }
        lv_draw_buf_destroy(shot);
    }
}

/* Write the current s_rgb (screen frame) to the panel as the spin's
 * frame: step-rotate it into s_disp (the panel frame), overwrite the
 * UI keep-mask pixels from the front mirror (LVGL's labels/icons/arc --
 * the clock label sits INSIDE the disk, so a radius rule cannot keep
 * it: the mask is per-pixel, see the state block), and push the 16
 * strips straight to the panel under the panel mutex (the real
 * draw_bitmap, not the wrapper -- the mutex is held for the whole
 * composite + write so the front mirror is stable and the frame lands
 * atomically). The spin's frame and LVGL's flushes both derive the
 * moon pixels from s_rgb, so a full LVGL flush interleaved between
 * spin frames shows the same moon (sub-pixel rotation apart) -- no
 * visible seam. */
static void moon_spin_write_panel(void) {
    if (!s_panel || !s_disp || !s_front || !s_ui_mask)
        return;
    if (CONFIG_TEST_SPIN_BANDS >= 2) {
        /* Seam diagnostic variant 2: the SAME 16-band pattern (25-row
         * pitch, the middle 400 rows; the outer 33 rows and 33 columns
         * black) written as FULL-PANEL-WIDTH strips (x 0..465, 25 rows
         * each, 466 px/row) -- the exact geometry of the LVGL flushes.
         * Isolating the 400-wide, x=33 strip window (variant 1) as the
         * seam-line cause: if the lines appear only on variant 1 the
         * window is the trigger; if they appear here too, it is the
         * strip write in general (RASET windows / GRAM scan interplay).
         * The band canvas is static -- filled once, then rewritten every
         * frame through the same write path as the moon. The black edge
         * columns cover the arc-ring slivers at the mid-height edges for
         * the duration of the diagnostic (the production fix, if the
         * window is confirmed, keeps the full-width edge content). */
        static uint16_t *s_bands;
        if (!s_bands) {
            s_bands = heap_caps_malloc((size_t)BSP_LCD_H_RES * BSP_LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
            if (s_bands) {
                for (int y = 0; y < BSP_LCD_H_RES; y++) {
                    const bool in_moon = (y >= MOON_IMG_POS) && (y < MOON_IMG_POS + MOON_PX);
                    const int b = in_moon ? (y - MOON_IMG_POS) / MOON_SPIN_STRIP_ROWS : 0;
                    const int v5 = (b + 1) * 31 / MOON_SPIN_NSTRIPS;
                    const int v6 = (b + 1) * 63 / MOON_SPIN_NSTRIPS;
                    const uint16_t gray = in_moon ? (uint16_t)((v5 << 11) | (v6 << 5) | v5) : 0;
                    for (int x = 0; x < BSP_LCD_H_RES; x++)
                        s_bands[(size_t)y * BSP_LCD_H_RES + x] =
                            (x >= MOON_IMG_POS && x < MOON_IMG_POS + MOON_PX) ? gray : 0;
                }
            }
        }
        if (s_bands) {
            xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
            for (int y0 = 0; y0 < BSP_LCD_H_RES; y0 += MOON_SPIN_STRIP_ROWS) {
                const int y1 = y0 + MOON_SPIN_STRIP_ROWS < BSP_LCD_H_RES ? y0 + MOON_SPIN_STRIP_ROWS : BSP_LCD_H_RES;
                (void)moon_panel_draw_wait(s_panel, 0, y0, BSP_LCD_H_RES, y1, s_bands + (size_t)y0 * BSP_LCD_H_RES);
            }
            xSemaphoreGive(s_panel_mtx);
            return;
        }
        /* PSRAM unavailable: fall through to the normal path. */
    }
    if (CONFIG_TEST_SPIN_BANDS > 0) {
        /* Seam diagnostic (TEST_SPIN_BANDS): one distinct gray per strip --
         * the 16 strips of the moon area become 16 horizontal bands,
         * darkest to brightest, so a glitch at a strip seam (a stray
         * line, a shifted boundary, a row that does not land) shows on a
         * clean pattern with no moon texture or mask to confound it. The
         * write below (same strips, same geometry, same panel mutex) is
         * unchanged; only the content differs. */
        for (int s = 0; s < MOON_SPIN_NSTRIPS; s++) {
            const int v5 = ((s + 1) * 31) / MOON_SPIN_NSTRIPS; /* 5-bit R/B (0-31) */
            const int v6 = ((s + 1) * 63) / MOON_SPIN_NSTRIPS; /* 6-bit G (0-63) */
            const uint16_t gray = (uint16_t)((v5 << 11) | (v6 << 5) | v5);
            for (int y = 0; y < MOON_SPIN_STRIP_ROWS; y++)
                for (int x = 0; x < MOON_PX; x++)
                    s_disp[(size_t)(s * MOON_SPIN_STRIP_ROWS + y) * MOON_PX + x] = gray;
        }
    } else {
        const int step = s_panel_step;
        spin_step_rotate(s_rgb, s_disp, MOON_PX, step);
    }
    xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
    if (CONFIG_TEST_SPIN_BANDS == 0) {
        for (int y = 0; y < MOON_PX; y++) {
            uint16_t *row = s_disp + (size_t)y * MOON_PX;
            const uint16_t *frow = s_front + (size_t)y * MOON_PX;
            const uint8_t *mrow = s_ui_mask + (size_t)y * MOON_PX;
            for (int x = 0; x < MOON_PX; x++)
                if (mrow[x])
                    row[x] = frow[x];
        }
    }
#if defined(CONFIG_BOARD_P4) || CONFIG_TEST_SPIN_BANDS > 0
    /* P4: 65-row DSI strips -- a single 650-row write would underrun the DPI
     * FIFO (X16 + XIP PSRAM, measured). TEST_SPIN_BANDS keeps the per-strip
     * writes too: the seam diagnostic needs the per-strip CASET/RASET
     * re-latch to observe. */
    for (int s = 0; s < MOON_SPIN_NSTRIPS; s++) {
        (void)moon_panel_draw_wait(s_panel, MOON_IMG_POS, MOON_IMG_POS + s * MOON_SPIN_STRIP_ROWS,
                                   MOON_IMG_POS + MOON_PX, MOON_IMG_POS + (s + 1) * MOON_SPIN_STRIP_ROWS,
                                   s_disp + (size_t)s * MOON_SPIN_STRIP_ROWS * MOON_PX);
    }
#else
    /* S3: ONE continuous write of the whole 400x400 window. The io driver
     * issues a single CASET+RASET + 0x2C and streams the 320 KB with CS
     * held active between 466x25x2 = 23,300 B chunks -- the bus
     * max_transfer_sz is set to exactly that size in the vendored BSP,
     * because the S3's largest DMA-capable internal-RAM block is ~31 KiB
     * (the io driver's default 32 KiB chunking NO_MEM'd the first chunk of
     * this write). The 16-strip variant (one CASET/RASET re-latch + CS
     * toggle per strip) left a visible seam line at every 25-row strip
     * boundary (2026-11-01). */
    (void)moon_panel_draw_wait(s_panel, MOON_IMG_POS, MOON_IMG_POS, MOON_IMG_POS + MOON_PX, MOON_IMG_POS + MOON_PX,
                               s_disp);
#endif
    xSemaphoreGive(s_panel_mtx);
}

/* Start the render task (a previous one, if any, is already joined by
 * the caller -- moon_spin_stop). No re-anchoring: the free spin's
 * state (theta, omega, the tracked on-screen orientation) persists
 * across the pause, so the first frame after a sleep/wake resumes
 * from the exact pose the moon was showing (the render clamps its
 * first dt to 0.5 s, so the pause itself adds at most half a
 * second of spin). */
static void moon_spin_begin(void) {
    if (!CONFIG_UI_MOON_SPIN_ENABLE || s_spin_running)
        return;
    s_spin_cancel = false;
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
    ESP_LOGI(TAG, "spin started (active %d s/turn, settled %d s/turn, %d fps)", CONFIG_UI_MOON_SPIN_PERIOD_S,
             CONFIG_UI_MOON_SPIN_SETTLED_PERIOD_S, CONFIG_UI_MOON_SPIN_FPS);
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

/* Sleep entry: the spin task is already joined by the caller, so the
 * free spin would otherwise freeze wherever its last frame landed.
 * Park it: advance theta by the spin rate over the pause gap (the
 * wall time since the last frame, clamped to 0.5 s like the render's
 * resume step) and display that frame atomically -- the moon rests at
 * the exact pose the spin was heading to (sleep is the preparation for
 * the full stop). theta/s_omega carry into the wake: the spin resumes
 * from exactly this pose and keeps the rate it had (a settled coast
 * coasts on). */
static void moon_park(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    double dt = (double)(t0 - s_spin_last_ms) / 1e6;
    if (dt < 0.0)
        dt = 0.0;
    else if (dt > 0.5)
        dt = 0.5;
    s_spin_last_ms = t0;
    s_spin_theta += s_spin_omega * dt;
    float rad = (float)fmod(s_spin_theta, 6.283185307179586);
    moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, moon_cos15(rad),
                moon_sin15(rad));
    s_spin_last_rad = rad;
    xSemaphoreGive(s_rgb_mtx);
    moon_invalidate_all();
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    lv_refr_now(NULL);
    xSemaphoreGive(s_rgb_mtx);
    s_spin_deg = (int)(rad * 57.29577951308232f + 0.5f);
    ESP_LOGI(TAG, "parked at spin deg %d (exact pose)", s_spin_deg);
}

/* Forward decl: defined in the orbit section below (the orbit pipeline
 * must be cancelled before a re-bake takes over s_frame/s_rgb). */
static void moon_orbit_interrupt(void);

/* ---------------- out-of-plane orbit (sphere model) ---------------- */

/* Bake the current phase at the given orbit angle -- the WHOLE frame,
 * off screen (row by row, per-row critical sections so a cancel is
 * honored quickly). When it is done the tick displays the finished frame
 * as one atomic whole frame (MOON_SPIN_BATCH = MOON_NSTRIPS, the spin's
 * display), so the viewport change lands as one ~150 ms frame -- the
 * same sequential update as the spin -- instead of a progressive
 * top-to-bottom wipe over the ~2.4 s bake. The task owns s_frame and
 * s_rgb's row content, but never the images' validity. */
static void moon_orbit_task(void *arg) {
    (void)arg;
    int64_t epoch = s_orbit_epoch;
    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief(); /* perf A/B hook */
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; orbit bake aborted");
        s_orbit_baking = false;
        vTaskDelete(NULL);
    }
    if (!grel)
        ESP_LOGW(TAG, "no relief cache; orbit bake relief-free");
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
        moon_bake_rows(&g, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
                       MOON_ALB_H, y, y + 1, 1,
                       0.0f); /* dark_zero (T3); dk_sup 0.0f = the plain d < 0 terminator (the true orbit phase) */
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
    if (!s_orbit_cancel) {
        s_orbit_pub = MOON_PX;      /* the tick sweeps the finished frame in */
        s_orbit_baked = target_deg; /* the on-screen frame IS this viewport */
        s_frame_stale = false;      /* the frame is complete and consistent */
    } else {
        s_frame_stale = true; /* cancelled mid-bake: partial frame */
    }
    s_orbit_baking = false;
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
    /* The ownership flags BEFORE the task exists: the task may start
     * baking s_rgb rows immediately, and the spin (the pause-poll) and
     * the tick's display guard (the atomic whole-frame flush must not
     * land over a half-baked frame) only stop once the flags are up. */
    s_orbit_baking = true;
    s_orbit_sweeping = true;
    s_orbit_strip_shown = 0;
    s_orbit_pub = 0;
    if (xTaskCreatePinnedToCore(moon_orbit_task, "moon_orbit", 8192, NULL, configMAX_PRIORITIES - 3, &s_orbit_task,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "orbit task allocation failed; viewport stays at the last bake");
        s_orbit_baking = false;
        s_orbit_sweeping = false;
        return;
    }
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
    if (!CONFIG_UI_MOON_ORBIT_ENABLE || !s_started || s_orbit_epoch <= 0 || s_orbit_baking || s_orbit_sweeping ||
        s_sweep_baking || s_dusk_baking || s_dusk_done)
        return; /* the wake sweep owns the buffers; a peek can wait.
                 * s_dusk_done: the dark side is on screen and the deep-sleep
                 * countdown runs -- an orbit peek would re-bake s_frame/s_rgb
                 * and replace the dark side with the lit phase mid-countdown.
                 * Cleared on the next wake (moon_ui_pause(false)). */
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
        if (s_orbit_baking) {
            ESP_LOGW(TAG, "orbit task did not exit within 400 ms; forcing state clear");
            s_frame_stale = true; /* it may still be mid-row in the buffers */
        }
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

/* ---------------- synodic boot sweep (new moon -> now) ----------------
 *
 * The phase sweep, rebuilt on the atomic whole-frame
 * pipeline: the last synodic cycle (from the last NEW MOON to now) in
 * CONFIG_UI_MOON_ANIM_FRAMES frames, played at BOOT from the black
 * screen (moon_ui_replay_animation) -- the boot animation only; wakes
 * from sleep show the current phase static. Starting at the new moon
 * means the
 * animation always begins on a dark disk; its length is the moon's
 * current age this cycle (<= 29.53 days). A dedicated
 * bake task (core 1, like the orbit) bakes the frames OFF
 * screen and publishes them one at a time; the tick displays each
 * published frame as ONE atomic whole frame (the spin's display,
 * MOON_SPIN_BATCH = MOON_NSTRIPS strips in one lv_refr_now), one frame
 * per tick. Backpressure: the bake task does not start frame f+1 until
 * the tick has displayed frame f -- without it, the display's full-frame
 * flush could pick up the new frame's first rows over the old frame's
 * bottom (a mixed frame on screen, the wipe the old pipeline showed).
 *
 * Resolution: the intermediate frames bake at UI_MOON_ANIM_BAKE_PX
 * (default 240, a scratch s_sweep_small frame) and upscase to the full
 * 400x400 for display; the FINAL frame (the current phase the spin
 * resumes from) always bakes at full MOON_PX. The bake cost scales
 * with the on-disk pixel count, (S/400)^2 -- measured on the 336-frame
 * sweep: 200 -> ~0.21 s/frame, 240 -> ~0.28 s, 320 -> ~0.48 s, 400 ->
 * ~0.72 s, plus a fixed ~0.23 s of display+tick per frame
 * (resolution-independent), so the sweep wall is 200 -> ~2.4 min, 240
 * -> ~2.8 min (the default, chosen to stay under ~3 min), 400 ->
 * ~5.3 min (full res, montage pixel-identical to the static/spin
 * bakes). A reduced bake size makes the montage coarser (400/S x 400/S
 * NN blocks) but physically correct (the lighting/relief model is
 * resolution-independent).
 *
 * The frames are evenly spaced from the last NEW MOON (f=0, a dark
 * disk) to the CURRENT phase (f=frames-1, epoch = now); the tick
 * re-anchors the spin at the unrotated pose the last frame just showed
 * and lets it resume. The spin render is paused for the whole sweep
 * (the flag poll), the idle timer is held (the user is present for the
 * sweep), and the sleep transition aborts the sweep
 * (moon_sweep_interrupt).
 */

/* The last NEW MOON before utc_epoch: the coarse (6 h) + fine (20 min)
 * backward scan of moon_geometry's illumination over at most one
 * synodic month. Two passes because the ephemeris is a few dozen ms per
 * call and a full 20-min grid over 29.53 d would be ~2200 of them. The
 * coarse grid (6 h -> ~240 calls) brackets the minimum within 6 h; the
 * fine pass only refines the coarse minimum and its two neighbors. The
 * 1e-6 slack keeps a flat/tied coarse step from drifting the fine pass
 * to a neighboring cycle's minimum (a full month further back). Returns
 * < utc_epoch by construction; the caller clamps the span. */
static int64_t new_moon_epoch(int64_t utc_epoch) {
    moon_geom_t g;
    int64_t best = utc_epoch - (int64_t)(29.53 * 86400);
    float best_illum = 1.0f;
    for (int64_t e = utc_epoch - (int64_t)(6 * 3600); e >= utc_epoch - (int64_t)(29.53 * 86400); e -= 6 * 3600) {
        /* Abortable: this scan runs ~8 s before the frame loop (no
         * frames baked yet), and moon_sweep_interrupt joins it -- an
         * abort must be honored inside the scan, not just in the loop.
         * On abort the partial `best` is good enough: the sweep is
         * dying anyway (its frame loop exits at its first poll). */
        if (s_sweep_abort)
            return best;
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
    for (int64_t e = best + 6 * 3600; e >= best - 6 * 3600; e -= 20 * 60) {
        if (s_sweep_abort)
            return fine_best;
        g.illum = 1.0f;
        moon_geometry(e, &g);
        if (g.illum < fine_illum - 1e-6f) {
            fine_illum = g.illum;
            fine_best = e;
        }
    }
    return fine_best;
}

static void moon_sweep_task(void *arg) {
    (void)arg;
    int32_t frames = s_sweep_frames;
    int sweep_bake_px = CONFIG_UI_MOON_ANIM_BAKE_PX; /* intermediate frames' bake size */
    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief(); /* perf A/B hook */
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; sweep aborted");
        s_sweep_baking = false;
        vTaskDelete(NULL);
    }
    if (!grel)
        ESP_LOGW(TAG, "no relief cache; sweep relief-free");
    int64_t now = s_sweep_now;
    /* Sweep span: the last synodic cycle, from the last NEW MOON to now
     * (the animation always starts dark). new_moon_epoch() finds it by
     * coarse-then-fine scanning moon_geometry's illumination backwards
     * over at most one synodic month; the span is clamped to (0, 29.53 d]. */
    const int64_t SYNODIC_S = 29.53 * 86400;
    int64_t span = now - new_moon_epoch(now);
    if (span <= 0)
        span = SYNODIC_S; /* ephemeris degenerate: fall back to a full month */
    if (span > SYNODIC_S)
        span = SYNODIC_S;
    ESP_LOGI(TAG, "sweep span: %lld d (new moon %lld s before now)", (long long)(span / 86400), (long long)span);
    int64_t t0 = esp_timer_get_time();
    for (int f = 0; f < frames && !s_sweep_abort; f++) {
        /* Backpressure (see the section above): the previous frame must
         * be on screen before s_rgb may be overwritten with the next.
         * The tick displays each published frame one tick later (one
         * atomic ~140 ms lv_refr_now), while the bake is ~1.3 s, so the
         * bake paces the sweep and this wait is usually ~0; the 5 s cap
         * is a safety bail (the only tick-paused case -- sleep entry --
         * sets s_sweep_abort, which the loop polls too). */
        if (f > 0) {
            int waited = 0;
            while (!s_sweep_abort && s_sweep_shown < (uint32_t)f) {
                vTaskDelay(pdMS_TO_TICKS(10));
                waited += 10;
                if (waited > 5000)
                    break;
            }
            if (s_sweep_abort)
                break;
        }
        /* Frame f is (frames-1-f) sweep steps before the current
         * phase: f=0 is the last NEW MOON (dark disk), f=frames-1 is
         * NOW.
         *
         * Intermediate frames bake at the reduced UI_MOON_ANIM_BAKE_PX
         * resolution (the on-disk pixel count scales (S/400)^2, so the
         * per-frame cost drops with it -- the lighting/relief model is
         * resolution-independent, only the screen sampling is coarser),
         * then upscase to the full MOON_PX for display. The FINAL frame
         * is always baked at full MOON_PX: it is the current phase the
         * spin resumes from, and it is the only frame that lingers on
         * screen. With the reduction disabled (bake px >= MOON_PX) every
         * frame bakes at full resolution (the old path). */
        double t = (double)(frames - 1 - f) / (double)(frames - 1);
        int64_t epoch = now - (int64_t)(t * span);
        moon_geom_t g;
        moon_geometry(epoch, &g);
        int S = (f == frames - 1) ? MOON_PX : sweep_bake_px; /* this frame's bake size */
        if (S > MOON_PX)
            S = MOON_PX;
        uint8_t *dst = (S == MOON_PX) ? s_frame : s_sweep_small; /* non-NULL (init check) */
        /* Computed (backlit) dark side for every montage frame: the
         * unlit surface with its real relief, lit faintly from the
         * anti-solar direction (dark_zero = 1, MOON_NIGHT_BACKLIT_K) --
         * the same night side as the static/wake frame, so the sweep's
         * final frame hands over to the static phase without a dark-side
         * pop. (Replaces the old flat-black device dark side; the
         * backlit ramp is a smooth per-pixel gradient, not the
         * earthshine's ~17-step banded ramp that motivated the flat
         * floor.) */
        int dark_zero = 1;
        int64_t b0 = esp_timer_get_time();
        for (int y = 0; y < S && !s_sweep_abort; y++) {
            /* Bake into the (scratch, sweep-only) small frame unlocked:
             * nothing else reads s_sweep_small, the spin is paused for
             * the whole sweep, and the display only ever touches s_rgb
             * (converted below, under the mutex). A cancel is honored
             * within a row. */
            moon_bake_rows(&g, alb, grel, dst, S, (float)(S / 2), (float)(S / 2), (float)(S / 2), MOON_ALB_W,
                           MOON_ALB_H, y, y + 1, dark_zero,
                           0.0f); /* the sweep shows the true moving phase: dk_sup 0.0f */
            if ((y & 15) == 15)
                vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
        }
        if (s_sweep_abort)
            break;
        /* Convert + (if reduced) upscase into s_rgb as ONE locked block:
         * the display's atomic flush (under the same mutex) can never
         * mix two frames, and the small buffer is only read here. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        if (S == MOON_PX) {
            for (int y = 0; y < MOON_PX; y++) {
                const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
                uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
                for (int x = 0; x < MOON_PX; x++)
                    rrow[x] = moon_gray565(frow[x]);
            }
        } else {
            /* Nearest-neighbor (S -> MOON_PX): the pixel centers map to
             * src = (i + 0.5) * S / MOON_PX, computed as (2i+1)*S/(2*MOON_PX)
             * to keep it exact in int math (S/MOON_PX is not integer). */
            int sm[MOON_PX];
            for (int i = 0; i < MOON_PX; i++) {
                int s_ = ((2 * i + 1) * S) / (2 * MOON_PX);
                if (s_ >= S)
                    s_ = S - 1;
                sm[i] = s_;
            }
            for (int Y = 0; Y < MOON_PX; Y++) {
                const uint8_t *frow = &s_sweep_small[(size_t)sm[Y] * S];
                uint16_t *rrow = &s_rgb[(size_t)Y * MOON_PX];
                for (int x = 0; x < MOON_PX; x++)
                    rrow[x] = moon_gray565(frow[sm[x]]);
            }
        }
        xSemaphoreGive(s_rgb_mtx);
        s_sweep_pub = (uint32_t)f + 1; /* the tick displays this frame next */
        ESP_LOGI(TAG, "sweep %d/%d baked in %lldms @%dpx (illum %.1f%%)", f + 1, frames,
                 (long long)((esp_timer_get_time() - b0) / 1000), S, g.illum * 100.0f);
    }
    if (!s_sweep_abort) {
        /* Wait for the LAST frame to be on screen before exiting: the
         * tick re-anchors the spin in the same advance of s_sweep_shown,
         * and the spin render resumes only when s_sweep_baking is
         * false -- so the re-anchor happens-before the resume. The 5 s
         * bail covers the tick being paused (the only such case --
         * sleep entry -- aborts the sweep, which the loop polls). */
        int waited = 0;
        while (!s_sweep_abort && s_sweep_shown < (uint32_t)s_sweep_frames && waited < 5000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
    } else if (s_sweep_pub < (uint32_t)s_sweep_frames) {
        s_frame_stale = true; /* the buffers were left mid-bake */
    } else {
        s_frame_stale = false; /* all frames completed */
    }
    s_sweep_baking = false;
    ESP_LOGI(TAG, "sweep bake %s in %lldms", s_sweep_abort ? "interrupted" : "done",
             (long long)((esp_timer_get_time() - t0) / 1000));
    vTaskDelete(NULL);
}

/* Abort a running sweep and clear its state (sleep entry / start). The
 * task checks s_sweep_abort per row and in the backpressure poll, so it
 * exits within a row (~2 ms) of the abort. */
static void moon_sweep_interrupt(void) {
    if (s_sweep_baking) {
        s_sweep_abort = true;
        int waited = 0;
        while (s_sweep_baking && waited < 400) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (s_sweep_baking) {
            /* The task is still inside a section between abort polls
             * (the epoch scan or the 5 s backpressure bail): it WILL
             * exit at its next poll -- leaving s_sweep_abort SET means
             * it cannot resume the animation in the meantime. Resetting
             * the abort here (the old code) let a zombie task keep
             * baking 336 frames into the buffers after the caller had
             * moved on to the sleep screens. */
            ESP_LOGW(TAG, "sweep task did not exit within 400 ms; abort stays armed until it exits");
            s_frame_stale = true; /* it may still be mid-row in the buffers */
            return;               /* the zombie exits on its own and clears s_sweep_baking */
        }
    }
    s_sweep_abort = false;
    s_sweep_baking = false;
    s_sweep_pub = 0;
    s_sweep_shown = 0;
    s_sweep_frames = 0;
}

/* ---------------- light-sleep "dusk" (pole-axis spin to the dark side) ---
 *
 * The light-sleep entry indicator: the moon spins about its POLE (the
 * body-frame +z axis) at the current phase, from the exact on-screen pose
 * the free spin parked it at, until the fully UNILLUMINATED side faces the
 * viewer -- then the deep-sleep countdown is armed. Sleep depth follows
 * the animation, not a fixed timer (the deep-sleep countdown is armed on
 * the last frame's display, never at the light-sleep entry itself).
 *
 * The spin is a CONSTANT-PROJECTION re-render: the renderer's color is a
 * function of the body-frame surface direction and the epoch's fixed
 * body-frame sun S alone (the view basis selects which direction is
 * visible but never enters the color -- see the moon_bake.c section).
 * So the full body-frame equirect color map (lit side: albedo x
 * displaced-normal Lambert x tone LUT; dark side: the faint backlit
 * surface, 864x432 8-bit cells) is built ONCE for the dusk epoch and
 * every pole-spin frame RE-PROJECTS that fixed map through the folded
 * camera basis -- the
 * cheap moon_rotate-class gather (one map load/pixel, ~50 ms), NOT a
 * lighting re-bake (~1.3 s). A fixed surface point keeps exactly its map
 * cell's color on every frame, so the terminator cannot flicker (the
 * per-frame re-bake's reduced-res lighting was the flicker source).
 * The phase sweep keeps per-frame bakes: there the sun geometry changes
 * between frames. The map build itself (~1.5-2 s) is one-time at dusk
 * start; the animation is display-paced from then on.
 *
 * Each frame folds the camera basis three ways:
 *   orbit(o)    the tilt viewport of the parked frame (moon_orbit_geometry)
 *   inplane(th) the parked free-spin angle (moon_inplane_geometry): the
 *               on-screen pose is a theta-rotated gather of the static
 *               frame, so the theta-folded basis reproduces it -- the
 *               seamless entry seam (verified on the host)
 *   pole(phi)   the animation itself (moon_pole_geometry): phi advances
 *               1 degree per frame from 0 to phi*, the shortest arc to the
 *               dark side (at most a half turn).
 *
 * Endpoint: the visible disk is fully night-side when E . S is minimized.
 * With E = the orbit-folded view direction (the ACTUAL starting view --
 * the tilt peeks the view, so the endpoint is computed from the folded E,
 * not the plain one) and S the body-frame Sun direction:
 *   E(phi) . S = A cos(phi) + B sin(phi) + C,  A = E0 S0 + E1 S1,
 *               B = E0 S1 - E1 S0,  C = E2 S2
 * and phi* = atan2(-B, -A) is that minimum (host-verified against a
 * full-circle grid; the in-plane fold leaves E unchanged, so it does not
 * shift the endpoint). phi* in (-pi, pi] is the shortest arc.
 *
 * dark_zero = 1 for every dusk frame (the user's decision: the dark side
 * is COMPUTED, not the earthshine "earth ray"): the shade map's dark
 * cells and the final bake's night side are written the faint backlit
 * surface (albedo + relief lit from the anti-solar direction), so no
 * phase-dependent earthshine/relief speckle survives to the rim (the
 * "bright fragment on the edge") while the far side still shows the
 * lunar surface. The host gate keeps dark_zero = 0 there (pole_test):
 * the earthshine model itself is unchanged -- only the device-visible
 * rendering drops it.
 *
 * Resolution: intermediate frames re-project the FULL-RES map (the
 * re-projection is gather-class, so no reduced-res bake + NN upscase is
 * needed); the FINAL frame is a full MOON_PX bake at phi* (the one
 * pixel-exact dark-side lighting pass -- it lingers on the sleep screens
 * through the countdown). The animation is display-paced (~0.2 s/frame)
 * at |phi*| frames (14-95 s) -- the animation's duration is the sleep
 * indicator.
 *
 * Display: the sweep's pipeline -- the task paces on s_dusk_shown
 * (backpressure), the tick shows each published frame as ONE atomic whole
 * frame. The moon tick is resumed for the duration (the light-sleep entry
 * paused it for the parked pose; moon_ui_dusk resumes it, and the orbit
 * drain is guarded off while the dusk owns the buffers). On completion the
 * task pre-bakes the STATIC frame (epoch + orbit viewport, unrotated) into
 * s_frame, row by row so an interrupt is honored: the wake shows the
 * CURRENT phase, not the dark-side disk, and the spin resumes from exactly
 * the parked pose.
 */
/* The dusk's rim-sliver kill bound is dk_sup + this margin ("kill the
 * limb harder", 2026-11-04 bugreport): 0.12 in the cosine distance d,
 * ~7 deg of crescent edge. The lit region stays full brightness where
 * it remains; the shade map and the final bake use the same bound. */
#define MOON_DUSK_KILL_MARGIN 0.12f

static void moon_dusk_task(void *arg) {
    (void)arg;
    int frames = s_dusk_frames;
    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief(); /* perf A/B hook */
    if (!alb) {
        ESP_LOGE(TAG, "no albedo; dusk aborted");
        s_dusk_baking = false;
        vTaskDelete(NULL);
    }
    if (!grel)
        ESP_LOGW(TAG, "no relief cache; dusk relief-free");
    moon_geom_t g0;
    moon_geometry(s_dusk_epoch, &g0);
    /* Orbit-fold the view FIRST (the parked frame's viewport), so the
     * endpoint is computed from the actual starting E. */
    moon_geom_t g1;
    moon_orbit_geometry(&g0, (float)s_dusk_orbit * (float)M_PI / 180.0f, &g1);
    float A = g1.E[0] * g1.S[0] + g1.E[1] * g1.S[1];
    float B = g1.E[0] * g1.S[1] - g1.E[1] * g1.S[0];
    float phi_star = atan2f(-B, -A); /* the fully unilluminated view */
    float phi_end = phi_star;
    /* The endpoint's rim-sliver kill (the dusk's "white ray"): the pole
     * spin never changes the view latitude, so the endpoint view still
     * carries a geometrically LIT rim sliver -- the visible disk stays
     * d > 0 within 180 deg - (the pole's angular distance to the
     * anti-solar point) of the horizon, i.e. d up to sqrt(1 - E.S(phi*)^2).
     * Left lit it lingers on the otherwise-dark sleep screen as a bright
     * arc hugging the disk edge -- AND in every re-projected frame: the
     * map's 0 < d < dk_sup band (relief-boosted, up to gray 137) sweeps
     * the disk as a latitudinal arc while the pole spins (the "white
     * ray" the user reported on 2026-10-29). So BOTH the shade map (the
     * one-time build below) and the FINAL bake (below) write the BACKLIT
     * night side for d < dk_sup -- and since that sliver is sun-lit
     * (lns > 0), its backlit color clips to pure black, the exact old
     * flat-floor kill: the lit region the frames show is the
     * body-fixed d > dk_sup crescent -- constant (it only rotates) and
     * in agreement with the final bake at the endpoint (no pop). The
     * one step change is at frame 1 (the parked pose's true terminator
     * -> the shifted one, ~7 deg of crescent edge today): the user-
     * accepted look over the ray.
     *
     * The kill margin (the 2026-11-04 bugreport, "kill the limb harder"): the
     * bound is dk_sup + MOON_DUSK_KILL_MARGIN, not the geometric dk_sup --
     * the lit crescent is THINNER by 0.12 (~7 deg in the cosine distance d),
     * still full brightness where it remains, and the map + the final bake
     * agree on the same bound (no pop at the endpoint). The bound is clamped
     * < 1.0: at dk_sup = 1 (the parked pose 90 deg from the Sun -- the phi*
     * minimum still half-lit) a wider bound would write the backlit side
     * over the whole disk and black out the half that is still lit. */
    float e_dot_s_end = A * cosf(phi_star) + B * sinf(phi_star) + g1.E[2] * g1.S[2]; /* = E(phi*).S */
    float dk_sup = sqrtf(1.0f - e_dot_s_end * e_dot_s_end);
    float dk_kill = dk_sup + MOON_DUSK_KILL_MARGIN;
    if (dk_kill >= 1.0f)
        dk_kill = 0.999f;
    int64_t t0 = esp_timer_get_time();
    bool s_frame_touched = false;
    /* One-time: the full body-frame equirect map for this epoch (lit +
     * backlit night side; S is never folded -- the folds touch the
     * camera basis only). Row-by-row so an abort lands within a row
     * (~2 ms); s_shade_map is dusk-only (the sweep and dusk never run at
     * once, and the sweep does not use the map). */
    int64_t mb0 = esp_timer_get_time();
    for (int y = 0; y < MOON_ALB_H && !s_dusk_abort; y++) {
        /* dark_zero + dk_kill (the geometric bound + the kill margin):
         * the backlit night side for d < dk_kill kills the 0 < d <
         * dk_kill rim sliver in the MAP itself (the "white ray" on the
         * re-projected frames, see above), one step wider than the
         * geometric sliver. */
        moon_shade_map_rows(&g0, alb, grel, s_shade_map, MOON_ALB_W, MOON_ALB_H, y, y + 1, 1, dk_kill);
        if ((y & 15) == 15)
            vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
    }
    if (s_dusk_abort)
        goto abort_out; /* s_frame/s_rgb untouched: the parked pose */
    ESP_LOGI(TAG, "dusk shade map built in %lldms (%d cells)", (long long)((esp_timer_get_time() - mb0) / 1000),
             MOON_ALB_W * MOON_ALB_H);
    for (int f = 0; f < frames && !s_dusk_abort; f++) {
        /* Backpressure (the sweep's rule): the previous frame on screen
         * before s_rgb may be overwritten. The tick displays each frame
         * one tick later; the 5 s cap is the safety bail (the only
         * tick-paused case -- a wake -- sets s_dusk_abort, polled here). */
        if (f > 0) {
            int waited = 0;
            while (!s_dusk_abort && s_dusk_shown < (uint32_t)f) {
                vTaskDelay(pdMS_TO_TICKS(10));
                waited += 10;
                if (waited > 5000)
                    break;
            }
            if (s_dusk_abort)
                break;
        }
        /* Frame f+1 of frames: phi advances 1 deg/frame (the user's
         * smoothness bar) along the shortest arc to the dark side. */
        float phi = phi_end * ((float)f + 1.0f) / (float)frames;
        moon_geom_t g;
        moon_inplane_geometry(&g1, s_dusk_theta, &g);
        moon_pole_geometry(&g, phi, &g);
        s_frame_touched = true; /* every frame writes s_frame, row by row */
        int64_t b0 = esp_timer_get_time();
        if (f == frames - 1) {
            /* FINAL frame: the one pixel-exact dark-side bake at phi*
             * (full lighting, dark_zero 1 -- the backlit night side: no
             * earthshine/relief speckle at the rim, the "bright fragment
             * on the edge" the user wanted gone, yet the far side still
             * shows the faint surface). It lingers on the sleep screens
             * through the countdown. */
            for (int y = 0; y < MOON_PX && !s_dusk_abort; y++) {
                moon_bake_rows(&g, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2),
                               MOON_ALB_W, MOON_ALB_H, y, y + 1, 1,
                               dk_kill); /* kill the endpoint's lit rim sliver (the "white ray"), margin wider */
                if ((y & 15) == 15)
                    vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
            }
        } else {
            /* Intermediate frames: RE-PROJECT the constant map through
             * the folded basis (no lighting recompute, no flicker) at
             * full resolution. Row-by-row like the sweep so an abort
             * lands within a row. */
            for (int y = 0; y < MOON_PX && !s_dusk_abort; y++) {
                /* Pure gather: the map already carries the dark side
                 * (backlit, with the dk_sup + margin erosion) -- no bound. */
                moon_reproject_rows(s_shade_map, MOON_ALB_W, MOON_ALB_H, &g, s_frame, MOON_PX, MOON_R,
                                    (float)(MOON_PX / 2), (float)(MOON_PX / 2), y, y + 1);
                if ((y & 15) == 15)
                    vTaskDelay(pdMS_TO_TICKS(1)); /* feed the idle task + TWDT */
            }
        }
        if (s_dusk_abort)
            break;
        /* Convert into s_rgb as ONE locked block (the sweep's block):
         * the tick's atomic flush can never mix two frames. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        for (int y = 0; y < MOON_PX; y++) {
            const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
            uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
            for (int x = 0; x < MOON_PX; x++)
                rrow[x] = moon_gray565(frow[x]);
        }
        xSemaphoreGive(s_rgb_mtx);
        s_dusk_pub = (uint32_t)f + 1;
        if ((f & 15) == 0 || f == frames - 1)
            ESP_LOGD(TAG, "dusk %d/%d %s in %lldms (phi %+.1f deg)", f + 1, frames,
                     f == frames - 1 ? "final dark-side bake" : "reprojected",
                     (long long)((esp_timer_get_time() - b0) / 1000), phi * 57.29577951308232f);
    }
abort_out: /* the map-build abort lands here: the else-branch below handles it
            * (s_frame_touched/s_dusk_pub are both still 0 -> no stale frame,
            * nothing dusk-published on screen) */
    if (!s_dusk_abort) {
        /* Wait for the LAST frame to be on screen before the pre-bake:
         * the tick arms the deep-sleep countdown on this same advance. */
        int waited = 0;
        while (!s_dusk_abort && s_dusk_shown < (uint32_t)s_dusk_frames && waited < 5000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (!s_dusk_abort) {
            /* Pre-bake the STATIC frame (the basis the spin gathered before
             * the dusk: epoch + orbit viewport, NO in-plane/pole fold -- the
             * spin re-applies the parked angle on wake) into s_frame, row by
             * row so an interrupt is honored quickly. It is NOT displayed:
             * the dark-side disk stays on the sleep screens; the wake
             * (moon_ui_pause(false)) displays it atomically at the parked
             * pose. s_rgb keeps the dark side the whole time. */
            s_frame_touched = true;
            for (int y = 0; y < MOON_PX && !s_dusk_abort; y++) {
                xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
                /* dark_zero 1: the wake frame's night side is the
                 * backlit computed surface too (matching the static
                 * phase). This is the CURRENT phase (not the dusk's dark
                 * endpoint), so it keeps the true d < 0 terminator:
                 * dk_sup 0.0f. */
                moon_bake_rows(&g1, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2),
                               MOON_ALB_W, MOON_ALB_H, y, y + 1, 1, 0.0f);
                xSemaphoreGive(s_rgb_mtx);
                if ((y & 15) == 15)
                    vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
    }
    if (!s_dusk_abort) {
        s_frame_stale = false; /* the pre-bake completed: a consistent static frame */
        s_dusk_done = true;    /* wake: show the current phase at the parked pose */
        ESP_LOGI(TAG,
                 "dusk done in %lldms (%d frames, %+.0f deg about the pole); countdown armed, static frame pre-baked",
                 (long long)((esp_timer_get_time() - t0) / 1000), frames, phi_star * 57.29577951308232f);
    } else {
        s_frame_stale = s_frame_touched;   /* a partial last frame / pre-bake;
                                            * an abort before either left s_frame intact */
        s_dusk_on_screen = s_dusk_pub > 0; /* s_rgb may hold an intermediate
                                            * dusk frame (zero published frames:
                                            * it still holds the parked pose) */
        ESP_LOGI(TAG, "dusk interrupted at %d/%d in %lldms", (int)s_dusk_pub, frames,
                 (long long)((esp_timer_get_time() - t0) / 1000));
    }
    s_dusk_baking = false;
    vTaskDelete(NULL);
}

/* Abort a running dusk and clear its state (wake / alarm / deep-sleep
 * transition). The task checks s_dusk_abort per row (the shade-map build,
 * the re-projection frames AND the static pre-bake) and in the
 * backpressure poll, so it exits within a row (~2 ms) of the abort -- the
 * 400 ms join is a defensive margin. */
static void moon_dusk_interrupt(void) {
    if (s_dusk_baking) {
        s_dusk_abort = true;
        int waited = 0;
        while (s_dusk_baking && waited < 400) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (s_dusk_baking) {
            ESP_LOGW(TAG, "dusk task did not exit within 400 ms; forcing state clear");
            s_frame_stale = true; /* it may still be mid-row in s_frame */
        }
    }
    s_dusk_abort = false;
    s_dusk_baking = false;
    s_dusk_pub = 0;
    s_dusk_shown = 0;
    s_dusk_frames = 0;
    /* s_dusk_done / s_dusk_on_screen are the WAKE's business: they must
     * survive this (sleep-side) call so the wake display can show the
     * parked pose atomically. The task's abort path already refreshed
     * s_dusk_on_screen; a no-op join (nothing running) leaves both as
     * they are. */
}

/* The tilt drives the moon's OUT-OF-PLANE orbit (sphere model; the
 * in-plane pose is the free spin's, see the spin section -- the tilt
 * counts as activity for its rate but does not set its angle). The
 * orbit comes from the gravity vector: angle from vertical asin(-gz)
 * (chip z is the out-of-screen axis,
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
 * Motion test: the DEPARTURE (deg) of the current pose from a
 * settled-pose reference, NOT the delta between two raw samples. The
 * raw per-sample delta (the old test) amplifies per-sample noise: on
 * this board the at-rest noise floor (1 Hz, device on a table) has
 * p90 ~3.3 deg with a long tail, and a streak of two raw deltas above
 * UI_IMU_ACTIVITY_DEG (5) still fired 3-9 s after sleep entry ("wakes
 * right after entering sleep"). The fix is a reference: s_pose_g is a
 * fast EMA of the raw gravity (the current pose, per-sample noise
 * suppressed) and s_rest_g is a SLOW EMA of it (the settled pose),
 * updated only while settled so it tracks the measured at-rest drift
 * (~0.4-0.7 deg/s on this board, faster when warm after a long bake)
 * instead of fighting it. At rest the departure therefore sits ~1.5 deg
 * or less (measured 2026-10-27), far below every threshold; only a
 * deliberate move produces a large departure. The tracking ceiling is
 * the bar x alpha / (1 - alpha) of drift: 5 deg x 0.2 / 0.8 = 1.25 deg/s
 * at the activity bar -- a first-order EMA lags a ramp by (1 - alpha) /
 * alpha of the drift velocity, so drift above the ceiling pins the
 * departure just over the bar and the device would hold its idle timer
 * forever (never sleep). The original alpha 0.1 (ceiling 0.56 deg/s)
 * was measured right at its ceiling on a hot board; 0.2 doubles the
 * margin. The wake-bar ceiling (20 x 0.2 / 0.8 = 5 deg/s) is still far
 * above any real drift.
 *
 * Two thresholds:
 *   - awake (hold the idle timer while the user manipulates the ball):
 *     UI_IMU_ACTIVITY_DEG (default 5 deg) -- a gentle manipulation
 *     clears it; a still hold settles and the reference follows, so a
 *     held-still device is free to sleep.
 *   - asleep (the wake bar): UI_IMU_WAKE_DEG (default 20 deg) -- a
 *     deliberate pick-up / tilt clears it; table vibration and the
 *     at-rest drift cannot.
 * A STREAK of two consecutive qualifying samples gates both, so a
 * single residual spike cannot fire. 1 Hz is plenty: a hand tilt that
 * should wake the device moves far more than the bar in a second, and
 * the wake is all-or-nothing so per-second granularity is fine.
 *
 * This callback lives on its own timer (not the moon tick) because the
 * moon tick is PAUSED in sleep -- the wake path must run there too. */
#ifndef CONFIG_UI_IMU_ACTIVITY_DEG
#define CONFIG_UI_IMU_ACTIVITY_DEG 5
#endif
#ifndef CONFIG_UI_IMU_WAKE_DEG
#define CONFIG_UI_IMU_WAKE_DEG 20
#endif
#define MOON_TILT_SMOOTH_ALPHA 0.5f /* fast EMA: the current pose */
#define MOON_TILT_REST_ALPHA 0.2f   /* slow EMA: the settled reference (see the ceiling note above) */
static float s_pose_g[3];           /* fast EMA of the raw gravity, unit vector */
static float s_rest_g[3];           /* slow EMA, updated only while settled, unit */
static bool s_have_g;
static int s_motion_streak; /* consecutive samples above the active bar */
static bool s_tilt_settled; /* the monitor's settled state (departure below the
                             * activity bar) -- see moon_ui_tilt_settled */

/* Normalize a gravity vector (the magnitude wanders around 1 g; the
 * departure test is an angle, so keep both EMAs unit). */
static void moon_unit3(float v[3]) {
    float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-4f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    }
}

static void moon_tilt_monitor_cb(lv_timer_t *timer) {
    (void)timer;
    /* This timer runs on the LVGL task and is never paused, so it is the
     * reliable early point to cache the LVGL task's handle (see
     * moon_on_lvgl). The boot path (app_main / task main) has a 3.5 KB
     * stack and must not run a synchronous whole-panel lv_refr_now. */
    s_lvgl_task = xTaskGetCurrentTaskHandle();
    if (s_mask_dirty)
        spin_mask_rebuild(); /* the orientation step changed (it can move
                                from the main task -- the app_main base
                                push -- so this LVGL-task tick is where
                                the re-rotated mask is built) */
    if (!CONFIG_UI_IMU_ENABLE)
        return; /* the moon ignores the IMU (orbit off too); the task
                 * handle cache above is the only reason the timer keeps
                 * running in that case */
    float dep_deg = -1.0f;
    float g[3];
    if (imu_read_gravity(g)) {
        if (!s_have_g) {
            /* First sample seeds both EMAs: zero departure. */
            s_pose_g[0] = g[0];
            s_pose_g[1] = g[1];
            s_pose_g[2] = g[2];
            s_rest_g[0] = g[0];
            s_rest_g[1] = g[1];
            s_rest_g[2] = g[2];
            s_have_g = true;
            s_tilt_settled = true; /* zero departure at the seed */
        } else {
            /* Fast EMA: the current pose, per-sample noise suppressed. */
            s_pose_g[0] += MOON_TILT_SMOOTH_ALPHA * (g[0] - s_pose_g[0]);
            s_pose_g[1] += MOON_TILT_SMOOTH_ALPHA * (g[1] - s_pose_g[1]);
            s_pose_g[2] += MOON_TILT_SMOOTH_ALPHA * (g[2] - s_pose_g[2]);
            moon_unit3(s_pose_g);
            /* Departure of the pose from the settled reference. */
            float c = s_pose_g[0] * s_rest_g[0] + s_pose_g[1] * s_rest_g[1] + s_pose_g[2] * s_rest_g[2];
            if (c < -1.0f)
                c = -1.0f;
            else if (c > 1.0f)
                c = 1.0f;
            dep_deg = acosf(c) * 57.29577951308232f;
            if (dep_deg < (float)CONFIG_UI_IMU_ACTIVITY_DEG) {
                /* Settled: the slow reference absorbs the current pose
                 * (and any slow at-rest drift), so at rest the departure
                 * stays far below both bars. */
                s_rest_g[0] += MOON_TILT_REST_ALPHA * (s_pose_g[0] - s_rest_g[0]);
                s_rest_g[1] += MOON_TILT_REST_ALPHA * (s_pose_g[1] - s_rest_g[1]);
                s_rest_g[2] += MOON_TILT_REST_ALPHA * (s_pose_g[2] - s_rest_g[2]);
                moon_unit3(s_rest_g);
                s_motion_streak = 0;
                s_tilt_settled = true; /* the clock-roll correction's "at rest" gate */
            } else if (dep_deg >= (float)CONFIG_UI_IMU_WAKE_DEG || !clock_is_idle()) {
                /* Above the bar for this state -- awake: the activity
                 * bar (holds the idle timer while the user manipulates
                 * the ball); asleep: the wake bar. Streak gate: two
                 * consecutive samples. */
                if (++s_motion_streak >= 2)
                    clock_idle_notify_tilt_activity();
                s_tilt_settled = false; /* deliberately in motion */
            } else {
                /* Asleep, between the activity and wake bars: below the
                 * wake bar -- neither wake nor hold. */
                s_motion_streak = 0;
                s_tilt_settled = false; /* moving (below the wake bar) */
            }
        }
    }
}

/* The monitor's settled state (see moon_ui_tilt_settled): the clock-roll
 * correction tracks its "at rest" reference only while this is true, so
 * at-rest drift cannot rotate the text. */
bool moon_ui_tilt_settled(void) { return s_tilt_settled; }

/* True while the synodic boot sweep's bake task is running (the on-demand
 * sync flow waits for it before switching the WiFi screen to airplane). */
bool moon_ui_sweeping(void) { return s_sweep_baking; }

/* ---------------- tick ---------------- */

static void moon_tick_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ready || !s_started)
        return;
    /* The spin no longer displays through the tick (see the spin
     * section): its render task writes the frame straight to the
     * panel (moon_spin_write_panel), so the tick has nothing to drain
     * for the spin. */
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
            show = MOON_SPIN_BATCH; /* the spin's atomic whole-frame display */
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
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
        }
        if (s_orbit_strip_shown >= MOON_NSTRIPS) {
            /* Re-anchor (under the buffer mutex) BEFORE clearing the
             * sweep flag: the render polls s_orbit_sweeping and cannot
             * compute a post-sweep angle until it is false, and it
             * reads the anchor under this same mutex -- so the re-anchor
             * happens-before the render's next angle read, and the first
             * spin frame after the orbit is the identity gather (the
             * sweep's last frame, unrotated). No orientation jump. */
            moon_spin_reanchor();
            s_orbit_sweeping = false; /* now the render may resume */
            if (!clock_is_idle())
                clock_idle_set_enabled(true);
        }
    }
    /* Synodic wake sweep consumer (see the sweep section): each
     * published frame is displayed as ONE atomic whole frame (the
     * spin's display) -- never left for LVGL's async 25-row drain. The
     * task paces itself on s_sweep_shown (backpressure), so the cycle
     * unfolds at the bake rate (~0.51 s/frame at the default 240 px:
     * ~0.28 s bake + ~0.23 s display+tick, ~2.8 min for the default
     * 336 frames). */
    if (s_sweep_baking && s_sweep_pub > s_sweep_shown) {
        moon_invalidate_all();
        int64_t r0 = esp_timer_get_time();
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        s_sweep_shown = s_sweep_pub;
        ESP_LOGI(TAG, "sweep %u/%d shown in %dms", (unsigned)s_sweep_shown, (int)s_sweep_frames,
                 (int)((esp_timer_get_time() - r0) / 1000));
        if (s_sweep_shown >= (uint32_t)s_sweep_frames) {
            /* Last frame on screen: it IS the current phase, unrotated.
             * Re-anchor BEFORE the task exits (its final wait runs on
             * this very advance), so the spin resumes from exactly
             * this pose -- the month ends where the clock is. Boot
             * flow: no spin task is running yet (moon_ui_start did not
             * start one -- the sweep owns the buffers), so start it here,
             * after the re-anchor: the render integrates theta from
             * zero on the fresh last frame (moon_spin_begin is a
             * no-op if a spin task is already running). Only while a
             * moon screen is active: on a moon-less screen the render
             * would park on the (withheld) display handshake, and the
             * screen switch back to the moon starts it then (the
             * re-anchored theta is already zero). */
            moon_spin_reanchor();
            if (s_moon_screen)
                moon_spin_begin();
            if (s_orbit_epoch <= 0) {
                /* Boot sweep: the first static frame of the session.
                 * The orbit pipeline needs the phase epoch (the sweep's
                 * last frame bakes the default viewport, so the
                 * on-screen pose is orbit angle 0). */
                s_orbit_epoch = s_sweep_now;
                s_orbit_baked = 0;
            }
            if (!clock_is_idle())
                clock_idle_set_enabled(true);
        }
    }
    /* Light-sleep "dusk" consumer (see the dusk section): like the
     * sweep, each published frame is ONE atomic whole frame. On the
     * LAST frame the deep-sleep countdown is armed -- the sleep depth
     * follows the animation (the timer fires only once the fully
     * unilluminated side is on screen), never at the light-sleep entry
     * itself. Same LVGL-task context the countdown poll callback runs
     * in. */
    if (s_dusk_baking && s_dusk_pub > s_dusk_shown) {
        moon_invalidate_all();
        int64_t r0 = esp_timer_get_time();
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        s_dusk_shown = s_dusk_pub;
        ESP_LOGI(TAG, "dusk %u/%d shown in %dms", (unsigned)s_dusk_shown, (int)s_dusk_frames,
                 (int)((esp_timer_get_time() - r0) / 1000));
        if (s_dusk_shown >= (uint32_t)s_dusk_frames) {
            esp_err_t ret = clock_timer_enable(settings_deep_sleep_countdown_sec(),
                                               CLOCK_TIMER_FREQ_1HZ | CLOCK_TIMER_MODE_ENABLE_INT);
            if (ret == ESP_OK)
                ESP_LOGI(TAG, "dusk done; RTC countdown %u s to deep sleep armed",
                         (unsigned)settings_deep_sleep_countdown_sec());
            else
                ESP_LOGE(TAG, "dusk done but the countdown arm failed: %d", (int)ret);
        }
    }
    moon_imu_tick();    /* tilt drives the orbit target (no-op without an IMU) */
    moon_orbit_drain(); /* latest-wins: start a pending orbit bake */
}

/* ---------------- public API ---------------- */

/* The panel-io watchdog task (see the s_panel_mtx block): take the panel
 * mutex with a finite timeout -- a normal take returns in <100 ms (a
 * single strip or one spin frame), so a wait that overruns the limit
 * means the current holder is stuck inside a panel-io write on a bus
 * that will never free. Log and restart: the device comes back clean
 * (the moon re-bakes from the RTC; the spin angle re-anchors) instead
 * of freezing. Priority 3 = above the spin (configMAX_PRIORITIES - 3)
 * and every UI task; the take is the ONLY work, so it cannot starve
 * anything. */
static void moon_panel_watchdog_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PANEL_IO_WATCHDOG_LIMIT_MS));
        if (!s_panel_watchdog_armed)
            continue;
        TickType_t t0 = xTaskGetTickCount();
        BaseType_t r = xSemaphoreTake(s_panel_mtx, pdMS_TO_TICKS(PANEL_IO_WATCHDOG_LIMIT_MS));
        if (r == pdTRUE) {
            xSemaphoreGive(s_panel_mtx);
            continue;
        }
        /* Still held after PANEL_IO_WATCHDOG_LIMIT_MS of waiting, on top
         * of PANEL_IO_WATCHDOG_LIMIT_MS of idle -- the holder (a spin
         * strip, a flush strip or a 0x51 write) is parked inside
         * spi_device_acquire_bus on a bus lock that will never clear.
         * Restart so the user gets a working clock instead of a dead
         * one; the log marks it unambiguously for the field. */
        ESP_LOGE(TAG,
                 "PANEL IO STUCK: panel mutex held %lu ms after a %lu ms wait -- the QSPI "
                 "bus lock is stuck (a DMA completion was lost); restarting",
                 (unsigned long)(xTaskGetTickCount() - t0) * portTICK_PERIOD_MS,
                 (unsigned long)PANEL_IO_WATCHDOG_LIMIT_MS);
        vTaskDelay(pdMS_TO_TICKS(100)); /* let the log flush out the UART */
        esp_restart();
    }
}

void moon_ui_init(void) {
    if (s_ready)
        return;

    s_frame = heap_caps_malloc(MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
    s_rgb = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    /* Reduced-res intermediate sweep frame (UI_MOON_ANIM_BAKE_PX): the
     * wake sweep's non-final frames bake here (smaller than MOON_PX^2) and
     * are upscased into s_rgb on display. Allocated at the bake size (not
     * MOON_PX^2) -- the bake writes size^2 bytes. If the reduction is
     * disabled (bake size >= MOON_PX) this stays NULL and the sweep bakes
     * intermediate frames into s_frame directly (the old full-res path). */
    int sweep_bake_px = CONFIG_UI_MOON_ANIM_BAKE_PX;
    if (sweep_bake_px < MOON_PX)
        s_sweep_small = heap_caps_malloc((size_t)sweep_bake_px * sweep_bake_px, MALLOC_CAP_SPIRAM);
    /* The dusk's pre-shaded body-frame equirect map (the constant-
     * projection pass; see the dusk section): MOON_ALB_W x MOON_ALB_H
     * 8-bit cells, built once per dusk epoch, re-projected every dusk
     * frame (no per-frame lighting recompute). */
    s_shade_map = heap_caps_malloc((size_t)MOON_ALB_W * MOON_ALB_H, MALLOC_CAP_SPIRAM);
    if (!s_frame || !s_rgb || (sweep_bake_px < MOON_PX && !s_sweep_small) || !s_shade_map) {
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
    /* Eagerly load the albedo + build the relief gradient cache HERE, on
     * the main task, before ui_init / moon_ui_start can spawn a bake task
     * (orbit/sweep). The build is the one slow, non-idempotent op (one 4 MB
     * packed PSRAM array from a one-shot whole-DEM read +
     * moon_relief_build); running it once up front means the later
     * moon_bake_epoch / orbit / sweep moon_relief() calls are cheap
     * pointer returns with no concurrent-build race (two tasks both
     * seeing relief_built == false and double-allocating the array). */
    if (!moon_albedo() || !moon_relief())
        ESP_LOGW(TAG, "moon assets unavailable: moon will bake relief-free or stay blank");
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
     * s_rgb / s_frame reader+writer (the render's gather, the orbit's
     * row sweep, the static LUT convert), and a PANEL mutex serializing
     * the spin's direct panel writes with LVGL's own flushes (which
     * reach the panel through the draw_bitmap wrapper installed below
     * -- every LVGL flush funnels through esp_lcd_panel_draw_bitmap,
     * proven by the adapter's flush path and the old reveal). */
    s_rgb_mtx = xSemaphoreCreateMutex();
    s_panel_mtx = xSemaphoreCreateMutex();
    if (!s_rgb_mtx || !s_panel_mtx) {
        ESP_LOGE(TAG, "spin sync allocation failed; moon disabled");
        s_ready = false;
        return;
    }
    /* The spin's direct-panel-write buffers (see the spin section): the
     * front mirror (LVGL's on-panel content, moon area, panel byte
     * order) + the per-frame composite, in PSRAM (the panel io stages
     * PSRAM strips into internal RAM itself -- the same path the old
     * reveal's snapshot write took). */
    s_front = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    s_disp = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    s_ui_mask = heap_caps_malloc(MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
    memset(s_front ? (void *)s_front : (void *)1, 0, (size_t)MOON_PX * MOON_PX * 2);
    memset(s_disp ? (void *)s_disp : (void *)1, 0, (size_t)MOON_PX * MOON_PX * 2);
    /* The UI keep-mask: the screen is loaded (ui_init ran first), so
     * the first rebuild captures the home screen's text + arc. */
    spin_mask_rebuild();
    /* The panel handle + the draw_bitmap wrapper (the serialization
     * point with LVGL). bsp_display_start runs before moon_ui_init
     * (app_main), so the handle is valid. */
    s_panel = board_panel();
    if (s_panel) {
        s_orig_draw = s_panel->draw_bitmap;
        s_panel->draw_bitmap = moon_panel_draw_bitmap;
        s_panel_wrap = true;
        ESP_LOGI(TAG, "panel draw_bitmap wrapper installed (spin direct write)");
    } else {
        ESP_LOGE(TAG, "panel handle unavailable; spin direct write off");
    }
    /* 1 Hz tilt activity/wake monitor; runs in every screen state
     * because the moon tick itself is paused in deep sleep. It also
     * caches the LVGL task handle (moon_on_lvgl), so the timer is
     * created even when UI_IMU_ENABLE is off (see the callback). */
    lv_timer_create(moon_tilt_monitor_cb, 1000, NULL);
    /* The panel-io watchdog (the restart guard for the lost-DMA-completion
     * bus stall; the panel handle exists, so its lock is the one to watch).
     * Low priority on core 0: it sleeps 3 s at a time and needs only a few
     * instructions to test the mutex, so the LVGL task (also core 0) and
     * the spin (core 1) are never starved by it. */
    if (s_panel) {
        if (xTaskCreatePinnedToCore(moon_panel_watchdog_task, "panel_wdg", 4096, NULL, 3, &s_panel_watchdog, 0) !=
            pdPASS) {
            ESP_LOGW(TAG, "panel watchdog task failed to start");
        } else {
            s_panel_watchdog_armed = true;
            ESP_LOGI(TAG, "panel-io watchdog armed (%lu ms stuck limit)", (unsigned long)PANEL_IO_WATCHDOG_LIMIT_MS);
        }
    }
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
    if (s_n_images < 8) {
        s_images[s_n_images] = img;
        s_moon_roots[s_n_images] = parent;
        s_n_images++;
    }
    return img;
}

void moon_ui_refresh(void) { moon_invalidate_all(); }

/* Publish the orientation step (0..3 clockwise 90-degree steps, the
 * root transform_rotation's value in 900 units) to the spin's direct
 * panel write: spin_step_rotate pre-rotates every frame by it, so the
 * panel shows the same pixels LVGL's own root transform would. Called
 * from the orientation pipeline (LVGL task context) on every step
 * change; the render reads the volatile under no lock (a step change
 * mid-frame at most shows one frame at the old rotation -- the
 * orientation itself changes in 90-degree steps by design). */
void moon_ui_set_panel_step(int step) {
    const int st = ((step % 4) + 4) % 4;
    if (st != s_panel_step)
        s_mask_dirty = true; /* the mask is screen-local: it must be
                                re-rotated by the new step (the LVGL
                                task does it, see moon_tilt_monitor_cb) */
    s_panel_step = st;
}

/* Rebuild the UI keep-mask (LVGL task only -- the snapshot is the
 * refresh draw path): called on an orientation step change (the
 * mask is screen-local and must be re-rotated by the current step).
 * No-op off the LVGL task (the app_main base-orientation push runs
 * before the LVGL task handle is cached). */
void moon_ui_rebuild_ui_mask(void) {
    if (moon_on_lvgl())
        spin_mask_rebuild();
}

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
         * than a wrong phase -- the moon starts on the black screen the
         * init cleared and STAYS black. The daily day-tick (re-bake +
         * spin start) picks it up once the clock syncs. */
        ESP_LOGW(TAG, "no UTC time yet; moon stays blank until the clock syncs");
        lv_timer_resume(s_tick);
        return;
    }
    /* Boot: the moon starts on a BLACK screen (s_rgb was zeroed at
     * init) and the synodic sweep reveals it -- new moon -> now, ending
     * on the current phase; the free spin then resumes from the
     * sweep's last frame. The kickoff is the whole of the boot
     * animation (the sweep's first frame is the dark new moon: the
     * reveal is built into the pipeline, no separate fade). */
    moon_ui_replay_animation();
    if (!s_sweep_baking) {
        /* No sweep (UI_MOON_ANIM_ENABLE off / kickoff refused): bake the
         * current phase directly so the screen is not left black. */
        moon_orbit_interrupt(); /* the boot bake owns the buffers */
        s_baked_day = now / 86400;
        moon_bake_epoch(now);
        moon_spin_begin();
    }
    lv_timer_resume(s_tick);
}

void moon_ui_replay_animation(void) {
    /* Synodic sweep (see the sweep section): the current synodic cycle
     * (last NEW MOON -> now) in CONFIG_UI_MOON_ANIM_FRAMES frames,
     * baked off screen by the bake task and displayed one atomic whole
     * frame per tick, ending on the current phase. This is the BOOT
     * animation only (moon_ui_start kicks it off from the black
     * screen; the free spin resumes from the sweep's last frame) --
     * wakes from sleep show the current phase STATIC, no animation.
     * No-op before moon_ui_start and while the orbit / sweep pipeline
     * owns the buffers. The spin is held for the sweep; the idle timer
     * is held too (the user is present for the sweep's ~2.8 min at the
     * default 240 px). */
    if (!boot_anim_enabled() || !s_ready || !s_started || s_orbit_baking || s_orbit_sweeping || s_sweep_baking)
        return;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK)
        return;
    moon_sweep_interrupt(); /* defensive join; nothing should be running */
    s_sweep_now = now;
    s_sweep_frames = CONFIG_UI_MOON_ANIM_FRAMES;
    s_sweep_pub = 0;
    s_sweep_shown = 0;
    s_sweep_abort = false;
    s_frame_stale = false;
    s_baked_day = now / 86400; /* the sweep's final frame is the current phase */
    /* Ownership flag BEFORE the task exists (the orbit rule): the task
     * may start baking s_rgb rows immediately; the spin's pause-poll and
     * the tick's display guard must already be stopped. */
    s_sweep_baking = true;
    if (xTaskCreatePinnedToCoreWithCaps(moon_sweep_task, "moon_sweep", 8192, NULL, configMAX_PRIORITIES - 3,
                                        &s_sweep_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "sweep task allocation failed; moon stays on the current frame");
        s_sweep_baking = false;
        return;
    }
    if (!clock_is_idle())
        clock_idle_set_enabled(false); /* hold sleep for the sweep duration */
    ESP_LOGI(TAG, "synodic boot sweep started: %d frames (new moon -> now)", s_sweep_frames);
}

bool moon_ui_dusk_start(void) {
    /* Light-sleep entry (see the dusk section): the pole-axis spin from
     * the EXACT on-screen pose the free spin parked the moon at to the
     * fully unilluminated side, 1 degree per frame (the smoothness
     * bar), shortest arc -- at most a half turn. The caller
     * (action_switch_to_light_sleep_screen) runs moon_ui_pause(true)
     * first: the spin is joined and the moon parked, so the parked pose
     * (in-plane theta + orbit viewport) is stable to capture here, and
     * the tick is paused -- resumed below, the dusk frames need it to
     * display. Returns FALSE when the dusk could not start (no static
     * phase yet, a pipeline owns the buffers, or the dark side is
     * already the on-screen state after a completed dusk): the caller
     * then arms the deep-sleep countdown immediately (the pre-dusk
     * behavior) so the deep-sleep transition still happens. */
    if (!dusk_anim_enabled() || !s_ready || !s_started || s_orbit_epoch <= 0 || s_orbit_baking || s_orbit_sweeping ||
        s_sweep_baking || s_dusk_baking || s_dusk_done)
        return false;
    /* The parked pose, captured before the task exists: the in-plane
     * free-spin angle (theta-folded into every dusk frame's basis --
     * the seamless seam) and the tilt orbit viewport (endpoint
     * computed from the orbit-folded view). */
    s_dusk_theta = (float)fmod(s_spin_theta, 6.283185307179586);
    s_dusk_orbit = s_orbit_baked;
    s_dusk_epoch = s_orbit_epoch;
    moon_geom_t g0;
    moon_geometry(s_dusk_epoch, &g0);
    moon_geom_t g1;
    moon_orbit_geometry(&g0, (float)s_dusk_orbit * (float)M_PI / 180.0f, &g1);
    float A = g1.E[0] * g1.S[0] + g1.E[1] * g1.S[1];
    float B = g1.E[0] * g1.S[1] - g1.E[1] * g1.S[0];
    float phi_star = atan2f(-B, -A);                                        /* the fully unilluminated view */
    int32_t frames = (int32_t)lrintf(fabsf(phi_star) * 57.29577951308232f); /* 1 deg/frame */
    if (frames < 1)
        frames = 1;
    s_dusk_frames = frames;
    s_dusk_pub = 0;
    s_dusk_shown = 0;
    s_dusk_abort = false;
    s_dusk_done = false;
    /* Ownership flag BEFORE the task exists (the sweep's rule): the
     * task may start baking s_rgb rows immediately; the tick's display
     * guards (spin drain, orbit drain, screen switch) must already be
     * stopped. */
    s_dusk_baking = true;
    if (xTaskCreatePinnedToCoreWithCaps(moon_dusk_task, "moon_dusk", 8192, NULL, configMAX_PRIORITIES - 3, &s_dusk_task,
                                        1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "dusk task allocation failed; the moon stays parked");
        s_dusk_baking = false;
        return false;
    }
    lv_timer_resume(s_tick); /* the dusk frames display from the tick */
    ESP_LOGI(TAG, "dusk started: %d frames (%+.0f deg about the pole, 1 deg/frame)", frames,
             phi_star * 57.29577951308232f);
    return true;
}

void moon_ui_day_tick(void) {
    /* Guarded on s_started, not just s_ready: at boot the main-screen
     * handler runs BEFORE moon_ui_start (its one first bake: the
     * one-time gradient-cache build + the ~1.3 s bake), and a
     * valid RTC would otherwise re-bake here (s_baked_day is 0) -- a
     * duplicate boot bake. The day-tick is for "the day rolled over while
     * we slept"; the first bake is moon_ui_start's alone. */
    if (!s_ready || !s_started)
        return;
    int64_t now;
    if (clock_get_utc_epoch(&now) != ESP_OK)
        return;
    int64_t day = now / 86400;
    if (day != s_baked_day) {
        if (s_orbit_baking || s_orbit_sweeping || s_sweep_baking || s_dusk_baking || s_dusk_done) {
            /* The orbit or wake-sweep pipeline owns s_frame/s_rgb until
             * it finishes; defer (do NOT advance s_baked_day) so the
             * next tick re-tries and the bakes never race on the
             * buffers. s_dusk_done: the dark side is on screen (light
             * or deep sleep, countdown running) -- a re-bake here would
             * replace it with the lit phase. The wake path
             * (moon_ui_pause(false)) bakes the rolled-over day. */
            return;
        }
        ESP_LOGI(TAG, "new day; re-baking moon phase");
        s_baked_day = day;
        moon_bake_epoch(now);
        /* Only while a moon screen is active: starting the render on a
         * moon-less screen would park it on the (withheld) display
         * handshake; the screen switch back to the moon starts/primed it
         * then, from this fresh s_frame. */
        if (s_moon_screen)
            moon_spin_begin();
    }
}

void moon_ui_pause(bool pause) {
    if (!s_tick)
        return;
    if (pause) {
        /* Sleep entry (and the alarm screen): the pipelines take
         * priority in this order -- join the wake sweep, stop the spin
         * render, interrupt the orbit -- then the moon PARKS: one final
         * frame rendered at the EXACT current wall-clock spin angle and
         * displayed atomically (see moon_park), so the sleep screens
         * show the pose the clock says the moon is in, not wherever the
         * last frame happened to land (up to ~200 ms / ~0.4 deg stale).
         * Sleep is the preparation for the full stop. All of this is
         * bounded (each join polls its exit at 10 ms) and runs in the
         * LVGL task, so the display stays live the whole way. */
        moon_sweep_interrupt();
        moon_dusk_interrupt();
        moon_spin_stop();
        moon_orbit_interrupt(); /* sleep shows the last baked viewport */
        if (s_started && !s_frame_stale && !s_dusk_done)
            moon_park(); /* s_dusk_done: the dark side is already on
                          * screen (a completed dusk) -- the sleep screens
                          * keep showing it, no re-park */
        lv_timer_pause(s_tick);
    } else {
        /* Wake (any input: touch, tilt, alarm dismiss): interrupt a
         * RUNNING dusk first -- otherwise it would keep baking/displaying
         * toward the dark side (and arm the deep-sleep countdown) while
         * the user is back. The join is bounded (<= 400 ms, polled at
         * 10 ms) and runs in the LVGL task, like the sleep-side joins. */
        moon_dusk_interrupt();
        if (s_frame_stale && s_started && !s_sweep_baking) {
            /* A mid-bake abort left s_frame/s_rgb partial (a sleep
             * transition landed on an in-flight bake): re-bake the
             * current phase atomically before the spin may gather.
             * Skipped while a sweep owns the buffers: the sweep's last
             * frame IS the current phase, and it re-anchors the spin
             * itself when it lands. */
            int64_t now;
            if (clock_get_utc_epoch(&now) == ESP_OK) {
                ESP_LOGI(TAG, "frame was mid-bake at sleep; re-baking before wake");
                s_baked_day = now / 86400;
                moon_bake_epoch(now);
                s_frame_stale = false;
            }
        }
        if (s_dusk_done || s_dusk_on_screen) {
            /* A dusk (completed or interrupted) left one of ITS frames
             * on screen (the dark side / an intermediate spin frame)
             * while s_frame holds the static phase. A wake shows the
             * CURRENT phase STATIC, at the parked pose, ONE atomic
             * whole frame -- no animation (the spin's parked angle is
             * exactly the pose the dusk started from). */
            s_dusk_done = false;
            s_dusk_on_screen = false;
            int64_t now;
            if (clock_get_utc_epoch(&now) == ESP_OK && now / 86400 != s_baked_day) {
                /* The dusk's pre-bake is the phase at the SLEEP entry --
                 * possibly a day old for an all-night sleep (same
                 * rule as moon_ui_day_tick): re-bake the current phase
                 * before the pose display shows it. */
                ESP_LOGI(TAG, "wake after dusk on a new day; re-baking before the pose display");
                moon_bake_epoch(now);
            }
            float rad = (float)fmod(s_spin_theta, 6.283185307179586);
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            moon_rotate(s_frame, MOON_PX, s_rgb, MOON_PX, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2, MOON_PX / 2,
                        moon_cos15(rad), moon_sin15(rad));
            xSemaphoreGive(s_rgb_mtx);
            moon_invalidate_all();
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
            /* Re-anchor the spin to the displayed pose (like
             * moon_park): the on-screen frame is now at this angle. */
            s_spin_last_rad = rad;
            ESP_LOGI(TAG, "wake after dusk: current phase at the parked pose (spin deg %d), no animation",
                     (int)(rad * 57.29577951308232f + 0.5f));
        }
        lv_timer_resume(s_tick);
        /* Wake from sleep: restart the render task (stopped on the
         * sleep transition). The free spin's angle/rate persist across
         * the pause (the render clamps its first dt to 0.5 s), so the
         * first frame is exactly where the spin parked at sleep -- no
         * snap on wake. Guarded by s_started: at boot the screen
         * actions run before moon_ui_start, which does the first bake +
         * spin start. */
        if (s_started)
            moon_spin_begin();
    }
}

/* A screen switch happened (the plain lv_scr_load in loadScreen):
 * display the new screen's contents -- the moon image included -- as
 * ONE atomic whole-panel flush, the same atomic rule the spin frames
 * obey. lv_scr_load invalidates the whole new screen; without this
 * immediate flush LVGL's async 25-row strip drain would repaint it
 * top-to-bottom over ~a second (the wake artifact).
 *
 * The flush is safe whenever s_rgb holds a complete frame -- the steady
 * spin (s_rgb is always a whole rendered frame) and an orbit sweep in
 * flight (s_rgb fully converted, the tick is draining its strips). It
 * is unsafe ONLY while a bake task is actively rewriting s_rgb row by
 * row (s_orbit_baking / s_sweep_baking): that leaves a temporarily
 * mixed frame (new top rows over old bottom rows) that a full flush
 * would put on screen. Skip in that case: the bake's completion display
 * (or the spin's next atomic frame) lands the new screen atomically.
 *
 * Must be on the LVGL task (8 KB stack) -- a whole-panel lv_refr_now
 * overflows the 3.5 KB main task at boot; off it the invalidated screen
 * is painted by the LVGL task's async refresh (a boot crawl is
 * acceptable, the moon is not baked yet anyway). */

/* Does the screen just loaded carry a moon image? The images persist
 * (created once at boot); only the ACTIVE screen matters. The moon
 * spin is pointless on a moon-less screen (the settings screens idle
 * at ~50% CPU with the spin render task + the tick's full-panel
 * lv_refr_now both running at the spin's cadence), so this is the
 * lever that starts/stops the render task on screen change. */
static bool moon_screen_has_image(void) {
    lv_obj_t *active = lv_screen_active();
    for (int i = 0; i < s_n_images; i++) {
        if (s_moon_roots[i] == active)
            return true;
    }
    return false;
}

/* ---------------- screen-switch fade (UI_SCREEN_SWITCH_FADE) ----------------
 *
 * EVERY screen switch dims the WHOLE PANEL to black and back
 * (moon_ui_fade_out in loadScreen BEFORE lv_scr_load; the fade-in leg
 * in moon_ui_screen_switched). The ONE exception is the both-moon
 * switch (airplane / wifi / the sleep screens share the one moon
 * image): a fade there would only dim the moon, which is the same
 * disk on both screens. The dimming is the CO5300's OWN brightness
 * control (command 0x51,
 * bsp_display_brightness_set -- the same hardware dimming the
 * deep-sleep backlight off/on uses), so the fade writes NO pixels: it
 * scales everything on screen at once (the moon, the labels, the
 * spin's direct writes) with no overlay to composite and no per-step
 * re-render. The old overlay fade charged one full-screen lv_refr_now
 * per step (~175 ms of QSPI strip traffic each, 4 steps per leg) and
 * the panel's left-to-right strip fill showed as a slow crawl on every
 * switch; here a fade step is one ~100 us SPI param transaction, so
 * the steps are many and short.
 *
 *   fade-out: the brightness ramps 100 -> 0 in FADE_STEPS steps of
 *   CONFIG_UI_SCREEN_SWITCH_FADE / FADE_STEPS ms. The spin KEEPS
 *   running -- its frames just dim, so the moon keeps moving while the
 *   screen goes dark. s_fade_pending is left set for the fade-in.
 *
 *   fade-in: the new screen is displayed as ONE atomic whole-panel
 *   lv_refr_now while the panel is STILL BLACK (invisible -- the
 *   invariant's atomic display, off the visible path), then the
 *   brightness ramps 0 -> 100 and the fully rendered new screen comes
 *   up at once.
 *
 * Both-moon switches (airplane / wifi / the sleep screens) and
 * switches while a bake pipeline owns the buffers keep the plain
 * atomic whole-panel switch. Must run on the LVGL task. */
#define FADE_STEPS 12
static bool s_fade_pending;       /* the fade-out dimmed the panel; the fade-in is owed */
static volatile bool s_fade_busy; /* a fade ramp is running: the spin skips its
                                      panel writes so the ramp's 0x51 writes are
                                      the ONLY panel-io traffic in flight */

/* The panel's current hardware brightness (0..100 %), tracked here because
 * every brightness write in the project funnels through
 * moon_ui_panel_brightness_set: the fade-out ramp starts from the ACTUAL
 * level (not a hard-coded 100 -- after the light-sleep dim the panel sits
 * at UI_LIGHT_SLEEP_DIM_PCT, and a 100 -> 0 ramp would flash it brighter
 * first), and the dim ramp (moon_ui_panel_dim_ramp) is a no-op when the
 * panel is already at its target. The backlight paths write it; the ramps
 * read it (LVGL task). Benign int, no lock. */
static volatile int s_panel_pct = 100;

/* Ramp the panel's hardware brightness from `from_pct` to `to_pct`
 * (0..100) in FADE_STEPS steps, paced CONFIG_UI_SCREEN_SWITCH_FADE /
 * FADE_STEPS ms between steps. Each step is one 0x51 SPI param
 * transaction (~100 us): the ramp is free, so the steps are many and
 * short (the luminance steps stay small enough not to read as
 * discrete steps). Each write takes the panel mutex: a 0x51 write must
 * not land in the middle of a whole-panel flush or a spin frame (the
 * panel-io driver's bus lock serializes them, but a 0x51 queued behind
 * a queued strip that never completes parks the ramp's writer forever --
 * the freeze this project hit). The fade sets s_fade_busy, and the spin
 * holds its own panel writes for the ramp's duration, so in practice the
 * mutex is free when the ramp runs. */
static void fade_ramp(int from_pct, int to_pct) {
    for (int s = 1; s <= FADE_STEPS; s++) {
        int pct = from_pct + (to_pct - from_pct) * s / FADE_STEPS;
        xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
        bsp_display_brightness_set(pct);
        xSemaphoreGive(s_panel_mtx);
        if (s < FADE_STEPS)
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UI_SCREEN_SWITCH_FADE / FADE_STEPS));
    }
    s_panel_pct = to_pct; /* the steps are portMAX_DELAY: by the end the
                           * panel is unambiguously at the target */
}

/* Dim the panel to black ahead of a screen switch (LVGL task;
 * loadScreen calls it before lv_scr_load on every switch except the
 * both-moon one). No pixels are written; the spin's PANEL WRITES pause
 * for the ramp (s_fade_busy -- the 0x51 ramp must be the only panel-io
 * traffic in flight so a lost-DMA-completion stall cannot strand it
 * behind a queued strip: the spin's angle keeps integrating, so the
 * moon just holds its pose for ~240 ms and resumes from the exact angle
 * when the switch completes). The fade-in leg (moon_ui_screen_switched)
 * displays the new screen while the panel is still black and ramps the
 * brightness back. No-op when the fade is off, the moon UI is not
 * ready, or a bake pipeline owns the buffers. */
void moon_ui_fade_out(void) {
    if (!s_ready || !moon_on_lvgl() || CONFIG_UI_SCREEN_SWITCH_FADE <= 0 || s_orbit_baking || s_sweep_baking ||
        s_dusk_baking)
        return;
    int from = s_panel_pct;
    s_fade_pending = true; /* the fade-in (0 -> 100) is owed either way */
    if (from > 0) {
        s_fade_busy = true; /* the spin holds its panel writes until the
                             * screen switch (moon_ui_screen_switched) */
        fade_ramp(from, 0);
    }
    ESP_LOGI(TAG, "screen switch fade-out: panel brightness %d -> 0 (%d ms)", from, CONFIG_UI_SCREEN_SWITCH_FADE);
}

/* Does the screen with this id carry a moon image (loadScreen's fade
 * decision: every switch fades except the both-moon one, which stays
 * atomic)? */
bool moon_ui_screen_is_moon(enum ScreensEnum id) {
    lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
    if (!root)
        return false;
    for (int i = 0; i < s_n_images; i++)
        if (s_moon_roots[i] == root)
            return true;
    return false;
}

/* The panel's hardware brightness (0..100%), serialized with every other
 * panel-io consumer: a 0x51 write must not be queued behind a whole-panel
 * flush or a spin's in-flight strip DMAs -- the panel-io driver's bus lock
 * would serialize it anyway, and if one of those DMAs never completes the
 * queued 0x51 parks this caller forever (the settings-entry freeze). The
 * fade ramp (same file) and the spin/LVGL flushes (s_panel_mtx) are the
 * other users of the lock; this is the one for the wake / deep-sleep /
 * boot backlight paths (actions.c, main.cpp). When the moon UI is not
 * ready the BSP call runs directly (the panel is quiet at boot). */
esp_err_t moon_ui_panel_brightness_set(int pct) {
    esp_err_t r;
    if (s_ready && s_panel_mtx) {
        xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
        r = bsp_display_brightness_set(pct);
        xSemaphoreGive(s_panel_mtx);
    } else {
        r = bsp_display_brightness_set(pct);
    }
    if (r == ESP_OK)
        s_panel_pct = pct; /* the fade/dim ramps start from the ACTUAL level */
    return r;
}

/* Ramp the panel's hardware brightness to `to_pct` (0..100 %) from the
 * current tracked level (s_panel_pct), paced like the screen-switch fade
 * (UI_SCREEN_SWITCH_FADE ms, FADE_STEPS steps). LVGL task. This is the
 * light-sleep dim: the entry ramps DOWN to CONFIG_UI_LIGHT_SLEEP_DIM_PCT
 * and the wake / an alarm ring ramp back to 100. The ramp holds
 * s_fade_busy WITHOUT s_fade_pending -- the spin skips its panel writes
 * for the ramp's duration (the 0x51 steps must be the only panel-io
 * traffic in flight, the fade's rule) but no fade-in is owed afterwards:
 * the next screen switch paints plain (s_fade_pending is untouched).
 * The dusk's frames display from the tick (this same task), so they are
 * blocked behind the ramp and simply resume at the new level. No-op when
 * the panel is already at the target -- the call sites are unconditional
 * (the wake and alarm paths also run on a fully-lit panel). Non-LVGL /
 * not-ready: the instant set. */
void moon_ui_panel_dim_ramp(int to_pct) {
    int from = s_panel_pct;
    if (from == to_pct)
        return;
    if (!s_ready || !moon_on_lvgl()) {
        moon_ui_panel_brightness_set(to_pct);
        return;
    }
    s_fade_busy = true;      /* the spin holds its panel writes for the ramp */
    fade_ramp(from, to_pct); /* also tracks s_panel_pct */
    s_fade_busy = false;
    ESP_LOGI(TAG, "panel dim ramp: brightness %d -> %d%% (%d ms)", from, to_pct, CONFIG_UI_SCREEN_SWITCH_FADE);
}

void moon_ui_screen_switched(void) {
    if (s_ready && moon_on_lvgl()) {
        /* Start/stop the spin render with the active screen (LVGL task:
         * the task create/delete belongs here). While a bake pipeline
         * owns the buffers it manages the spin itself, so leave it alone. */
        bool moon_here = moon_screen_has_image();
        if (moon_here != s_moon_screen) {
            s_moon_screen = moon_here;
            if (!s_orbit_baking && !s_sweep_baking && !s_dusk_baking) {
                if (moon_here) {
                    /* The free spin's angle/rate persist across the
                     * stop (the render clamps its first dt to 0.5 s), so
                     * the moon resumes from the exact pose it showed.
                     * The new render task needs no priming: it writes
                     * its first frame straight to the panel. */
                    moon_spin_begin();
                } else {
                    moon_spin_stop();
                    ESP_LOGI(TAG, "spin render stopped (active screen has no moon image)");
                }
            }
        }
        /* The UI keep-mask follows the active screen (the spin's
         * composite keeps LVGL's pixels only where the mask says so):
         * the snapshot is the refresh draw path, LVGL task only. */
        spin_mask_rebuild();
    }
    if (!s_ready || !moon_on_lvgl() || s_orbit_baking || s_sweep_baking || s_dusk_baking) {
        s_fade_pending = false; /* a plain async paint shows the new screen */
        s_fade_busy = false;
        return;
    }
    if (s_fade_pending && CONFIG_UI_SCREEN_SWITCH_FADE > 0) {
        /* The fade-in leg: the new screen's ONE atomic whole-panel
         * flush runs while the panel is STILL BLACK (invisible -- the
         * invariant's atomic display, off the visible path), then the
         * hardware brightness ramps back to full and the fully
         * rendered screen comes up at once. The flush strips take the
         * panel mutex in the draw_bitmap wrapper (per strip -- taking
         * it here as well would self-deadlock: the mutex is
         * non-reentrant and the wrapper IS the flush's panel path),
         * and the ramp's 0x51 writes take it per step. The spin holds
         * its panel writes while s_fade_busy, so the only panel-io
         * traffic in flight is the flush's own. */
        s_fade_pending = false;
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        fade_ramp(0, 100);
        s_fade_busy = false; /* the spin's panel writes resume */
        ESP_LOGI(TAG, "screen switch fade-in: panel brightness 0 -> 100 (%d ms)", CONFIG_UI_SCREEN_SWITCH_FADE);
        return;
    }
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    lv_refr_now(NULL);
    xSemaphoreGive(s_rgb_mtx);
}

/* Hang-diagnostic context dump (TEST_HANG_WATCH only; the hang-watcher in
 * app_main calls it when the LVGL heartbeat goes stale). Prints the volatile
 * pipeline flags around the fade / spin / flush path, so the task-state dump
 * (which names the BLOCKED tasks) can be cross-referenced with the pipeline
 * state to identify WHICH portMAX_DELAY take a stuck task is parked on. The
 * only project locks on that path are s_rgb_mtx (buffer) and s_panel_mtx
 * (panel); a deadlock must involve one of them (or the panel io's internal
 * SPI lock). Safe from any task (volatile reads only). */
void moon_ui_dump_hang_context(void) {
    ESP_LOGE(TAG, "  flags: ready=%d moon_screen=%d spin_running=%d spin_cancel=%d fade_pending=%d", (int)s_ready,
             (int)s_moon_screen, (int)s_spin_running, (int)s_spin_cancel, (int)s_fade_pending);
    ESP_LOGE(TAG, "  flags: panel_pct=%d fade_busy=%d", (int)s_panel_pct, (int)s_fade_busy);
    ESP_LOGE(TAG, "  flags: orbit_baking=%d orbit_sweep=%d sweep_baking=%d dusk_baking=%d frame_stale=%d",
             (int)s_orbit_baking, (int)s_orbit_sweeping, (int)s_sweep_baking, (int)s_dusk_baking, (int)s_frame_stale);
    ESP_LOGE(TAG, "  flags: panel_step=%d panel_wrap=%d mask_dirty=%d", (int)s_panel_step, (int)s_panel_wrap,
             (int)s_mask_dirty);
}
