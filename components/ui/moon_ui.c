/**
 * Moon phase UI (device side of components/moon).
 *
 * Runtime model (see components/moon/include/moon.h for the portable core):
 *   - one 8-bit grayscale bake (moon_bake) of the lit disk in the FIXED
 *     Earth viewport, only when the phase changes (~daily) or when the
 *     tilt moves the orbit (sphere model, below);
 *   - the home screen shows the finished frame as a STATIC image with
 *     plain LVGL. The burn-in spin that used to rotate the frame in
 *     place (direct panel writes at 8 fps) was removed 2026-11-13 --
 *     its compositing machinery caused the clock-row flicker family
 *     and cost more than the burn-in duty it served.
 *
 * Boot (UI_MOON_BOOT_REPLAY, the replay section): the core-1 replay
 * producer plays ALL stored TF hour frames ascending on the boot black
 * -- one atomic whole-frame show per tick, no fade -- and ends on the
 * newest stored frame; the live-computed current phase lands as the
 * final frame right after. With no usable cache there is nothing to
 * play, and the now-frame's ~1.3-2.4 s bake would leave the panel dead
 * black -- the bake reveal (UI_MOON_BAKE_REVEAL, the reveal section)
 * makes that computation visible instead: the moon draws itself north
 * to south over black at the actual bake rate. With the replay off,
 * moon_ui_start bakes the current phase synchronously and the tick
 * displays it whole (the synodic boot sweep of the pre-2026-11-13 era
 * -- the last 29.5 days computed on the fly -- stayed removed; the
 * replay is pure playback of the hour cache; the reveal shows the REAL
 * bake, it is not a canned animation).
 *
 * Display invariant (the "no top-to-bottom" rule): every frame the
 * pipeline produces -- orbit sweep frame, re-bake, the hourly refresh,
 * and even a screen switch -- is displayed as ONE atomic whole-frame /
 * whole-panel lv_refr_now under the buffer mutex. A frame
 * must never be left for LVGL's async refresh to drain in 25-row strips
 * (that is the visible top-to-bottom crawl), and no pending full-screen
 * invalidation (e.g. a screen-load animation) may ride along in one of
 * these flushes -- a screen switch is a plain lv_scr_load + one atomic
 * whole-panel flush (moon_ui_screen_switched). The ONE sanctioned
 * exception: the hardware brightness fade (UI_SCREEN_SWITCH_FADE, reused
 * by the hourly refresh) -- a panel-brightness ramp that writes no
 * pixels, whose one atomic flush of the new frame runs while the panel
 * is dimmed to black. The SECOND sanctioned flow (user decision
 * 2026-11-14, docs/implementation/bake-reveal): the bake reveal --
 * during the current-phase bake the moon square arrives as a sequence
 * of atomic strip states (each strip's rows are fully baked, converted
 * and flushed as ONE strip-band lv_refr_now under the buffer mutex with
 * its drain witness; unbaked rows are black). Every DISPLAYED state is
 * still one atomic flush; the rule forbids a frame torn MID-ROW by the
 * async drain, not a designed sequence of consistent states.
 *
 * Once per hour the home's moon refreshes behind that fade (the hourly
 * section): the panel dims to black on its OWN brightness (0x51 / the
 * P4 backlight), a core-1 task produces the current-phase frame at the
 * HOUR CENTRE (the TF hour cache serves it when the card has it, else a
 * full bake with write-through store), the finished frame lands as ONE
 * atomic whole-panel flush, and the brightness ramps back. With the
 * bake reveal on (the cold-bake leg only -- a TF hit has nothing to
 * compute), the brightness ramp-back starts at the FIRST completed bake
 * strip, so the panel never sits black for the whole bake: the user
 * watches the moon being computed, strip by strip. Only the brightness
 * animates -- the clock, the
 * alarm row and the seconds keep running, and the refresh is skipped
 * while the device is idle, on a moon-less screen, or while another
 * pipeline / 0x51 ramp owns the buffers.
 *
 * Tilt input: the on-board QMI8658 (components/imu) is the ONLY input
 * to the moon (touch is the core-UI wake input, never a moon input).
 * The moon keeps the FIXED Earth viewport (the sub-Earth point is up,
 * posangle applied); the tilt changes only which side of the sphere the
 * camera sees (there is no in-plane rotation input):
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
 * the finished frame into the display as ONE atomic whole-frame flush
 * (MOON_ORBIT_BATCH = MOON_NSTRIPS strips), so the viewport change
 * lands as one ~150 ms frame instead of a progressive top-to-bottom
 * wipe over the bake.
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
 * only when needed.
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

#include "esp_lcd_panel_interface.h"    /* the full vtable struct (s_panel->draw_bitmap) */
#include "esp_lcd_panel_io_interface.h" /* esp_lcd_panel_io_t (the io base struct, for the trans-done witness chain) */
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
#if CONFIG_UI_MOON_TFCACHE
#include "freertos/queue.h"
#include "moon_tfcache.h" /* the hourly producer's fetch/store (hour keys) */
#endif

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
 * sdkconfig.h, so a #define-to-1 fallback would silently re-enable a
 * feature the build turned off. The
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
/* Once-per-hour moon refresh (the hourly section). All four fall back to
 * 0/off so a Kconfig "n" (which leaves the macro UNDEFINED) can never
 * re-enable a feature the build turned off -- the rule the orbit note
 * above documents. FADE_MS follows the screen-switch fade when Kconfig
 * leaves it at its default; it is only referenced while HOURLY_ENABLE is
 * on. HOURLY_TFCACHE gates the producer's TF hour-cache leg. */
#ifndef CONFIG_UI_MOON_HOURLY_ENABLE
#define CONFIG_UI_MOON_HOURLY_ENABLE 0
#endif
#ifndef CONFIG_UI_MOON_HOURLY_TFCACHE
#define CONFIG_UI_MOON_HOURLY_TFCACHE 0
#endif
#ifndef CONFIG_UI_MOON_HOURLY_FADE_MS
#define CONFIG_UI_MOON_HOURLY_FADE_MS CONFIG_UI_SCREEN_SWITCH_FADE
#endif
#ifndef CONFIG_TEST_HOURLY_FORCE_S
#define CONFIG_TEST_HOURLY_FORCE_S 0
#endif
/* The bake reveal (the reveal section): the current-phase bake displayed
 * AS it computes instead of a black window. Follows the hourly refresh
 * (its cold-bake leg and the boot replay's final leg are the two
 * revealed bakes); the 0 fallback follows the n-undefined rule the orbit
 * note documents. */
#ifndef CONFIG_UI_MOON_BAKE_REVEAL
#define CONFIG_UI_MOON_BAKE_REVEAL 0
#endif
/* Reveal pacing instrumentation (the TEST_BAKE_REVEAL_TRACE test hook, the
 * reveal section): producer-strip / displayed-band / gated-tick lines. The
 * 0 fallback follows the n-undefined rule; everything it gates compiles
 * out when off. */
#ifndef CONFIG_TEST_BAKE_REVEAL_TRACE
#define CONFIG_TEST_BAKE_REVEAL_TRACE 0
#endif
#ifndef CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC
#define CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC 30
#endif

/* The countdown is a runtime setting (the deep-sleep settings screen
 * overrides it and persists it); the Kconfig macro is only the
 * build-time default. (The store's dusk/boot animation flags are
 * legacy no-ops since 2026-11-13 -- nothing reads them.) */
/* Perf A/B hook (unset bools are absent from sdkconfig.h; fall back to
 * 0 = relief ON, the normal path). */
#ifndef CONFIG_TEST_BAKE_NO_RELIEF
#define CONFIG_TEST_BAKE_NO_RELIEF 0
#endif
static uint8_t *s_frame;      /* 8-bit baked frame, MOON_PX^2 */
static uint16_t *s_rgb;       /* RGB565 display buffer, MOON_PX^2 */
static lv_image_dsc_t s_dsc;  /* (MOON_PX), points at s_rgb */
static lv_obj_t *s_images[8]; /* 4 moon screens today; headroom for more */
static int s_n_images;
static lv_obj_t *s_moon_roots[8];          /* the screen root each image was created in */
static volatile bool s_moon_screen = true; /* the active screen carries a moon image */
static lv_timer_t *s_tick;
static bool s_ready;
static bool s_started; /* moon_ui_start() called; ticks may run */

static int64_t s_baked_day;   /* calendar day of the last static bake */
static int s_baked_event;     /* moon_event_signature of the last static bake's epoch */
static int64_t s_baked_hour;  /* utc/3600 of the last static bake (the blood-moon hour re-bake) */
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

/* Strips (25 rows) displayed per tick by the orbit sweep consumer
 * (MOON_NSTRIPS of them per atomic whole frame), so a tilt viewport
 * change lands as one atomic lv_refr_now frame (~175 ms measured for
 * the full 400-row frame, comfortably under the 5 s Task WDT). */
#define MOON_ORBIT_BATCH MOON_NSTRIPS
static SemaphoreHandle_t s_rgb_mtx; /* guards s_rgb (+ s_frame while baking) */

/* The panel vtable's draw_bitmap member (this IDF exposes the vtable
 * struct but no typedef for its member) -- the wrapper's type and the
 * saved original's type. */
typedef esp_err_t (*moon_panel_draw_t)(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                       const void *color_data);

static SemaphoreHandle_t s_panel_mtx;  /* every panel write: flush strips, 0x51 ramps, probes */
static esp_lcd_panel_handle_t s_panel; /* the CO5300 panel handle */
static moon_panel_draw_t s_orig_draw;  /* the panel's real draw_bitmap (the wrapper's callee) */
static volatile bool s_panel_wrap;     /* the draw_bitmap wrapper is installed */
/* Forward decl: the screen-switch fade / dim ramps (defined later) hold this
 * flag while their 0x51 brightness ramp runs, so the ramps are mutually
 * exclusive on the panel io (the fade section). */
static volatile bool s_fade_busy;
/* Forward decl: the hold arm's LVGL-task check (defined below). */
static bool moon_on_lvgl(void);
/* Forward decls (the fade section, defined after the tick + pause): the
 * hourly refresh's completion legs run on the tick (moon_tick_cb via
 * moon_hourly_service) and on sleep entry (moon_ui_pause), both ahead of
 * the fade machinery. fade_ramp took an explicit ms leg-time so the
 * hourly fade can pace independently of the screen-switch fade.
 * s_panel_pct / s_fade_pending are the fade section's panel bookkeeping
 * that the hourly probe / service gate on; tentative decls here, defined
 * (with the initializer) below. */
static void fade_ramp(int from_pct, int to_pct, int ms);
static void moon_hourly_finish(int show_new);
static bool s_fade_pending;
static volatile int s_panel_pct;

/* The sync-storm panel-traffic hold (moon_ui.h). The wake path's on-demand
 * WiFi sync brings the radio up HERE: esp_wifi_init loads the full
 * firmware into RAM and runs RF calibration -- a ~300 ms storm on a
 * prio-23 core-0 driver task, concurrent with live panel traffic (the
 * 10 ms tick flushes). The storm takes the completion
 * of an in-flight QSPI DMA: a lost completion parks the GPSPI bus lock
 * forever and the panel-io watchdog restarts the board ~6 s into the
 * wake (bench evidence, task #29: deterministic wake+6.29 s PANEL IO
 * STUCK on both DEEP tilt-wakes; boot -- where the storm lands before
 * LVGL exists -- never stalls). Writers are DROPPED, never blocked:
 * blocking a writer is the wake-freeze regression 0c35dd7 cured. LVGL
 * keeps drawing into its buffers, and the release queues a whole-screen
 * repaint that heals everything the dropped writes left behind.
 *
 * The LVGL flushes drop at a swapped-out flush_cb (hold_flush_cb), NOT
 * at the draw_bitmap wrapper: the esp_lvgl_adapter's double_direct
 * flush completes its LVGL hand only through the real transaction's
 * trans-done notify (its error path still blocks in the notify take),
 * so dropping a flush inside the wrapper leaves disp->flushing set and
 * wedges the LVGL task in wait_for_flushing forever -- IDLE0 WDT spam,
 * frozen screen (found by the task #29 proof hammer). Swapping the
 * flush_cb is the only seam that drops pixels without breaking the
 * hand, and the adapter is managed_components (public API only). */
#define PANEL_TRAFFIC_HOLD_MS 15000
/* Releasing at the sync task's END used to be the design -- the proof
 * hammer showed why it cannot be: the STA stays ASSOCIATED after
 * "sync complete" (the airplane hand-off never deinited), and the
 * connected modem's ~300 ms DTIM beacon wake steals QSPI DMA
 * completions just like the init storm does (cycle 1: the first
 * post-release flush lost its completion ~4 s past sync-complete,
 * stall signature exactly 3+3 s later). The sync task now brings the
 * radio fully DOWN (wifi_manager_deinit) BEFORE stamping this tail,
 * so the tail covers what its name claims: the driver teardown churn
 * the stop/deinit kicks off (bench: ~0.6-3.5 s). */
#define PANEL_TRAFFIC_TAIL_MS 4000
static volatile bool s_panel_hold;
static volatile TickType_t s_panel_hold_until;
static volatile bool s_panel_hold_release; /* sync ended, tail draining */
static volatile bool s_panel_repaint;      /* a whole-screen repaint is queued */

/* The hold's LVGL flush guard: renders complete, nothing hits the bus.
 * DROPPED flushes are counted and (first four) logged with their area:
 * this is the field evidence for the hold/heal bookkeeping (task #32) --
 * which pixels the panel never saw, and whether the restore repaint is
 * the only thing between that and a permanently stale screen. */
static volatile uint32_t s_hold_flush_drops;
static volatile uint32_t s_hold_flush_drops2; /* seam-2 (draw_wait) drops, same cap/log */
static volatile bool s_hold_pass_switch;      /* the atomic switch-frame window */
static lv_display_flush_cb_t s_flush_cb_real; /* captured at moon_ui_init */
/* The switch-frame pass (user 2026-11-16: "cannot enter settings from the
 * wifi screen with touch" -- bench RED 3/3, hub opened in LVGL in 20 ms
 * but the panel never showed it). Dropping the SCREEN-SWITCH frame was
 * never the hold's job: a screen switch is user presence, and freezing it
 * strands the panel on the stale frame until the release repaint -- the
 * bench measured 7 s (sync + 4 s tail), the user's "takes more than 2s /
 * does not work". moon_ui_screen_switched brackets its atomic lv_refr_now
 * legs with the flag below; the periodic repaints (seconds arc, clock
 * row) stay dropped and self-heal at the release repaint as designed.
 * The passed frame rides the storm bus -- the sanctioned-exception family
 * (0x51 ramp steps already do): task #31's bounded flush wait, task #32's
 * drain witness and the panel-io watchdog own a lost completion, one
 * frame per user action. */
static volatile uint32_t s_hold_pass_flushes;
static volatile bool s_hold_pass_pending; /* forwarded strips await their drain witness */
static void hold_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    if (s_hold_pass_switch && s_flush_cb_real) {
        const uint32_t n = ++s_hold_pass_flushes;
        if (n <= 4)
            ESP_LOGI(TAG, "flush PASSED (hold): switch frame strip (%ld,%ld)-(%ld,%ld)", (long)area->x1, (long)area->y1,
                     (long)area->x2, (long)area->y2);
        s_hold_pass_pending = true;          /* the strip goes to the bus: its drain
                                              * witness must run (rule 2 amendment) */
        s_flush_cb_real(disp, area, px_map); /* drives flushing/ready itself */
        return;
    }
    (void)px_map;
    const uint32_t n = ++s_hold_flush_drops;
    if (n <= 4)
        ESP_LOGI(TAG, "flush DROPPED (hold) #%u: (%ld,%ld)-(%ld,%ld)", (unsigned)n, (long)area->x1, (long)area->y1,
                 (long)area->x2, (long)area->y2);
    lv_display_flush_ready(disp);
}

/* Probe wrapper around the adapter's flush: logs every flush that really
 * goes to the bus, with the hold flag as seen at entry. A real flush at
 * hold=1 is the unguarded pre-arm window (an arm from a non-LVGL task
 * leaves the guard swap one tick late -- while the LVGL task is busy in
 * exactly this flush); a drop at hold=1 with no later restore+repaint is
 * a screen the panel never got. */
static void flush_cb_probe(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    ESP_LOGI(TAG, "flush: (%ld,%ld)-(%ld,%ld) hold=%d", (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
             (int)s_panel_hold);
    if (s_flush_cb_real)
        s_flush_cb_real(disp, area, px_map);
}

/* Bounded, yielding replacement for LVGL's unbounded flush wait
 * (task #31, the 2026-11-12 "touch is very unresponsive on the AMOLED").
 * The hold guard covers flushes it sees, but disp->flushing can already
 * be set when it swaps in: the sync screen's own pre-arm switch flush
 * loses its trans-done hand to the very radio session the hold was
 * armed for (bench: failed boot sync into a marginal AP -- the atomic
 * switch flush completed at the DRIVER level, the adapter's LVGL hand
 * never landed). LVGL's default wait_for_flushing then spins without
 * yielding on whichever task enters it -- the LVGL task (its indev
 * poll dies with it: touch goes dead) and, until the boot sweep
 * retired, the sweep task (lv_refr_now at frame show). No mutex is
 * held, so the panel watchdog is blind;
 * the task WDT only prints, it cannot free the board.
 *
 * The replacement gates on what actually matters: ALL panel traffic
 * (adapter flushes, 0x51 ramp steps, drain probes) serializes under
 * s_panel_mtx
 * and moon_panel_draw_wait consumes the trans-done INSIDE it -- so a
 * free mutex means the flush physically landed, wherever the LVGL
 * hand above the seam went. Return (LVGL clears disp->flushing itself
 * when a wait cb is installed and returns) and the refr pass
 * continues into flush_cb -- under a hold that is the stub, so the
 * screen self-heals exactly like the sync-storm design intends. The
 * 2 s cap only fires on a genuinely stuck bus, where it logs and lets
 * LVGL proceed (the panel-io watchdog keeps owning the true-stall
 * case: still s_panel_mtx-stuck, still 3 s). On the normal path this
 * callback never even runs: wait_for_flushing calls it only when
 * disp->flushing is still set, which it never is when the adapter
 * keeps up. */
#define FLUSH_WAIT_TIMEOUT_MS 2000
static void moon_flush_wait_cb(lv_display_t *disp) {
    (void)disp; /* LVGL clears disp->flushing itself once we return */
    if (!s_panel_mtx) {
        return; /* pre-init: nothing to gate on */
    }
    if (xSemaphoreTake(s_panel_mtx, pdMS_TO_TICKS(FLUSH_WAIT_TIMEOUT_MS)) == pdTRUE) {
        xSemaphoreGive(s_panel_mtx); /* observed quiescence only */
        return;                      /* the flush physically landed */
    }
    ESP_LOGE(TAG,
             "panel flush wait: bus still busy >%d ms (lost trans-done hand, the "
             "task #29/#30 completion-thief class); proceeding",
             FLUSH_WAIT_TIMEOUT_MS);
}

static bool panel_hold_live(void) {
    if (!s_panel_hold)
        return false;
    if ((int32_t)(xTaskGetTickCount() - s_panel_hold_until) >= 0) {
        bool drained = s_panel_hold_release; /* normal tail drain vs missed release */
        s_panel_hold = false;
        s_panel_hold_release = false;
        s_panel_repaint = true;
        s_hold_pass_pending = false; /* never carry a passed strip into a later hold */
        if (drained)
            ESP_LOGI(TAG, "panel traffic hold OFF (radio tail drained, repaint queued)");
        else
            ESP_LOGW(TAG, "panel traffic hold expired (missed release); writes resume");
        return false;
    }
    return true;
}

/* Extended arm/release: the self-heal cap and the release tail are
 * parameters so the WiFi-scan session (screens.c) can arm with a cap
 * that covers its 0-AP four-retry worst case (~25 s) and release TAIL-
 * FREE: the scan task has fully stopped/deinited the radio before it
 * completes -- no associated station is left to steal completions, so
 * the 4 s DTIM-beacon guard the sync path needs would only delay the
 * scan-result reveal. tail_ms = 0 releases on the next UI tick. */
/* Sessions may overlap (a wake/hourly sync arming while the user's scan
 * still runs): each arm takes a reference and a release drops one, so
 * the shorter session's release cannot end the hold while the longer
 * session still owns it. */
static volatile int s_panel_hold_refs;
void moon_ui_panel_traffic_hold_ext(bool hold, int cap_ms, int tail_ms) {
    if (hold) {
        const TickType_t want = xTaskGetTickCount() + pdMS_TO_TICKS(cap_ms);
        const bool was_live = panel_hold_live(); /* expiry self-heals */
        s_panel_hold_refs++;
        if (was_live) {
            /* Another session holds the bus frozen already: extend the
             * deadline and undo a stamped release (a live session needs
             * the freeze; its own release will stamp later). */
            if ((int32_t)(want - s_panel_hold_until) > 0)
                s_panel_hold_until = want;
            s_panel_hold_release = false;
            return;
        }
        ESP_LOGI(TAG, "panel traffic hold ON (%d ms cap)", cap_ms);
        s_hold_flush_drops = 0;
        s_hold_flush_drops2 = 0;
        s_hold_pass_pending = false; /* the hold always starts with a clean drain debt */
        s_panel_hold_release = false;
        s_panel_hold_until = want;
        s_panel_hold = true;
        /* Arm QUIESCE (the task #29 hammer's first cycle: a lost
         * completion despite the flags being set -- the victim was a
         * DMA already IN FLIGHT at arm time). A flush strip queued up to
         * 125 ms earlier completes asynchronously AFTER its wrapper left
         * the mutex, so the hold flags cannot drop it; and the guard
         * swap in the tick poll is one tick late, leaving the sync
         * screen's own switch flush able to queue into the storm. On the
         * LVGL task (every caller today) install the flush guard HERE --
         * that switch flush completed synchronously in this tick's
         * lv_refr_now before the arm -- then hold the panel mutex for
         * one DMA drain so whoever queued last completes on the
         * still-quiet bus before the kickoff's esp_wifi_init. A
         * non-LVGL arm keeps the one-tick guard latency. */
        if (s_ready && moon_on_lvgl()) {
            lv_display_t *d = lv_display_get_default();
            if (d && s_flush_cb_real && lv_display_get_flush_cb(d) != hold_flush_cb)
                lv_display_set_flush_cb(d, hold_flush_cb);
        }
        if (s_panel_mtx && xSemaphoreTake(s_panel_mtx, pdMS_TO_TICKS(500)) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(20)); /* one DMA drain, bus quiet */
            xSemaphoreGive(s_panel_mtx);
        }
    } else {
        if (s_panel_hold_refs > 0)
            s_panel_hold_refs--;
        if (s_panel_hold_refs > 0)
            return; /* another session still owns the hold */
        if (s_panel_hold && !s_panel_hold_release) {
            /* Session ended: keep the freeze through the radio tail (the
             * completion-thieving teardown/churn runs AFTER the last
             * activity); the UI-tick poll resumes writes at the deadline
             * (the expiry path clears the hold and queues the repaint). */
            s_panel_hold_release = true;
            TickType_t tail = xTaskGetTickCount() + pdMS_TO_TICKS(tail_ms);
            if ((int32_t)(tail - s_panel_hold_until) < 0)
                s_panel_hold_until = tail;
            ESP_LOGI(TAG, "panel traffic hold: release stamped, %d ms radio tail", tail_ms);
        }
    }
}

void moon_ui_panel_traffic_hold(bool hold) {
    moon_ui_panel_traffic_hold_ext(hold, PANEL_TRAFFIC_HOLD_MS, PANEL_TRAFFIC_TAIL_MS);
}

bool moon_ui_panel_repaint_pending(void) {
    /* LVGL task (the UI tick): drives the flush guard swap, then hands
     * out the queued whole-screen repaint. Swapping only here keeps all
     * flush_cb mutation on the task that runs the flushes; the arm
     * latency is one tick (~10 ms) after the hold ON, comfortably ahead
     * of the storm's first concurrent flush (the driver task starts
     * ~100 ms after the kickoff). */
    lv_display_t *d = lv_display_get_default();
    if (!d || !s_flush_cb_real)
        return false;
    if (panel_hold_live()) {
        if (lv_display_get_flush_cb(d) != hold_flush_cb)
            lv_display_set_flush_cb(d, hold_flush_cb);
        return false;
    }
    if (lv_display_get_flush_cb(d) == hold_flush_cb) {
        lv_display_set_flush_cb(d, flush_cb_probe); /* restore */
        ESP_LOGI(TAG, "flush guard restored (%u flushes dropped by the hold)", (unsigned)s_hold_flush_drops);
        s_panel_repaint = true;
    }
    if (!s_panel_repaint)
        return false;
    s_panel_repaint = false;
    return true;
}

/* The panel-io watchdog. Every QSPI panel write goes through the panel io's
 * ONE spi_device and the GPSPI bus lock: if a queued DMA transfer's
 * completion is ever lost (the CO5300 + QSPI driver has lost one on this
 * board -- the deterministic repro raced the old spin's strip writes
 * against a whole-panel flush), the bus lock's background (REQ/PEND) bit
 * is never cleared, and EVERY later spi_device_acquire_bus -- LVGL's
 * flushes, the 0x51 brightness writes -- blocks forever
 * in dev_wait (portMAX_DELAY), with the mutex still held: a silent,
 * no-WDT, permanent freeze (two tasks parked on one bus lock, no holder).
 * s_panel_mtx is held only inside a write (a strip ~10 ms, the boot or
 * whole-panel refresh flush <500 ms), so a hold longer
 * than the limit below can ONLY mean the bus is stuck -- no legitimate
 * path waits that long (the holders are the LVGL task's flushes and
 * atomics, the drain probes and the 0x51 ramp steps, none for seconds).
 * The watchdog takes the mutex and, when the
 * wait overruns, restarts: a clean reboot instead of a dead screen. */
#define PANEL_IO_WATCHDOG_LIMIT_MS 3000
static TaskHandle_t s_panel_watchdog;
static volatile bool s_panel_watchdog_armed; /* moon_ui_init succeeded */

/* The storm-pressure NO_MEM family (field 2026-11-17, the wifi-screen wedge):
 * the flush strip is a PSRAM buffer, so spi_master's setup_priv_desc bounces
 * every strip through a CONTIGUOUS 23,300-B internal-RAM DMA block. While
 * esp_wifi_init loads the radio firmware the largest free DMA block drops
 * under that size (bench: 100×ESP_ERR_NO_MEM, largest-DMA 20,480 B) and
 * every strip is rejected at the door -- while the bus itself stays provably
 * healthy (each attempt's CASET/RASET polling commands land; only the bounce
 * allocation dies). The old loop burned its 100 attempts (~1.5 s/strip) with
 * the mutex held, tripped the 3 s watchdog, and the board restarted under
 * the false "a DMA completion was lost" diagnosis. NO_MEM is transient:
 * retry out the storm (radio init/cal/connect/teardown) instead, and while
 * this flag names the reason the watchdog LOGS instead of restarting -- a
 * truly wedged queue (any other error, or no progress after the budget)
 * still owns the restart. */
#define PANEL_NO_MEM_RETRY_MS 8000
/* Give-up cap: three strips may ride a storm out (the normal case is ONE
 * long ride ending at the radio teardown; three covers staggered memory
 * return). The fourth give-up means the memory is not coming back -- the
 * classic 100-attempt cap and the watchdog restart resume, and the hold's
 * release repaint heals the gaps. Cleared by the next accepted strip. */
#define PANEL_NO_MEM_GIVEUP_CAP 3
static volatile bool s_panel_no_mem;
static volatile uint32_t s_nomem_giveups;

/* The draw_bitmap wrapper (installed on the panel vtable at init):
 * LVGL's flushes (and the moon pipeline's own lv_refr_now paths)
 * reach the panel here. Under the panel mutex it calls the real
 * draw_bitmap (via the accept-retry below). The buffer arrives in PANEL
 * byte order: the adapter bridge (ESP_LV_ADAPTER_PANEL_IF_OTHER) calls
 * lv_draw_sw_rgb565_swap on the flush buffer before draw_bitmap. This
 * is what keeps every panel writer -- LVGL's flushes, the drain probes,
 * the 0x51 ramp steps -- serialized on ONE lock at the panel boundary. */
/* The real draw_bitmap, retried until the panel accepts the write. The P4's
 * DPI panel copies each strip with an ASYNC DMA2D (its draw semaphore is
 * held until the copy lands): back-to-back calls return
 * ESP_ERR_INVALID_STATE ("previous draw operation is not finished") and the
 * strip is DROPPED. The S3's QSPI panel writes synchronously, so the first
 * attempt always succeeds and the retry costs nothing there. Callers must
 * hold s_panel_mtx. */
/* Last-accepted-write witness (lost-completion family): when a write
 * finally gives up, the stuck depth-1 queue slot belongs to whoever was
 * accepted LAST, not to the task that notices it. Logging the victim
 * (task + y) alone sent two captures chasing the wrong writer; the
 * accepted-write line names the culprit -- and the drain-verify pixel
 * is unmistakable in it (y 465..466). Plain statics: log-only, a torn
 * read across the writers costs nothing. */
static volatile uint32_t s_last_accept_tick;
static char s_last_accept_desc[40];

/* strict=true (drain probes only): submit to the bus even under a live
 * hold. A drain has already decided the queue must be exercised; under
 * a hold the write simply queues behind the storm's traffic, and the
 * hold shortcut would turn the probe into a phantom (accepted without
 * ever touching the bus -- the capture-8 hold series). */
static esp_err_t moon_panel_draw_wait(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                      const void *color_data, bool strict) {
    if (!strict && panel_hold_live() && !s_hold_pass_switch) {
        /* Seam-2 field evidence. The switch-frame pass (s_hold_pass_switch,
         * screen_switched's atomic window) is exempt: a screen switch is
         * user presence -- dropping its frame strands the panel on the
         * stale frame until the release repaint (the user's "touch does
         * nothing"). A switch strip dying here would be the anomaly --
         * the gate excludes the pass window, so it cannot (the bus-side
         * failure it would have caught is the rejected-attempt log
         * below). */
        if (s_hold_flush_drops2 < 4)
            ESP_LOGI(TAG, "strip dropped at draw_wait (hold) #%u: task %s (%d,%d)-(%d,%d)",
                     (unsigned)(s_hold_flush_drops2 + 1), pcTaskGetName(NULL), x_start, y_start, x_end, y_end);
        s_hold_flush_drops2++;
        return ESP_OK; /* dropped: the sync storm owns the bus (the hold) */
    }
    /* Diagnostic tally (wifi-sync wedge hunt): which esp_err_t the io
     * layer ACTUALLY returned is the discriminator between the two
     * suspected families -- ESP_ERR_NO_MEM from setup_priv_desc's
     * internal-RAM DMA bounce (radio storm squeezes internal heap) vs
     * ESP_ERR_INVALID_STATE from the "previous polling transaction not
     * terminated" check / a truly parked depth-1 slot (the lost
     * completion family). The give-up log names the codes + counts. */
    esp_err_t tally_code[4] = {0};
    uint32_t tally_n[4] = {0};
    int tally_cnt = 0;
    const uint32_t wait_t0 = lv_tick_get();
    s_panel_no_mem = false;
    for (int attempt = 0; attempt < 100 || s_panel_no_mem; attempt++) {
        esp_err_t r = s_orig_draw(panel, x_start, y_start, x_end, y_end, color_data);
        if (r == ESP_OK) {
            s_panel_no_mem = false;
            s_nomem_giveups = 0; /* the bus is flowing again: fresh storm budget */
            snprintf(s_last_accept_desc, sizeof(s_last_accept_desc), "%s y%d..%d", pcTaskGetName(NULL), y_start, y_end);
            s_last_accept_tick = lv_tick_get();
            return ESP_OK;
        }
        {
            int i;
            for (i = 0; i < tally_cnt; i++)
                if (tally_code[i] == r) {
                    tally_n[i]++;
                    break;
                }
            if (i == tally_cnt && tally_cnt < 4) {
                tally_code[i] = r;
                tally_n[i] = 1;
                tally_cnt++;
            }
        }
        /* Storm-pressure NO_MEM is transient (see PANEL_NO_MEM_RETRY_MS):
         * ride it out past the 100-attempt cap and keep the watchdog off
         * the restart path while it is the named reason. ANY other error
         * (the not-terminated-polling check, a truly parked depth-1 slot)
         * drops the flag and leaves the loop under the classic cap -- the
         * drain witness / heal / restart ladder owns those. */
        s_panel_no_mem = (r == ESP_ERR_NO_MEM) && s_nomem_giveups < PANEL_NO_MEM_GIVEUP_CAP &&
                         ((int32_t)(lv_tick_get() - wait_t0) < PANEL_NO_MEM_RETRY_MS);
        if (r == ESP_ERR_NO_MEM) {
            static uint32_t s_nomem_log;
            const uint32_t now = lv_tick_get();
            if (now - s_nomem_log >= 2000) {
                s_nomem_log = now;
                ESP_LOGW(TAG, "strip bounce no-mem (storm pressure): task %s (y %d..%d), largest-DMA %u B -- riding out",
                         pcTaskGetName(NULL), y_start, y_end,
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
            }
        }
        if (attempt == 0) {
            /* Anomaly-only attribution (2026-11-13 wake wedge): a FIRST
             * attempt that fails means the depth-1 flush queue slot is
             * already held -- the previous strip's trans-done has not
             * landed (legitimately in flight, or lost). Task + y tell the
             * wedge timeline apart; P4's async DMA2D rejects are routine
             * here, so the line is rate-limited, not per-strip. */
            static uint32_t s_reject0_log;
            const uint32_t now = lv_tick_get();
            if (now - s_reject0_log >= 5000) {
                s_reject0_log = now;
                ESP_LOGW(TAG, "panel draw rejected attempt 0: task %s (y %d..%d)", pcTaskGetName(NULL), y_start, y_end);
            }
        }
        vTaskDelay(1); /* a full tick: pdMS_TO_TICKS(1) = 0 ticks at HZ=100
                        * made this a 100-attempt tight spin, not a backoff */
    }
    /* ~100 ms is stale: each rejected attempt costs the io onFly poll
     * (~10 ms) plus the delay tick, so the loop is ~2 s per strip. */
    s_panel_no_mem = false; /* give-up: the watchdog owns the case again */
    if (tally_code[0] == ESP_ERR_NO_MEM)
        s_nomem_giveups++; /* the ride-out cap counts NO_MEM give-ups only */
    ESP_LOGW(TAG,
             "panel draw not accepted: task %s (y %d..%d); last accepted: %s @ %lu ms ago (the culprit); "
             "io errors: %d×0x%x %d×0x%x %d×0x%x %d×0x%x; largest-DMA %u B",
             pcTaskGetName(NULL), y_start, y_end, s_last_accept_desc,
             (unsigned long)(lv_tick_get() - s_last_accept_tick), (int)tally_n[0], (int)tally_code[0],
             (int)tally_n[1], (int)tally_code[1], (int)tally_n[2], (int)tally_code[2], (int)tally_n[3],
             (int)tally_code[3], (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    return ESP_ERR_TIMEOUT;
}

/* The wrapper body with the panel mutex ALREADY TAKEN by the caller.
 * moon_panel_drain_verify holds s_panel_mtx across its whole
 * submit+witness window so the probe's transaction descriptor is the
 * only io traffic while its completion is pending -- concurrent
 * writers could recycle it (the driver memsets the pool slot before
 * reuse) and silently eat the per-transaction completion flag before
 * the ISR-side post_cb reads it. strict passes through to draw_wait. */
static esp_err_t moon_panel_draw_bitmap_locked(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                               const void *color_data, bool strict) {
    (void)panel;
    return moon_panel_draw_wait(panel, x_start, y_start, x_end, y_end, color_data, strict);
}

static esp_err_t moon_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
                                        const void *color_data) {
    /* No hold gate HERE: a vtable caller (the adapter's blit) expects the
     * transaction's trans-done notify, so a drop at this seam breaks its
     * hand. LVGL flushes drop at hold_flush_cb instead; during a hold
     * nothing reaches this wrapper except the switch-frame pass
     * (s_hold_pass_switch -- screen_switched's atomic window). */
    xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
    esp_err_t r = moon_panel_draw_bitmap_locked(panel, x_start, y_start, x_end, y_end, color_data, false);
    xSemaphoreGive(s_panel_mtx);
    return r;
}

/* The trans-done witness (lost-completion family, task #32 finale).
 * A drain probe's ACCEPT proves the previous write (a depth-1 queue
 * accepts only when empty) but can never prove the probe pixel's own
 * completion, and no chain of probes can: the next writer that would
 * prove a tail pixel is itself the frozen victim. Capture 5 proved
 * exactly that -- the wake flush's strip 1 froze on the preflush
 * probe's own pending pixel ("last accepted: lvgl y465..466 ... (the
 * culprit)"), three boots, same signature. The proof must therefore
 * come from the driver: the io's on_color_trans_done is the very
 * completion whose loss starts the family. It is a single slot, and
 * the esp_lvgl_adapter bridge (PANEL_IF_OTHER's default branch) owns
 * it from bsp_display_start -- its handler IS this display's
 * flush-ready in NONE tear-avoid mode (display_bridge_v9_handle_vsync
 * -> display_manager_flush_ready -> lv_display_flush_ready;
 * dummy-draw off, no TE), so REPLACING it wedges LVGL's refr loop.
 * moon_ui_init runs after the adapter, so instead of replacing, the
 * witness CHAINS: it reads the bridge's {fn, ctx} off the io,
 * registers its own counting callback, and forwards every completion
 * into the bridge's handler untouched. The witness counts every color
 * completion on the single QSPI spi_device -- the drain's depth-1
 * FIFO argument then makes "an increment after my post-accept
 * snapshot" equivalent to "my own done landed": any later writer's
 * accept required my completion first, and its completion callback
 * is itself the increment. */
#if !defined(CONFIG_BOARD_P4)
/* The private half of esp_lcd_panel_io_spi_t (IDF v5.5.1,
 * components/esp_lcd/spi/esp_lcd_panel_io_spi.c) -- the driver stores
 * the registered callback pair there and the public API offers no
 * read-back. These two fields are only READ (registration still goes
 * through esp_lcd_panel_io_register_event_callbacks). The layout is
 * pinned by the IDF v5.5.1 checkout the builds use; drift shows up
 * LOUD, never silently: moon_panel_witness_arm's boot-time arm test
 * either fails to see a single increment (witness disabled, drain
 * falls back to accept-only) or faults immediately at boot -- both on
 * the bench, never in the field. */
typedef struct {
    esp_lcd_panel_io_t base;
    void *spi_dev; /* spi_device_handle_t */
    size_t spi_trans_max_bytes;
    int dc_gpio_num;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done; /* the bridge's handler */
    void *user_ctx;                                             /* ...and its impl ctx */
} moon_panel_io_spi_view_t;

static volatile uint32_t s_panel_done_cnt;
static esp_lcd_panel_io_color_trans_done_cb_t s_prev_trans_done;
static void *s_prev_trans_done_ctx;
static bool s_panel_witness; /* armed by moon_panel_witness_arm() */

static bool IRAM_ATTR moon_panel_trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                                               void *user_ctx) {
    (void)user_ctx;
    s_panel_done_cnt++;
    if (s_prev_trans_done)
        return s_prev_trans_done(io, edata, s_prev_trans_done_ctx);
    return false;
}

static void moon_panel_witness_arm(void) {
    esp_lcd_panel_io_handle_t io = board_panel_io();
    if (!io) {
        ESP_LOGW(TAG, "trans-done witness: no panel io handle (drain stays accept-only)");
        return;
    }
    const moon_panel_io_spi_view_t *v = (const moon_panel_io_spi_view_t *)io;
    s_prev_trans_done = v->on_color_trans_done; /* the adapter bridge's handler */
    s_prev_trans_done_ctx = v->user_ctx;
    static const esp_lcd_panel_io_callbacks_t witness_cbs = {
        .on_color_trans_done = moon_panel_trans_done_cb,
    };
    if (esp_lcd_panel_io_register_event_callbacks(io, &witness_cbs, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "trans-done witness: registration failed (drain stays accept-only)");
        return;
    }
    /* Arm test: one drain-ROW write must bump the counter ITSELF --
     * proving struct view, slot ownership and ISR reachability at
     * BOOT, where failure is a log line, not a sleep transition. Capture 6
     * proved the old "any increment counts" test worthless: ambient
     * LVGL flushes carried it while the probe pixel itself never
     * completed. The row is DMA-capable DRAM, single chunk -- the
     * exact shape whose completion the witness must trust. Serialized
     * exactly like the drain (mutex across submit+poll, strict submit):
     * an ambient flush racing in here could recycle the descriptor and
     * eat the completion, failing the arm on a healthy chain. */
    static DRAM_ATTR uint16_t s_arm_row[BSP_LCD_H_RES];
    xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
    const uint32_t before = s_panel_done_cnt;
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_err_t arm =
        moon_panel_draw_bitmap_locked(s_panel, 0, BSP_LCD_H_RES - 1, BSP_LCD_H_RES, BSP_LCD_H_RES, s_arm_row, true);
    bool arm_seen = false;
    if (arm == ESP_OK) {
        for (int i = 0; i < 30 && !arm_seen; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            arm_seen = (s_panel_done_cnt != before);
        }
    }
    xSemaphoreGive(s_panel_mtx);
    if (arm != ESP_OK) {
        ESP_LOGW(TAG, "trans-done witness: arm write rejected (drain stays accept-only)");
        return;
    }
    if (!arm_seen) {
        ESP_LOGE(TAG, "trans-done witness: arm row never completed (drain stays accept-only)");
        return;
    }
    s_panel_witness = true;
    ESP_LOGI(TAG, "panel trans-done witness armed (chained through the LVGL adapter bridge)");
}
#endif /* !CONFIG_BOARD_P4 */

/* Drain recovery (task #32, lost-completion family). A drain loss means
 * the depth-1 flush queue holds a write whose trans-done will never
 * land: the alternative to recovering HERE is the next writer pinned at
 * ~2 s per strip (capture 2: the LVGL task after the wake flush;
 * capture 3: the spin task 120 ms after "sweep 5/5 shown") until the
 * panel watchdog restarts -- naming the VICTIM, not the flush that lost
 * the hand. So restart at the site. The RTC_NOINIT budget survives
 * esp_restart (warm restarts keep RTC fast memory; a cold boot reads
 * garbage, caught by the magic): if a regression ever turns the witness
 * into a deterministic boot loop, restarts stop after the budget and
 * the boot proceeds degraded -- a working clock beats a restart storm,
 * and the field log still carries every loss line. */
#define DRAIN_RESTART_BUDGET 3
#define DRAIN_BOOT_MAGIC 0xD2A1CE00u
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint32_t used;
} s_drain_boot;

static void drain_recovery(const char *site, int probe, const char *why) {
    if (s_drain_boot.magic != DRAIN_BOOT_MAGIC) { /* cold boot: contents are garbage */
        s_drain_boot.magic = DRAIN_BOOT_MAGIC;
        s_drain_boot.used = 0;
    }
    if (s_drain_boot.used >= DRAIN_RESTART_BUDGET) {
        ESP_LOGE(TAG,
                 "drain verify after %s (probe %d): %s -- restart budget (%d) exhausted this boot; "
                 "continuing degraded (the panel watchdog only catches a stuck BUS, not this)",
                 site, probe, why, DRAIN_RESTART_BUDGET);
        return;
    }
    s_drain_boot.used++;
    ESP_LOGE(TAG,
             "drain verify after %s (probe %d): %s; recovering here instead of pinning the next writer "
             "[restart %u/%u this boot]",
             site, probe, why, (unsigned long)s_drain_boot.used, DRAIN_RESTART_BUDGET);
    vTaskDelay(pdMS_TO_TICKS(100)); /* let the log flush out the UART */
    esp_restart();                  /* never returns while budget remains */
}

/* Drain-verify after a sanctioned atomic whole-panel flush. lv_refr_now
 * returns with the LAST strip's DMA still in flight -- and when that one
 * trans-done is lost, the depth-1 flush queue stays occupied invisibly
 * until the NEXT writer hits it. One full-width 1-row drain write at the
 * AMOLED's bottom edge (y 465: outside the moon window -- mirror
 * untouched -- outside every widget, black on AMOLED black) forces that
 * hand to be consumed NOW, at the flush site; the 20 ms lead-in makes
 * the healthy path an attempt-0 accept (a strip DMAs in ~10 ms). The
 * probe's OWN completion is then witnessed through the chained io
 * callback above: accepted-but-never-completed is exactly the family
 * signature, and it is now visible. S3 only: the lost-completion family
 * is the GPSPI driver's; the P4 DSI path keeps its proven behaviour
 * untouched.
 *
 * Three hardening rules, each bought with a bench capture:
 *
 * 1. SHAPE (capture 7): the probe must be a DMA-capable single-chunk
 *    write. The original 2-byte flash (DROM) probe pixel was the ONLY
 *    color write on the bus whose completion never fired at ALL sites
 *    and boots (strips 19/19 per frame, probe 0/2) -- spi_master
 *    heap-bounces a flash buffer per write and the io color callback
 *    then never reaches us. The row (DRAM, 466 px, 932 B, one chunk --
 *    the exact strip shape) completes. The arm test (above) demands the
 *    row's own completion at BOOT, where failure is a log line.
 *
 * 2. HOLD (capture 8): under a live panel hold, the flush the drain
 *    follows was itself DROPPED (hold_flush_cb / draw_wait's shortcut
 *    never touched the bus) -- there is no hand to consume, and probing
 *    anyway rides draw_wait's hold shortcut into a phantom accept
 *    ("accepted" without ever queuing; counter cannot move; four boots
 *    logged it at switch fade-in / screen switch). The drain no-ops
 *    while the hold is live; the hold's heal repaint owns display
 *    recovery. Probes that start healthy submit STRICT (bypass the
 *    shortcut) so a hold beginning mid-drain can never phantom-accept.
 *
 * 3. SERIALIZATION (captures 6-9): the probe's transaction descriptor
 *    must be the ONLY io traffic while its completion is pending. The
 *    per-transaction completion flag lives in the depth-1 pool
 *    descriptor and the next writer memsets that slot before reuse
 *    (esp_lcd_panel_io_spi.c, en_trans_done_cb); a concurrent writer
 *    slipping in during the probe's ~10 ms flight -- a fade-ramp 0x51
 *    tx_param, an LVGL strip -- recycles the descriptor
 *    under the ISR and silently eats the flag. That is the impossible
 *    triangle resolved: the probe is genuinely accepted (the accept
 *    bookkeeping is desynced by the recycle path), its own callback
 *    never fires, and the following writer is fine. Captures 6-9 froze
 *    exactly the drains whose sites share the bus with other writers,
 *    and only there; the RTC-FAST mirror + throttled ISR log (capture 9)
 *    proved the callback side, not the poller, is dead. So the drain
 *    now holds s_panel_mtx across the WHOLE submit+witness window (no
 *    drain call site holds it -- audited 2026-11-13); the hold is a few
 *    tens of ms (bench note below), a fraction of the 3 s + 3 s panel
 *    watchdog. Serialization still does NOT prevent the loss
 *    (it lives inside the GPSPI/io driver -- see the heal branch), but
 *    it makes the witness AIRTIGHT: with the queue exclusive, an
 *    accepted probe whose own completion stays silent is the driver
 *    eating its flag -- healed in place; a probe that cannot even be
 *    accepted is a wedged queue -- restarted at the site (budget
 *    above).
 *
 *    The anomaly is the NORMAL case, not the rare one (bench
 *    2026-11-15, bake-reveal trace): every probe that follows a
 *    multi-strip lv_refr_now freezes its own completion -- 6/6 on the
 *    reveal legs, and the capture-7-era boot logs already carried
 *    "strips 19/19 per frame, probe 0/2". The old ladder burned two
 *    300 ms poll windows (a de-facto ~640 ms per verified flush) before
 *    the same heal; the witness now grants ONE 30 ms grace (a genuinely
 *    delivered completion lands within ~1-10 ms, per the arm-row test)
 *    and heals straight away -- ~50 ms. The second probe's
 *    disambiguation (a depth-1 accept as proof the first completed) is
 *    moot: the heal is the driver's own drain loop, a safe no-op over
 *    bookkeeping that in fact completed; a merely-LATE completion is
 *    reaped by that loop instead of ending in a false "swallowed" log. */
static void moon_panel_drain_verify(const char *site) {
#if !defined(CONFIG_BOARD_P4)
    static DRAM_ATTR uint16_t s_drain_row[BSP_LCD_H_RES];
    /* Rule 2 (amended 2026-11-16 with the switch-frame pass): under a live
     * hold the preceding flush was dropped -- UNLESS it was a switch frame
     * that passed the guard (s_hold_pass_pending: forwarded at hold_flush_cb,
     * released at draw_wait). A passed strip has a REAL trans-done hand to
     * consume, so the witness runs; a dropped one has none (the capture-8
     * phantom accept). */
    if (!s_hold_pass_pending && panel_hold_live())
        return; /* rule 2: the preceding flush never reached the bus */
    s_hold_pass_pending = false;
    xSemaphoreTake(s_panel_mtx, portMAX_DELAY); /* rule 3, see audit note above */
    /* One probe, one short grace, straight to the heal (see the header
     * note: post-flush probes swallow deterministically). */
    /* 20 ms lead-in: a strip DMAs in ~10 ms, so a healthy queue is
     * free by the time the probe reaches it (attempt-0 accept). */
    vTaskDelay(pdMS_TO_TICKS(20));
    /* s_panel, NOT NULL: the wait loop calls the real draw_bitmap
     * (s_orig_draw) on this handle -- a NULL here is a core-0
     * LoadProhibited (EXCVADDR 0x2c, the co5300 panel ops deref), which
     * is what the first drain-verify build did on its very first dusk
     * frame. The direct-write seam always passes the real handle.
     * strict: see rule 2 -- no phantom accepts under a late hold. */
    esp_err_t drain =
        moon_panel_draw_bitmap_locked(s_panel, 0, BSP_LCD_H_RES - 1, BSP_LCD_H_RES, BSP_LCD_H_RES, s_drain_row, true);
    if (drain != ESP_OK) {
        drain_recovery(site, 0, "the pending write never released the depth-1 queue");
        xSemaphoreGive(s_panel_mtx); /* only reached on budget exhaustion */
        return;
    }
    if (!s_panel_witness) {
        xSemaphoreGive(s_panel_mtx);
        return; /* witness not armed: accept-only, the capture-4 behaviour */
    }
    const uint32_t c0 = s_panel_done_cnt;
    for (int i = 0; i < 3 && s_panel_done_cnt == c0; i++)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (s_panel_done_cnt != c0) {
        xSemaphoreGive(s_panel_mtx);
        return; /* this probe's own completion was witnessed */
    }
    /* The queue-exclusive probe was accepted yet its own callback
     * stayed silent. The driver's own arithmetic then pins the
     * loss: ret_queue delivery and post_cb run back-to-back in the
     * ISR (spi_master.c, spi_intr: post then send), so "no
     * callback" cannot mean "no completion" -- what died is the
     * per-transaction en_trans_done_cb flag, memset by the io
     * driver's unconditional pool[0] recycle
     * (esp_lcd_panel_io_spi.c:224) whenever a stale ret_queue
     * entry (the polling-command double-post -- the GPSPI ISR has
     * no host->polling guard) let the inflight bookkeeping run
     * ahead. That is the lost-trans-done family, and it lives
     * below anything the public API can reach: co5300's per-draw
     * CASET/RASET/RAMWR polling is the trigger, IDF-internal the
     * bug. Recover IN PLACE instead of restarting: the stale
     * entries are just the io's bookkeeping -- a no-op
     * tx_param(-1) runs the driver's own drain loop over ALL
     * inflight entries and re-arms the flush queue clean. (If a
     * completion were ever truly undelivered that call parks in
     * get_trans_result under the panel mutex and the panel
     * watchdog restarts in 3 s + 3 s -- the honest fallback for
     * a wedge, with the log below naming the site.) */
    const moon_panel_io_spi_view_t *v = (const moon_panel_io_spi_view_t *)board_panel_io();
    ESP_LOGW(TAG,
             "drain verify after %s: completion swallowed (counter frozen at %lu; slot %s "
             "(fn %p ctx %p, mine %p), last accepted %s); healing the flush-queue bookkeeping",
             site, (unsigned long)c0, (v && v->on_color_trans_done == moon_panel_trans_done_cb) ? "MINE" : "STOLEN",
             v ? (const void *)v->on_color_trans_done : NULL, v ? v->user_ctx : NULL,
             (const void *)moon_panel_trans_done_cb, s_last_accept_desc);
    esp_err_t heal = esp_lcd_panel_io_tx_param(board_panel_io(), -1, NULL, 0);
    if (heal != ESP_OK) {
        drain_recovery(site, 0, "completion swallowed AND the heal was rejected");
    }
    xSemaphoreGive(s_panel_mtx);
#else
    (void)site;
#endif
}

static volatile bool s_frame_stale; /* s_frame/s_rgb left mid-bake by an
                                     * abort; a completed bake clears it.
                                     * A wake with the flag set defers a
                                     * re-bake instead of showing a
                                     * half-baked frame. */

/* Deferred wake re-bake (2026-11-10). The wake path and the day-tick
 * re-bake ~1.3 s of moon_bake_epoch -- on the LVGL task that froze the
 * just-woken UI (old panel frame + dead touch) for a second and more.
 * The bake now runs on core 1 like the other pipelines; off the LVGL
 * task moon_convert_to_rgb cannot flush (the 3.5 KB stack rule), so the
 * task sets s_rebake_show and the tick displays the finished frame as
 * ONE atomic whole-panel flush. While the frame is PARTIAL (a mid-bake
 * abort) the flag is never set, so a half-baked frame never reaches the
 * panel. */
static volatile bool s_rebake_baking; /* deferred bake task running */
static volatile bool s_rebake_again;  /* a newer epoch arrived mid-bake */
static volatile bool s_rebake_show;   /* bake finished on core 1: the tick displays it */
static volatile int64_t s_rebake_epoch;
static TaskHandle_t s_rebake_task;

/* Once-per-hour moon refresh (UI_MOON_HOURLY_ENABLE, the hourly section).
 * Producer/consumer split, exactly like the deferred re-bake: the LVGL task
 * is never blocked on a bake. The minute path (moon_ui_day_tick ->
 * moon_hourly_probe) records the INTENT (s_hourly_start + the hour-centre
 * epoch); the tick (moon_hourly_service, LVGL task) does the fade-to-black
 * + spawns the core-1 producer; the producer fills s_hourly_gray (TF hour
 * fetch or a full bake), then in ONE s_rgb_mtx block memcpy's it over
 * s_frame and rebuilds s_rgb, and sets s_hourly_done; the tick's done leg
 * shows the frame as ONE atomic whole-panel flush while the panel is still
 * black, then ramps the brightness back. The clock, alarm row and seconds
 * keep running throughout -- only the brightness animates. */
#define HOURLY_BUDGET_MS 5000                 /* producer wall-clock budget */
static volatile bool s_hourly_start;          /* the probe armed a cycle; the tick fades + spawns */
static volatile bool s_hourly_baking;         /* core-1 producer running */
static volatile bool s_hourly_done;           /* producer filled s_rgb: the tick shows it */
static volatile bool s_hourly_failed;         /* producer could not produce: restore, keep old frame */
static volatile bool s_hourly_faded;          /* dimmed to black: a fade-in back to 100 is owed */
static volatile int64_t s_hourly_epoch;       /* the hour-centre epoch being produced */
static volatile int s_hourly_event_sig;       /* signature to commit on show (s_baked_event) */
static volatile int s_hourly_src;             /* 1 = TF hour cache, 0 = baked (log only) */
static volatile int64_t s_hourly_deadline_ms; /* esp_timer ms; the 5 s budget */
static TaskHandle_t s_hourly_task;
static uint8_t *s_hourly_gray; /* PSRAM MOON_PX^2 fetch/bake bounce */
#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
static bool s_tf_ready; /* tfcache_init ran once (producer; TF hour leg only) */
#endif
static int64_t s_last_hourly_ms; /* esp_timer ms of the last arm (TEST cadence) */

/* Boot replay (UI_MOON_BOOT_REPLAY, the replay section below the hourly
 * one): ALL stored TF hour frames play ascending as the boot animation
 * and stop on the newest; the live-computed current phase lands as the
 * final frame (user 2026-11-14). Same producer/consumer split as the
 * hourly refresh -- the core-1 task publishes s_frame/s_rgb under
 * s_rgb_mtx and NEVER touches the panel; the tick lands each published
 * frame as ONE atomic whole-panel flush with its drain witness. No fade:
 * the brightness never moves (the replay's black is the panel's boot
 * black, not a dim). The flags compile unconditionally (cheap) so the
 * gates and moon_ui_replaying() exist even with the feature off; the
 * producer + spawn live under the Kconfig guard (and off for demo mode,
 * whose scripted tour owns the display). FLAG OWNERSHIP:
 * s_replay_baking is cleared ONLY by the producer itself (a paused tick
 * could otherwise strand "replaying" forever and starve the day tick);
 * s_replay_show/final/end are set by the producer and consumed by the
 * tick (pause/screen-leave may clear them as "will never show");
 * s_replay_presenting covers spawn..producer-exit. */
#define REPLAY_FRAME_WAIT_MS                                                                                           \
    2000                                  /* bounded show handshake: a paused tick must                                \
                                           * never wedge the producer (the abort flag or this                          \
                                           * bound releases it)                                     */
static volatile bool s_replay_presenting; /* boot presentation owns the buffers + ladder */
static volatile bool s_replay_baking;     /* core-1 producer running (it alone clears)    */
static volatile bool s_replay_show;       /* a published frame awaits the tick's atomic show */
static volatile bool s_replay_final;      /* the pending show is the FINAL (now-bake) frame  */
static volatile bool s_replay_end;        /* producer exited normally: the tick re-arms the ladder */
static volatile bool s_replay_abort;      /* pause / screen-left: stop animating, skip the final bake */
#if CONFIG_UI_MOON_BOOT_REPLAY && !CONFIG_UI_DEMO_MODE
static volatile int64_t s_replay_deadline_ms; /* esp_timer ms: the animation budget     */
#endif
static TaskHandle_t s_replay_task;

/* True while the boot presentation owns the buffers and the power ladder:
 * gates the hourly probe/spawn, the day tick, the orbit and every idle
 * re-arm. Includes the pending shows/end so the minute path cannot slip
 * a fade cycle (or strand the ladder re-arm) between the producer's exit
 * and the tick's final show. */
bool moon_ui_replaying(void) {
    return s_replay_presenting || s_replay_baking || s_replay_show || s_replay_final || s_replay_end;
}

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

/* Sky-event classification, day-scan-guarded (2026-11-09). The once-a-
 * calendar-day full_moments scan inside moon_event_classify (~350-600
 * ephemeris evaluations, measured 15-20 s on the S3) must never run on
 * the LVGL task: it hijacks the task mid-frame, freezing the minute
 * roll animation between the two pairs (observed on the panel: stuck
 * half-way 18:34 -> 18:35) and starving IDLE0 into a 5 s task-WDT
 * storm (no panic -- CONFIG_ESP_TASK_WDT_PANIC is off, so the device
 * looked like it was "crash looping" on the console). The CPU-1
 * pipeline tasks (orbit / re-bake -- both pinned to core 1, where
 * IDLE1 is unwatched, and all 12 KB-stacked for this scan) classify
 * their own epochs directly; every other caller asks the worker below
 * to warm the day cache and gets a plain (all-false) event until that
 * day's scan completed -- the one untinted bake self-heals, because
 * the next day-tick probe sees the signature change and re-bakes. */
static volatile int64_t s_ev_warm_day = -1; /* day whose full_moments scan completed */
static volatile int64_t s_ev_want_day = -1; /* day the worker was asked to scan */
static TaskHandle_t s_event_task;

/* The tasks that may run the day scan in place: the core-1 pipeline
 * tasks and the event worker itself. Everything else (the LVGL task,
 * the boot task) is guarded by moon_event_classify_here. */
static bool moon_ev_scan_allowed(void) {
    const TaskHandle_t me = xTaskGetCurrentTaskHandle();
    return me == s_orbit_task || me == s_event_task || me == s_rebake_task || me == s_hourly_task ||
           me == s_replay_task;
}

static void moon_event_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t now;
        if (clock_get_utc_epoch(&now) != ESP_OK)
            continue;
        const int64_t want = s_ev_want_day;
        moon_event_t ev;
        moon_event_classify(now, &ev); /* the day's scan, on core 1 */
        if (now / 86400 == want)
            s_ev_warm_day = want;
    }
}

/* moon_event_classify, but never on the LVGL task with a cold day
 * cache (see above). Returns false when the epoch's day has not been
 * scanned yet: ev is zeroed (a plain event -- byte-identical to an
 * all-false classify) and the worker was asked to warm that day. */
static bool moon_event_classify_here(int64_t epoch, moon_event_t *ev) {
    const int64_t day = epoch / 86400;
    if (moon_ev_scan_allowed()) {
        moon_event_classify(epoch, ev); /* core-1 pipeline context: the scan is its own */
        s_ev_warm_day = day;
        return true;
    }
    if (day != s_ev_warm_day) {
        s_ev_want_day = day;
        if (s_event_task)
            xTaskNotifyGive(s_event_task);
        memset(ev, 0, sizeof *ev);
        return false;
    }
    moon_event_classify(epoch, ev); /* warm day: the cheap path (the geometry evals only) */
    return true;
}

static void moon_invalidate_all(void) {
    for (int i = 0; i < s_n_images; i++)
        lv_obj_invalidate(s_images[i]);
}

/* ---------------- bake reveal (UI_MOON_BAKE_REVEAL) -- the computation,
 * made visible (user decision 2026-11-14, docs/implementation/bake-reveal) --
 *
 * The black windows of the current-phase bake (the hourly cold-bake leg
 * behind the fade, the boot replay's final "now" leg on the boot black:
 * ~1.3 s S3 / ~2.4 s P4 of dead panel) become the bake ITSELF. The core-1
 * producer bakes the PSRAM bounce strip by strip (moon_bake_rows -- the
 * row API the core always exposed for exactly this) and publishes a
 * rows-completed watermark; the tick converts each completed strip
 * gray->RGB565 into s_rgb and shows it as ONE lv_refr_now of that band
 * under s_rgb_mtx with its drain witness, over a moon square that was
 * cleared to black at reveal start. The moon draws itself north to south
 * at the ACTUAL bake rate: the bake is the metronome, no animation ever
 * outruns a computed row (no fake FPS -- if the bake slows, the reveal
 * slows; they are the same clock).
 *
 * ATOMICITY-PER-STATE: every DISPLAYED state is still one atomic flush
 * (the black canvas, then consistent strip states -- never a row torn
 * mid-pixel by LVGL's async drain, the rule that retired the old sweep).
 * This is the second sanctioned flow beside the brightness ramp; the
 * invariant wording in the file header and architecture.rst carries it.
 *
 * s_frame is NEVER touched during a reveal (only the bounce is), so any
 * abort heals the OLD frame back from s_frame (moon_reveal_restore) --
 * half a moon is never a resting state. The hourly fade choreography
 * pays its owed fade-in (s_hourly_faded) at the FIRST completed strip,
 * so the panel is dark for the fade window only, not fade + bake; the
 * finished frame still lands as the hourly done leg's ONE atomic
 * whole-panel flush (visually a no-op after a completed reveal -- the
 * contract and its drain witness are kept). Single owner: the existing
 * probes keep hourly / replay / orbit / re-bake mutually exclusive. The
 * flags compile unconditionally (cheap) so the hang-context dump shows
 * them even with the feature off. */
enum { REVEAL_NONE = 0, REVEAL_HOURLY, REVEAL_REPLAY_FINAL };
static volatile int s_reveal_owner;            /* whose bake is being revealed */
static volatile uint32_t s_reveal_pub;         /* rows fully baked into the bounce */
static volatile uint32_t s_reveal_shown;       /* rows the tick has displayed */
static volatile bool s_reveal_clear;           /* the black canvas is not shown yet */
static volatile bool s_reveal_dirty;           /* s_rgb holds reveal pixels: an abort must heal */
static volatile bool s_reveal_stop;            /* finish/pause asked the producer to stop */
static volatile int64_t s_reveal_last_show_ms; /* tick's last shown state (ms); the producer stamps
                                                * it at ARM so the first bands drip, not collapse */
#if CONFIG_TEST_BAKE_REVEAL_TRACE
static volatile int64_t s_trace_arm_us; /* producer's arm stamp (the trace's t0) */
#endif
/* Reveal strip height: 40 states on the S3 400 (40 x 10), 50 on the P4
 * 650 (50 x 13) -- the user's 2026-11-15 call ("I currently only see
 * two... we can add more"): with the drain witness paid in ~50 ms
 * (bench-proven, the drain-verify note) and one strip per tick, every
 * strip lands as its own state; the strip height only sets how fine
 * those states are. The S3 band payload (10 rows x 400 px x 2 B = 8
 * KiB) stays well under the 23,300 B strip cap; LVGL still drains
 * through the BSP's own buffer strips, so the 32 KiB SPI-DMA cap is
 * untouched. */
#if defined(CONFIG_BOARD_P4)
#define MOON_REVEAL_ROWS 13
#else
#define MOON_REVEAL_ROWS 10
#endif
#if (MOON_PX % MOON_REVEAL_ROWS) != 0
#error "MOON_PX must be a multiple of MOON_REVEAL_ROWS"
#endif
#define REVEAL_CATCHUP_WAIT_MS                                                                                         \
    2500 /* bounded wait for the tick to land the tail (one strip per                                                  \
          * tick paces the tail at ~50 ms/strip: ~40 strips of drip                                                    \
          * need ~2 s -- the old bound would truncate a full reveal                                                    \
          * into the heal path) */
#define REVEAL_PACE_WINDOW_MS                                                                                          \
    400 /* tick "in rhythm" = showed a state this recently: drip ONE strip per tick (the brief);                       \
         * longer = the tick was parked (a gate) -> resume collapses to the watermark instead of                       \
         * dripping strips a parked viewer never saw */

#if CONFIG_UI_MOON_BAKE_REVEAL

/* Heal the last committed frame over a half-revealed square: full
 * gray->RGB565 re-convert of the UNTOUCHED s_frame into s_rgb + ONE
 * atomic whole-panel flush (the moon_convert_to_rgb shape; LVGL-task
 * callers only -- moon_hourly_finish and moon_ui_pause both run there).
 * The reveal never wrote s_frame, so this always restores a COMPLETE
 * frame -- never half a moon. */
static void moon_reveal_restore(void) {
    s_reveal_dirty = false;
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
    moon_invalidate_all();
    lv_refr_now(NULL);
    xSemaphoreGive(s_rgb_mtx);
    moon_panel_drain_verify("bake reveal restore");
}

/* The revealed bake (core-1 producer side -- the hourly task, the replay
 * task's final leg): bake the PSRAM bounce strip by strip, publishing the
 * rows-completed watermark under the buffer mutex after every strip so
 * the tick's band reads and the watermark never race (the orbit rule).
 * The finished bounce is exactly what moon_bake would have produced (same
 * rows, same dark_zero/dk_sup, same event occlusion) -- everything
 * downstream (TF write-through, publish, commit-on-show) is unchanged.
 * abort_flag rides the replay's s_replay_abort (NULL for the hourly leg,
 * which is bounded by the 5 s cycle budget plus the catch-up wait here).
 * NEVER touches the panel; NEVER touches s_frame. */
static void moon_reveal_bake(int owner, const moon_geom_t *g, const uint8_t *alb, const moon_grel_t *grel,
                             const moon_bake_ev_t *bev, const volatile bool *abort_flag) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    s_reveal_owner = owner;
    s_reveal_pub = 0;
    s_reveal_shown = 0;
    s_reveal_dirty = false;
    s_reveal_clear = true;
    s_reveal_stop = false;
    s_reveal_last_show_ms = esp_timer_get_time() / 1000; /* "in rhythm" from the first tick */
    xSemaphoreGive(s_rgb_mtx);
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    ESP_LOGI(TAG, "reveal trace: ARM owner %d (%d px / %d rows)", owner, MOON_PX, MOON_REVEAL_ROWS);
#endif
    const int64_t t0 = esp_timer_get_time();
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    s_trace_arm_us = t0;
#endif
    int y = 0;
    while (y < MOON_PX) {
        if (s_reveal_stop || (abort_flag && *abort_flag))
            break;
        int y1 = y + MOON_REVEAL_ROWS;
        if (y1 > MOON_PX)
            y1 = MOON_PX;
#if CONFIG_TEST_BAKE_REVEAL_TRACE
        const int64_t strip_t0 = esp_timer_get_time();
#endif
        moon_bake_rows(g, alb, grel, s_hourly_gray, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2),
                       MOON_ALB_W, MOON_ALB_H, y, y1, /*dark_zero=*/1, /*dk_sup=*/0.0f, bev);
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        s_reveal_pub = (uint32_t)y1; /* the mutex orders the watermark against
                                      * the tick's row reads (the orbit rule) */
        xSemaphoreGive(s_rgb_mtx);
        y = y1;
#if CONFIG_TEST_BAKE_REVEAL_TRACE
        ESP_LOGI(TAG, "reveal trace: strip ->%d baked %lld ms +%lld ms (owner %d)", y,
                 (long long)((esp_timer_get_time() - strip_t0) / 1000), (long long)((esp_timer_get_time() - t0) / 1000),
                 owner);
#endif
        vTaskDelay(1); /* every strip: IDLE1 + TWDT oxygen (the orbit/knob bake cadence) */
    }
    /* Let the tick land the tail (bounded -- a parked or starved tick
     * never wedges this task; the done legs show atomically regardless,
     * and the heal paths cover every aborted state). */
    const int64_t catch_t0 = esp_timer_get_time() / 1000;
    while (s_reveal_shown < (uint32_t)MOON_PX && !s_reveal_stop && !(abort_flag && *abort_flag)) {
        if (esp_timer_get_time() / 1000 - catch_t0 > REVEAL_CATCHUP_WAIT_MS) {
            ESP_LOGW(TAG, "bake reveal: catch-up wait expired");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    s_reveal_owner = REVEAL_NONE;
    xSemaphoreGive(s_rgb_mtx);
    ESP_LOGI(TAG, "bake reveal: %d/%d rows in %lld ms (owner %d, shown %u)", y, MOON_PX,
             (long long)((esp_timer_get_time() - t0) / 1000), owner, (unsigned)s_reveal_shown);
}

/* The tick's reveal leg (LVGL task), called right after the hourly
 * service: consume the producer's watermark as designed atomic strip
 * states -- one state per tick (black canvas first, then one band per
 * call: the 50 ms tick against the real bake rate). */
static void moon_bake_reveal_service(void) {
    if (s_reveal_owner == REVEAL_NONE && !s_reveal_dirty)
        return;
    /* Panel-ownership gates (the orbit sweep's list plus the fade flows):
     * a moon-less screen, a traffic hold, light sleep, a screen-switch
     * fade or the sweep's own strip drain parks the reveal; the tick
     * heals / resumes when the panel frees (dirty persists until then).
     * s_fade_busy is DELIBERATELY not a gate: during the hourly the
     * reveal leg IS the 0x51 writer that pays the owed fade-in. */
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    /* Same five gates, read once so the gated dump can name them (all
     * cheap flag reads; the hold's expiry self-heal is time-driven
     * anyway). bits: 1 screen, 2 hold, 4 idle, 8 fade, 16 sweep. */
    const int gates = (!s_moon_screen) | (panel_hold_live() << 1) | (clock_is_idle() << 2) | (s_fade_pending << 3) |
                      (s_orbit_sweeping << 4);
    if (gates) {
        static int64_t s_trace_gate_ms; /* tick context only */
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (s_reveal_owner != REVEAL_NONE && now_ms - s_trace_gate_ms >= 1000) {
            s_trace_gate_ms = now_ms;
            ESP_LOGI(TAG, "reveal trace: GATED 0x%02x pub=%u shown=%u +%lld ms", gates, (unsigned)s_reveal_pub,
                     (unsigned)s_reveal_shown, (long long)((esp_timer_get_time() - s_trace_arm_us) / 1000));
        }
        return;
    }
#else
    if (!s_moon_screen || panel_hold_live() || clock_is_idle() || s_fade_pending || s_orbit_sweeping)
        return;
#endif
    int state = 0; /* 1 = canvas shown, 2 = strip band shown, 3 = heal */
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    uint32_t trace_from = 0; /* band's first row (the trace line) */
#endif
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    if (s_reveal_owner == REVEAL_NONE) {
        if (s_reveal_dirty) { /* backstop heal: finish/pause already cover the
                               * designed exits; this catches a producer that
                               * died with strips up (screen left mid-reveal --
                               * healed on the way back) */
            for (int i = 0; i < MOON_PX * MOON_PX; i++)
                s_rgb[i] = moon_gray565(s_frame[i]);
            s_reveal_dirty = false;
            moon_invalidate_all();
            lv_refr_now(NULL);
            state = 3;
        }
    } else if (s_reveal_clear) {
        memset(s_rgb, 0, sizeof(uint16_t) * (size_t)MOON_PX * MOON_PX);
        s_reveal_clear = false;
        s_reveal_shown = 0;
        s_reveal_dirty = true; /* s_rgb now diverges from s_frame: the black
                                * canvas + strips is a DESIGN state, half of
                                * it is not -- any abort must heal */
        moon_invalidate_all();
        lv_refr_now(NULL);
        state = 1;
    } else {
        /* The brief's "one completed strip band per 50 ms tick": while the
         * tick is in rhythm it advances by ONE strip, so every computed
         * strip becomes its own visible state (the producer is free-run:
         * the watermark would outrun the panel and collapse strips). The
         * watermark only wins right after a park (the tick showed nothing
         * for REVEAL_PACE_WINDOW_MS -- gated by hold/idle/fade/sweep):
         * stale strips are not worth dripping to a viewer who never saw
         * the recent ones. */
        uint32_t to = s_reveal_pub;
        if (to > (uint32_t)MOON_PX)
            to = (uint32_t)MOON_PX;
        if (to > s_reveal_shown + (uint32_t)MOON_REVEAL_ROWS &&
            esp_timer_get_time() / 1000 - s_reveal_last_show_ms <= REVEAL_PACE_WINDOW_MS)
            to = s_reveal_shown + (uint32_t)MOON_REVEAL_ROWS;
        if (to > s_reveal_shown) {
#if CONFIG_TEST_BAKE_REVEAL_TRACE
            trace_from = s_reveal_shown;
#endif
            for (uint32_t r = s_reveal_shown; r < to; r++) {
                const uint8_t *frow = &s_hourly_gray[(size_t)r * MOON_PX];
                uint16_t *rrow = &s_rgb[(size_t)r * MOON_PX];
                for (int x = 0; x < MOON_PX; x++)
                    rrow[x] = moon_gray565(frow[x]);
            }
            lv_area_t a = {.x1 = (lv_coord_t)MOON_IMG_POS,
                           .y1 = (lv_coord_t)(MOON_IMG_POS + (int)s_reveal_shown),
                           .x2 = (lv_coord_t)(MOON_IMG_POS + MOON_PX - 1),
                           .y2 = (lv_coord_t)(MOON_IMG_POS + (int)to - 1)};
            for (int i = 0; i < s_n_images; i++)
                lv_obj_invalidate_area(s_images[i], &a);
            s_reveal_shown = to;
            s_reveal_dirty = true;
            lv_refr_now(NULL);
            s_reveal_last_show_ms = esp_timer_get_time() / 1000;
            state = 2;
        }
    }
    xSemaphoreGive(s_rgb_mtx);
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    const int64_t pre_drain_us = esp_timer_get_time();
#endif
    if (state)
        moon_panel_drain_verify(state == 1 ? "bake reveal clear"
                                           : (state == 3 ? "bake reveal restore" : "bake reveal"));
#if CONFIG_TEST_BAKE_REVEAL_TRACE
    if (state)
        ESP_LOGI(TAG, "reveal trace: %s rows %u..%u +%lld ms (witness %lld ms)",
                 state == 1 ? "CANVAS" : (state == 3 ? "HEAL " : "band "), (unsigned)trace_from,
                 (unsigned)s_reveal_shown, (long long)((esp_timer_get_time() - s_trace_arm_us) / 1000),
                 (long long)((esp_timer_get_time() - pre_drain_us) / 1000));
#endif
    /* Hourly choreography (user decision 2026-11-14): the brightness ramp
     * back starts with the FIRST computed strip instead of after the whole
     * bake -- the dark window is the fade, not fade + bake. The owed
     * fade-in is paid exactly once: moon_hourly_finish's branch is the
     * only other payer and finds the flag false here (or pays it when no
     * strip ever landed -- TF hit, failed cycle, feature off). */
    if (state == 2 && s_hourly_faded) {
        s_hourly_faded = false;
        fade_ramp(0, 100, CONFIG_UI_MOON_HOURLY_FADE_MS);
        s_fade_busy = false; /* the other 0x51 writers may run again */
        ESP_LOGI(TAG, "bake reveal: first strip up, panel brightness 0 -> 100 (%d ms)", CONFIG_UI_MOON_HOURLY_FADE_MS);
    }
}
#endif /* CONFIG_UI_MOON_BAKE_REVEAL */

/* Static (angle-0) display of the baked frame: the 8-bit bake maps
 * straight to the RGB565 buffer through the LUT -- no gather. Used for
 * the first display and after every re-bake (day rollover / wake).
 * Under the buffer mutex like every other s_rgb writer, so it cannot
 * race a pipeline's row-wise rewrite. The fresh frame is DISPLAYED
 * here, atomically (one whole-panel flush): the invalidate alone would
 * leave it to LVGL's async refresh, which drains the 400-row image in
 * 25-row QSPI strips -- a visible top-to-bottom crawl of every
 * re-bake. */
static void moon_convert_to_rgb(void) {
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
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
    if (moon_on_lvgl())
        moon_panel_drain_verify("re-bake");
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
    const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief(); /* perf A/B hook */
    if (!grel)
        ESP_LOGW(TAG, "no relief cache; baking relief-free");
    /* Sky events (moon_event.c, the pre-calculated display tints):
     * classify this epoch's instant and select the matching LUT -- the
     * rebuild is a no-op when it already matches (a no-op on an ordinary
     * day, multipliers 1.0). The
     * blood occlusion (the eclipse "bite") is passed per-pixel into the
     * bake; an all-false event bakes as NULL (byte-identical). */
    moon_event_t ev;
    moon_event_classify_here(epoch, &ev); /* never the day scan on the LVGL task */
    moon_gray565_set_event(&ev);
    const moon_bake_ev_t *bev = moon_event_any(&ev) ? &ev : NULL;
    s_baked_event = moon_event_signature(&ev); /* the day-tick change detector */
    s_baked_hour = epoch / 3600;
    /* Under the buffer mutex: this re-bake replaces the WHOLE frame
     * (s_frame + the s_rgb display) in one guarded block, so no reader
     * ever sees a half-written frame. */
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    moon_bake(&g, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
              MOON_ALB_H, 1, 0.0f, /* dk_sup 0.0f = the plain d < 0 terminator */
              bev);                /* the blood-moon umbra occlusion (NULL on ordinary days) */
    xSemaphoreGive(s_rgb_mtx);
    s_frame_stale = false; /* a complete, consistent frame is in s_frame */
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

/* Deferred wake re-bake (see the rebake flags above): the
 * core-1 bake task. moon_bake_epoch owns the buffer mutex for the
 * bake; on completion the task sets s_rebake_show and the tick's
 * rebake leg displays the frame as ONE atomic whole-panel flush; a newer
 * epoch arriving mid-bake re-runs it (latest-wins, like the orbit).
 * 12 KB stack: the task's bake runs the geometry era chain, and
 * moon_event_classify_here is allowed here (core-1 pipeline context,
 * moon_ev_scan_allowed), so the day's full_moments scan may run on
 * THIS task -- exactly where it belongs. */
static void moon_rebake_task(void *arg) {
    (void)arg;
    for (;;) {
        moon_bake_epoch(s_rebake_epoch);
        if (!s_rebake_again)
            break;
        s_rebake_again = false;
    }
    s_rebake_baking = false;
    s_rebake_show = true; /* the tick shows the finished frame atomically */
    ESP_LOGI(TAG, "deferred re-bake done");
    vTaskDelete(NULL);
}

/* Ask for a current-phase re-bake OFF the LVGL task. While one is in
 * flight the newer epoch simply replaces the pending one (the task
 * re-runs). If the task cannot be created, bake inline -- correctness
 * beats responsiveness, and this is the rare OOM path. */
static void moon_rebake_request(int64_t epoch) {
    s_rebake_epoch = epoch;
    if (s_rebake_baking) {
        s_rebake_again = true;
        return;
    }
    s_rebake_again = false;
    s_rebake_baking = true;
    if (xTaskCreatePinnedToCore(moon_rebake_task, "moon_rebake", 12288, NULL, configMAX_PRIORITIES - 3, &s_rebake_task,
                                1) != pdPASS) {
        ESP_LOGW(TAG, "deferred re-bake task failed; baking inline");
        s_rebake_baking = false;
        moon_bake_epoch(epoch);
    }
}

/* ---------------- once-per-hour moon refresh (UI_MOON_HOURLY_ENABLE) -----
 *
 * The fade-to-black redraw (the file header + UI_MOON_HOURLY_ENABLE help).
 * Three roles, never on the same task at once:
 *   - the minute path moon_hourly_probe() (from moon_ui_day_tick) gates and
 *     arms: it records the hour-centre epoch + s_hourly_start, doing NO
 *     panel work (it may run on any task);
 *   - the tick moon_hourly_service() (LVGL task) fades to black and spawns
 *     the producer, then lands the finished frame as ONE atomic flush and
 *     fades back -- the fade's "atomic flush while black" rule, reused;
 *   - the producer moon_hourly_task() (core 1) fills the PSRAM bounce
 *     (TF hour fetch, else a full bake with write-through) and publishes
 *     s_frame + s_rgb in ONE s_rgb_mtx block. It NEVER touches the panel.
 * The hour-centre epoch (hour*3600+1800) is a stable key: byte-identical
 * across boots and boards (so the TF hour cache hits), and at most 30 min
 * stale at the show. s_baked_hour / s_baked_event commit on SHOW, not on
 * produce, so a cycle that never shows leaves the next hour still due. */

#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
/* The last NEW MOON at or before utc_epoch (defined in the replay section
 * together with the cache policy it feeds). Forward-declared for the
 * hourly producer's cache-policy leg below -- both legs (this and the
 * replay's) sit under the same TF guard, so the function exists exactly
 * where it has users. */
static int64_t new_moon_epoch(int64_t utc_epoch);
/* The memoized floor for the cache-policy leg (see the definition by the
 * replay section): both producers ask this, not new_moon_epoch directly. */
static int64_t new_moon_epoch_cached(int64_t utc_epoch);
#endif

/* The core-1 producer: produce the hour-centre frame into s_hourly_gray and
 * publish it. 12 KB stack (the bake's era chain + the allowed in-place day
 * scan), core 1 (IDLE1 unwatched), same shape as moon_rebake_task. A no-
 * albedo produce leaves the on-screen frame untouched and reports failure. */
static void moon_hourly_task(void *arg) {
    (void)arg;
    bool produced = false;
    const int64_t epoch = s_hourly_epoch;
    const int64_t hour = epoch / 3600;
    int64_t t0 = esp_timer_get_time();
    /* Classify the hour-centre instant: this sets the gray->RGB565 tint LUT
     * the publish below reads AND yields the blood-moon occlusion for a
     * cold bake. Allowed to run the day scan in place (core-1 pipeline --
     * moon_ev_scan_allowed). The LUT is stable for this fixed epoch. */
    moon_event_t ev;
    moon_event_classify_here(epoch, &ev);
    moon_gray565_set_event(&ev);
    s_hourly_event_sig = moon_event_signature(&ev);
    const moon_bake_ev_t *bev = moon_event_any(&ev) ? &ev : NULL;

    bool fetched = false;
#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
    if (!s_tf_ready) { /* lazy: the card mounts on the first producer to run
                        * (the boot replay when enabled; the hourly refresh
                        * otherwise) -- never on the boot task itself */
        s_tf_ready = true;
        tfcache_init(MOON_PX, MOON_PX, MOON_ALB_W, moon_relief() != NULL);
        if (tfcache_ready())
            tfcache_fetch_buf_alloc();
    }
    if (tfcache_ready()) {
        /* Cache policy every hour (user 2026-11-14): everything older
         * than the last NEW MOON is cleared, on top of the
         * one-synodic-month cap. The ephemeris scan runs HERE (core 1)
         * so a board that never reboots still prunes at the new-moon
         * crossing -- memoized across the synodic month (see
         * new_moon_epoch_cached: the raw ~8.2 s scan on EVERY cycle blew
         * the 5 s budget every hour, bench 2026-11-14); the meta rewrite
         * behind the setter fires only on the crossing itself. */
        tfcache_set_new_moon_floor(new_moon_epoch_cached(epoch) / 3600);
        tfcache_evict(hour);
    }
    if (tfcache_ready() && tfcache_fetch(hour, s_hourly_gray, MOON_PX))
        fetched = true; /* gray8 is LUT-independent; the tint LUT above covers it */
#endif
    if (!fetched) {
        moon_geom_t g;
        moon_geometry(epoch, &g);
        const uint8_t *alb = moon_albedo();
        if (!alb) {
            ESP_LOGE(TAG, "hourly: no albedo; keeping the old frame");
            s_hourly_baking = false;
            s_hourly_failed = true;
            vTaskDelete(NULL);
        }
        const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief();
#if CONFIG_UI_MOON_BAKE_REVEAL
        /* Reveal the computation (the reveal section): the same bake,
         * strip by strip with a watermark the tick displays -- the panel
         * is dark for the fade window only, not for the whole bake. */
        moon_reveal_bake(REVEAL_HOURLY, &g, alb, grel, bev, NULL);
#else
        moon_bake(&g, alb, grel, s_hourly_gray, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
                  MOON_ALB_H, 1, 0.0f, bev);
#endif
#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
        if (tfcache_ready()) {
            tfcache_store(hour, s_hourly_gray, MOON_PX); /* write-through */
            tfcache_evict(hour);                         // drop hours older than the window
        }
#endif
    }
    /* Publish: ONE guarded block replaces the whole frame (s_frame + s_rgb),
     * so no reader ever sees a half-written frame (same rule as
     * moon_bake_epoch). s_rgb_mtx held; no flush here (the tick shows it). */
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    memcpy(s_frame, s_hourly_gray, (size_t)MOON_PX * MOON_PX);
    for (int i = 0; i < MOON_PX * MOON_PX; i++)
        s_rgb[i] = moon_gray565(s_frame[i]);
    xSemaphoreGive(s_rgb_mtx);
    s_frame_stale = false;
    s_orbit_epoch = epoch; /* a later orbit peek re-bakes this same phase */
    produced = true;
    s_hourly_baking = false;
    s_hourly_done = produced; /* the tick's done leg shows + commits on show */
    ESP_LOGI(TAG, "hourly frame: hour %lld src=%s %lldms", (long long)hour, fetched ? "tf" : "bake",
             (long long)((esp_timer_get_time() - t0) / 1000));
    vTaskDelete(NULL);
}

/* The minute path's once-per-hour trigger. Returns true when it took the
 * hour (so moon_ui_day_tick skips the competing re-bake for it). Cheap flag
 * work only -- the tick performs the fade + spawn on the LVGL task. */
static bool moon_hourly_probe(int64_t now) {
    if (!CONFIG_UI_MOON_HOURLY_ENABLE || s_hourly_start || s_hourly_baking || s_hourly_done || s_hourly_failed)
        return false;
    const int64_t hour = now / 3600;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    bool due;
    if (CONFIG_TEST_HOURLY_FORCE_S > 0)
        due = (s_last_hourly_ms == 0) || (now_ms - s_last_hourly_ms >= (int64_t)CONFIG_TEST_HOURLY_FORCE_S * 1000);
    else
        due = (hour != s_baked_hour);
    if (!due)
        return false;
    /* Not while idle (light sleep), a moon-less screen is up, another
     * pipeline owns s_frame/s_rgb (the boot replay presents: its producer
     * rides the SAME s_hourly_gray bounce and the TF one-shot init -- two
     * producers must never run at once), or a 0x51 ramp / screen-switch
     * fade owns the panel. */
    if (!s_moon_screen || clock_is_idle() || s_orbit_baking || s_orbit_sweeping || s_rebake_baking || s_fade_pending ||
        s_fade_busy || moon_ui_replaying())
        return false;
    if (!s_hourly_gray) {
        /* Degraded (the PSRAM bounce never allocated): a plain deferred
         * re-bake still refreshes the phase this hour, just without the
         * fade and without the TF leg. */
        moon_rebake_request(hour * 3600 + 1800);
        return true;
    }
    s_hourly_epoch = hour * 3600 + 1800; /* the hour centre: a stable TF key */
    s_last_hourly_ms = now_ms;
    s_hourly_start = true;
    return true;
}

/* The tick's hourly legs (LVGL task), called at the head of moon_tick_cb.
 * Start leg: the probe armed a cycle -- fade to black on the hardware
 * brightness (the fade's atomic rule: the flush that shows the new frame
 * lands while black) and spawn the producer. Done leg: the producer filled
 * s_rgb -- show it as ONE atomic whole-panel flush, then ramp back. Failed
 * leg (or a blown deadline): restore the brightness; the OLD frame is still
 * what s_rgb held. A late producer that lands after the deadline still
 * shows on its own done leg, but with no second fade-in (s_hourly_faded was
 * already cleared) -- never a second black. */
static void moon_hourly_service(void) {
    if (s_hourly_start) {
        s_hourly_start = false;
        if (!s_hourly_baking && !s_hourly_done && !s_hourly_failed && !s_orbit_baking && !s_orbit_sweeping &&
            !s_rebake_baking && s_moon_screen && !s_fade_pending && !s_fade_busy && !moon_ui_replaying()) {
            s_hourly_deadline_ms = esp_timer_get_time() / 1000 + HOURLY_BUDGET_MS;
            s_hourly_baking = true;
            s_fade_busy = true;    /* the dim ramp is the only 0x51 writer */
            s_hourly_faded = true; /* a fade-in back to 100 is owed */
            fade_ramp(s_panel_pct, 0, CONFIG_UI_MOON_HOURLY_FADE_MS);
            if (xTaskCreatePinnedToCore(moon_hourly_task, "moon_hourly", 12288, NULL, configMAX_PRIORITIES - 3,
                                        &s_hourly_task, 1) != pdPASS) {
                ESP_LOGW(TAG, "hourly: producer task failed to start; restoring the old frame");
                s_hourly_baking = false;
                s_hourly_failed = true; /* the failed leg below restores brightness */
            }
        }
    }
    if (s_hourly_baking && !s_hourly_done && !s_hourly_failed && esp_timer_get_time() / 1000 > s_hourly_deadline_ms) {
        ESP_LOGW(TAG, "hourly: producer overran %d ms; restoring without it", HOURLY_BUDGET_MS);
        s_hourly_failed = true; /* the producer, if it lands, only updates s_rgb late */
    }
    if (s_hourly_done) {
        s_hourly_done = false;
        s_hourly_baking = false;
        moon_hourly_finish(1);
    } else if (s_hourly_failed) {
        s_hourly_failed = false;
        s_hourly_baking = false;
        moon_hourly_finish(0);
    }
}

/* ---------------- boot replay of the TF hour cache (UI_MOON_BOOT_REPLAY) --
 *
 * The boot presentation (user 2026-11-14): "on bootup, during
 * re-calculation of the current moon, all displays should show all
 * previously calculated frames as an animation and stop at the last
 * calculated frame that was stored on the TF card... If no cache is
 * available, the display stays black until the current moon phase is
 * calculated. The cache is cleared on each new moon and cannot hold more
 * than one synodic month of hourly frames."
 *
 * Roles (the hourly split, reused): the tick leg in moon_tick_cb shows
 * every published frame as ONE atomic whole-panel flush with its drain
 * witness (no fade -- the brightness never moves; the replay's black is
 * the panel's boot black) and re-arms the idle ladder on the end leg;
 * the core-1 producer moon_boot_replay_task owns the card and the
 * buffers. The producer and the hourly producer NEVER run at once (the
 * probe + spawn gates hold off while moon_ui_replaying()) -- they share
 * the s_hourly_gray bounce, the TF one-shot init, and tfcache's
 * single-caller contract. The whole replay (idle hold included) is
 * boot-only; wakes stay static. Demo mode boots without it (its scripted
 * tour owns the display).
 */

#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
/* The last NEW MOON at or before utc_epoch -- restored 2026-11-14 for the
 * cache policy (the original died with the boot sweep in c0034b4; the
 * knob carries the verbatim logic as knob_moon_new_moon_epoch, the probe
 * is host_sim/newmoon_test.c). Coarse 6 h grid backwards over at most one
 * synodic month brackets the illumination minimum, the fine 20 min pass
 * refines only the coarse minimum and its two neighbours (a full 20-min
 * grid over 29.53 d would be ~2200 ephemeris calls). The 1e-6 slack keeps
 * a flat/tied coarse step from drifting the fine pass to a neighbouring
 * cycle's minimum. Returns < utc_epoch by construction. Yields every 32
 * calls -- call on a core-1 pipeline task, never the LVGL task. */
static int64_t new_moon_epoch(int64_t utc_epoch) {
    moon_geom_t g;
    int64_t best = utc_epoch - (int64_t)(29.53 * 86400);
    float best_illum = 1.0f;
    int n = 0;
    for (int64_t e = utc_epoch - (int64_t)(6 * 3600); e >= utc_epoch - (int64_t)(29.53 * 86400); e -= 6 * 3600) {
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
/* The cache-policy floor, memoized (bench 2026-11-14): the raw scan costs
 * ~8.2 s on the S3 (~155 ephemeris evals) -- re-running it on EVERY hourly
 * cycle blew the 5 s producer budget every hour (measured: hit cycles
 * landed at a constant 8.2 s, black window + restore + late show each
 * time). The floor only MOVES at a new-moon crossing, so one scan serves
 * the whole synodic month: reuse while the query stays within 30 d of the
 * cached floor (30 > every real lunation, so a reuse can never point at a
 * floor that a crossing already invalidated; a crossing that just happened
 * at most delays the prune by the next query's span check -- and the
 * policy is non-authoritative anyway: tfcache_evict enforces the
 * one-synodic-month cap ephemeris-free). Clock-backwards jumps (query
 * below the cached floor) re-scan. Plain statics are safe: the two
 * callers are the hourly and replay producers, which NEVER run at once
 * (the probe + spawn gates). */
static bool s_nm_valid;
static int64_t s_nm_floor; /* the cached last new moon at or before the query */
static int64_t new_moon_epoch_cached(int64_t utc_epoch) {
    if (s_nm_valid && utc_epoch >= s_nm_floor && utc_epoch - s_nm_floor < (int64_t)(30.0 * 86400))
        return s_nm_floor;
    s_nm_floor = new_moon_epoch(utc_epoch);
    s_nm_valid = true;
    return s_nm_floor;
}
#endif /* TF cache policy users */

/* The producer compiles only where it can be spawned (see moon_ui_start:
 * the same guard) -- with the feature off or in demo mode it is gone. */
#if CONFIG_UI_MOON_BOOT_REPLAY && !CONFIG_UI_DEMO_MODE
#define REPLAY_MAX_HOURS 709 /* mirrors tfcache's TF_MAX_HOURS cap */

/* The core-1 replay producer. 12 KB (the final bake's era chain + the
 * whitelisted in-place day scan; the hour list lives in PSRAM, not on
 * this stack), pinned core 1, the hourly/rebake priority. It NEVER
 * touches the panel -- every frame goes out through s_rgb_mtx and the
 * tick's atomic show. */
static void moon_boot_replay_task(void *arg) {
    (void)arg;
    const int64_t t0 = esp_timer_get_time();
    int64_t now = 0;
    const bool have_time = (clock_get_utc_epoch(&now) == ESP_OK);
    int shown = 0;
#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
    if (!s_tf_ready) { /* first producer to run: the lazy mount moves to the
                        * boot TASK's first moment -- this task; the boot
                        * task itself still never touches the bus */
        s_tf_ready = true;
        tfcache_init(MOON_PX, MOON_PX, MOON_ALB_W, moon_relief() != NULL);
        if (tfcache_ready())
            tfcache_fetch_buf_alloc();
    }
    /* Prune BEFORE serving, on the persisted floor (the new-moon SCAN
     * rides the final leg, below -- frame #1 must not queue behind ~150
     * ephemeris calls). list_hours clamps to the same floor even without
     * a clock (its newest-key basis). */
    if (tfcache_ready() && have_time)
        tfcache_evict(now / 3600);
    int64_t *hours = heap_caps_malloc(REPLAY_MAX_HOURS * sizeof(int64_t), MALLOC_CAP_SPIRAM);
    const int n = (tfcache_ready() && s_hourly_gray && hours) ? tfcache_list_hours(hours, REPLAY_MAX_HOURS) : 0;
    if (n > 0) {
        moon_gray565_set_event(NULL); /* neutral LUT: the stored grays are
                                       * event-independent, and classifying
                                       * a whole cycle would cost more than
                                       * the animation is worth */
        int64_t next_due_ms = esp_timer_get_time() / 1000;
        const int64_t gap_ms = 1000 / (CONFIG_UI_MOON_REPLAY_FPS > 0 ? CONFIG_UI_MOON_REPLAY_FPS : 8);
        for (int i = 0; i < n; i++) {
            if (s_replay_abort)
                break;
            if (esp_timer_get_time() / 1000 > s_replay_deadline_ms) {
                ESP_LOGW(TAG, "replay: %d ms budget hit at frame %d/%d; going straight to the now-frame",
                         CONFIG_UI_MOON_REPLAY_BUDGET_MS, i, n);
                break;
            }
            if (!tfcache_fetch(hours[i], s_hourly_gray, MOON_PX))
                continue; /* healed miss (truncated/garbage file): skip it */
            /* Pacing: the min dwell AND the previous frame shown (single
             * in-flight, so the tick never races a publish with its
             * lv_refr_now). Every wait is bounded -- a paused tick
             * (sleep entry raised s_replay_abort) or a starved tick must
             * never wedge this task; only IT clears s_replay_baking. */
            const int64_t wait_t0 = esp_timer_get_time() / 1000;
            bool timed_out = false;
            while (s_replay_show || esp_timer_get_time() / 1000 < next_due_ms) {
                if (s_replay_abort || esp_timer_get_time() / 1000 > s_replay_deadline_ms)
                    break;
                if (esp_timer_get_time() / 1000 - wait_t0 > REPLAY_FRAME_WAIT_MS) {
                    timed_out = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (timed_out) { /* the tick never took the previous frame: stop
                              * animating, the final leg still lands the
                              * current phase */
                ESP_LOGW(TAG, "replay: show handshake stalled at frame %d/%d; going to the now-frame", i, n);
                break;
            }
            if (s_replay_abort)
                break;
            /* Publish: ONE guarded block replaces the whole frame (the
             * hourly publish shape). NO s_orbit_epoch here -- replay
             * frames are HISTORIC phases; only the final bake may move
             * it (an orbit peek after an aborted replay must not re-bake
             * a stored hour's phase). */
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            memcpy(s_frame, s_hourly_gray, (size_t)MOON_PX * MOON_PX);
            for (int j = 0; j < MOON_PX * MOON_PX; j++)
                s_rgb[j] = moon_gray565(s_frame[j]);
            xSemaphoreGive(s_rgb_mtx);
            s_frame_stale = false;
            s_replay_show = true;
            shown++;
            next_due_ms = esp_timer_get_time() / 1000 + gap_ms;
        }
    }
    free(hours);
#endif
    /* The final leg: the CURRENT phase, live-computed -- the animation
     * ends when the recalculation lands (the user's "during
     * re-calculation"). Bake into the bounce OUTSIDE s_rgb_mtx (the
     * hourly shape: holding the mutex across the ~1.3-2.4 s bake would
     * freeze the LVGL task on any touch that races it) and publish in
     * one short guarded block. Bookkeeping commits HERE and only here:
     * an aborted replay leaves s_baked_day = 0, so the minute day-tick
     * queues the corrective re-bake for a frame left on a stored hour. */
    bool final_landed = false;
    if (!s_replay_abort && have_time && moon_albedo() != NULL && s_hourly_gray) {
        moon_event_t ev;
        moon_event_classify_here(now, &ev); /* core-1 pipeline task: the day
                                             * scan may run in place
                                             * (moon_ev_scan_allowed) */
        moon_gray565_set_event(&ev);
        const int ev_sig = moon_event_signature(&ev);
        const moon_bake_ev_t *bev = moon_event_any(&ev) ? &ev : NULL;
        moon_geom_t g;
        moon_geometry(now, &g);
        const moon_grel_t *grel = CONFIG_TEST_BAKE_NO_RELIEF ? NULL : moon_relief();
#if CONFIG_UI_MOON_BAKE_REVEAL
        /* The reveal rides the "during re-calculation" leg (the reveal
         * section): with no usable cache this IS the boot display -- the
         * moon draws itself over the boot black at the bake rate instead
         * of the panel waiting ~1.3-2.4 s for it. The mid-bake abort
         * check is the reveal's alone -- the monolithic bake below never
         * checked (today's posture: a late abort still published). */
        moon_reveal_bake(REVEAL_REPLAY_FINAL, &g, moon_albedo(), grel, bev, &s_replay_abort);
        const bool aborted_mid = s_replay_abort;
#else
        moon_bake(&g, moon_albedo(), grel, s_hourly_gray, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2),
                  MOON_ALB_W, MOON_ALB_H, 1, 0.0f, bev);
        const bool aborted_mid = false;
#endif
        if (aborted_mid) { /* stopped mid-reveal (sleep hand-off): the bounce
                            * holds partial rows -- do NOT publish;
                            * s_baked_day stays 0 and the flag below lets the
                            * wake's day-tick re-bake take the frame (the
                            * aborted-replay posture). The reveal heal runs in
                            * moon_ui_pause / the tick. */
            s_frame_stale = true;
        } else {
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            memcpy(s_frame, s_hourly_gray, (size_t)MOON_PX * MOON_PX);
            for (int i = 0; i < MOON_PX * MOON_PX; i++)
                s_rgb[i] = moon_gray565(s_frame[i]);
            xSemaphoreGive(s_rgb_mtx);
            s_frame_stale = false;
            s_orbit_epoch = now; /* orbit peeks re-bake this same phase */
            s_orbit_baked = 0;   /* the static bake shows the default viewport */
            s_baked_hour = now / 3600;
            s_baked_event = ev_sig;
            s_baked_day = now / 86400;
#if CONFIG_UI_MOON_TFCACHE && CONFIG_UI_MOON_HOURLY_TFCACHE
            if (tfcache_ready()) { /* the new-moon scan rides the final leg (boot
                                    * latency): it prunes the card for the NEXT
                                    * boot and persists into meta.txt */
                tfcache_set_new_moon_floor(new_moon_epoch_cached(now) / 3600);
                tfcache_evict(now / 3600);
            }
#endif
            s_replay_final = true;
            s_replay_show = true; /* the tick shows the finished frame atomically */
            final_landed = true;
        }
    } else if (!s_replay_abort && have_time) {
        /* Degraded (no PSRAM bounce or no albedo): today's behavior --
         * the day-tick's deferred re-bake (or the hourly probe) takes
         * the frame once the replay exits. */
        s_frame_stale = true;
    }
    s_replay_baking = false;     /* the producer alone clears it (never the
                                  * tick / pause -- a parked task must not
                                  * strand "replaying" true) */
    s_replay_presenting = false; /* spawn..exit, producer-owned */
    s_replay_end = true;         /* the tick re-arms the idle ladder (once
                                  * the pending show landed) */
    ESP_LOGI(TAG, "boot replay: %d stored frame%s + %s in %lldms", shown, shown == 1 ? "" : "s",
             final_landed ? "now-frame" : (s_replay_abort ? "aborted" : "no now-frame"),
             (long long)((esp_timer_get_time() - t0) / 1000));
    vTaskDelete(NULL);
}
#endif /* BOOT_REPLAY && !DEMO_MODE */

/* Forward decl: defined in the orbit section below (the orbit pipeline
 * must be cancelled before a re-bake takes over s_frame/s_rgb). */
static void moon_orbit_interrupt(void);

/* ---------------- out-of-plane orbit (sphere model) ---------------- */

/* Bake the current phase at the given orbit angle -- the WHOLE frame,
 * off screen (row by row, per-row critical sections so a cancel is
 * honored quickly). When it is done the tick displays the finished frame
 * as one atomic whole frame (MOON_ORBIT_BATCH = MOON_NSTRIPS strips in
 * one lv_refr_now), so the viewport change lands as one ~150 ms frame
 * instead of a progressive top-to-bottom wipe over the ~2.4 s bake. The
 * task owns s_frame and s_rgb's row content, but never the images'
 * validity. */
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
    /* The orbit re-bake is the same phase as the last static bake
     * (s_orbit_epoch), so it carries the same sky event: classify it for
     * the blood occlusion (the tint LUT was already set by
     * moon_bake_epoch at that epoch). */
    moon_event_t ev;
    moon_event_classify_here(epoch, &ev); /* orbit task (core 1): scans in place */
    const moon_bake_ev_t *bev = moon_event_any(&ev) ? &ev : NULL;
    int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < MOON_PX && !s_orbit_cancel; y++) {
        /* One lock per row: this replaces the WHOLE frame (s_frame + the
         * s_rgb sweep), so every reader that blocks on the mutex sees
         * each new row only after it is complete, and can never tear the
         * sweep. The short per-row critical sections also keep the bake
         * yielding often, so a cancel is honored quickly. */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        moon_bake_rows(&g, alb, grel, s_frame, MOON_PX, MOON_R, (float)(MOON_PX / 2), (float)(MOON_PX / 2), MOON_ALB_W,
                       MOON_ALB_H, y, y + 1, 1, 0.0f, bev); /* dark_zero (T3); dk_sup 0.0f = the plain d < 0
                                                              terminator (the true orbit phase); bev = the
                                                              blood-moon occlusion (same phase as the last
                                                              static bake) */
        /* Fixed viewport: the row maps straight to RGB565 (no rotation). */
        const uint8_t *frow = &s_frame[(size_t)y * MOON_PX];
        uint16_t *rrow = &s_rgb[(size_t)y * MOON_PX];
        for (int x = 0; x < MOON_PX; x++)
            rrow[x] = moon_gray565(frow[x]);
        xSemaphoreGive(s_rgb_mtx);
        if ((y & 15) == 15)
            vTaskDelay(1); /* a FULL tick (10 ms at HZ=100): pdMS_TO_TICKS(1)
                            * rounds to 0 ticks -- a no-op that starved IDLE1
                            * (bench: boot-sweep bake spun core 1, TWDT
                            * IDLE1 hits every 5 s once the CPU1 check was
                            * enabled) */
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
     * baking s_rgb rows immediately, and the tick's display guard (the
     * atomic whole-frame flush must not land over a half-baked frame)
     * only stops once the flags are up. */
    s_orbit_baking = true;
    s_orbit_sweeping = true;
    s_orbit_strip_shown = 0;
    s_orbit_pub = 0;
    /* 12 KB, not 8: the task's moon_event_classify (the 60-day full-moon
     * S . E scan, §5.5) adds ~400 B of frames on top of the era chain the
     * bake already runs (moon_geometry -> eraEpv00/eraMoon98). */
    if (xTaskCreatePinnedToCore(moon_orbit_task, "moon_orbit", 12288, NULL, configMAX_PRIORITIES - 3, &s_orbit_task,
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

/* Start a bake if the quantized target is set and no bake is in
 * flight. Called from the tick (cheap flag checks); latest-wins falls
 * out of the target being read at task start. The orbit is only
 * meaningful once a static bake exists (s_orbit_epoch > 0, set by the
 * first bake) -- it bakes THE current phase at a peeked viewport. */
static void moon_orbit_drain(void) {
    /* The hourly producer owns s_frame/s_rgb while it is armed or baking,
     * and the boot replay owns them from spawn until its last frame has
     * shown (an orbit bake starting mid-replay would race the publish
     * memcpy -- and an orbit peek on a half-shown animation is moot
     * anyway: moon_ui_start cancelled the orbit for the boot). */
    if (!CONFIG_UI_MOON_ORBIT_ENABLE || !s_started || s_orbit_epoch <= 0 || s_orbit_baking || s_orbit_sweeping ||
        s_hourly_baking || s_hourly_start || moon_ui_replaying())
        return;
    if (s_orbit_target == 0 || s_orbit_target == s_orbit_baked)
        return; /* default viewport, or the on-screen frame is this viewport */
    moon_orbit_start();
}

/* Cancel the running orbit bake/sweep and leave a clean default-viewport
 * state. Called on the sleep transition and when the boot animation
 * (re)starts, both of which take over s_frame/s_rgb. */
static void moon_orbit_interrupt(void) {
    /* Joining the orbit task makes sure no writer is still mid-row in
     * s_rgb when the state is reset and the buffers go to the next
     * pipeline (the re-bake task may block on the orbit's per-row
     * mutex): that matters once the moon has started. Before
     * moon_ui_start there is no other writer, and the boot bake runs on
     * the display core before the task is ever spawned, so a
     * still-running orbit task cannot race it. */
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
    /* Never re-arm the ladder underneath the boot replay: moon_ui_start
     * calls THIS on the boot path right before spawning the replay
     * producer, and the replay holds the ladder itself until its end leg
     * runs (moon_tick_cb). */
    if (!clock_is_idle() && !moon_ui_replaying())
        clock_idle_set_enabled(true);
}

/* The tilt drives the moon's OUT-OF-PLANE orbit (sphere model; there is
 * no in-plane animation any more -- the tilt is an orbit input and an
 * activity/wake signal, nothing else). The
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

/* ---------------- tick ---------------- */

static void moon_tick_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_ready || !s_started)
        return;
    moon_hourly_service(); /* once-per-hour: fade / produce / show legs (head, owns the 0x51) */
#if CONFIG_UI_MOON_BAKE_REVEAL
    moon_bake_reveal_service(); /* strip states of an in-flight bake (the reveal section) */
#endif
    /* Boot replay show legs (the replay section): every frame the core-1
     * producer published lands as ONE atomic whole-panel flush,
     * drain-witnessed like every sanctioned show (a lost completion would
     * otherwise pin the NEXT show ~2 s/strip -- with up to 709 shows per
     * boot, witnessing only the last one is not enough). No fade: the
     * brightness never moves. The end leg re-arms the idle ladder the
     * presentation held (the orbit-completion leg below is the
     * precedent); skipped while a moon-less screen is up -- settings/sleep
     * own their ladder lifecycle and re-arm on their own way back. */
    if (s_replay_show) {
        s_replay_show = false;
        /* final rides WITH the pending show (the producer sets it before
         * show) and dies with it: consuming both here is what ends
         * moon_ui_replaying() -- a stale final would strand the gates
         * (hourly probe / day tick / orbit) shut for the boot. */
        const bool final_frame = s_replay_final;
        s_replay_final = false;
        if (s_moon_screen && !s_fade_pending && !s_fade_busy && !panel_hold_live()) {
            moon_invalidate_all();
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
            moon_panel_drain_verify(final_frame ? "boot replay final" : "boot replay");
        }
    }
    if (s_replay_end && !s_replay_show) { /* after the pending show landed */
        s_replay_end = false;
        if (s_moon_screen && !clock_is_idle()) {
            clock_idle_set_enabled(true);
            clock_idle_reset_timer();
        }
    }
    /* Deferred re-bake display leg: the core-1 bake task filled s_rgb
     * off the LVGL task (where moon_convert_to_rgb must not flush -- the
     * 3.5 KB stack rule), so the tick lands the finished frame as ONE
     * atomic whole-panel flush. Waits out any pipeline that is mid-read
     * of s_rgb (the strips it would interrupt belong to that pipeline). */
    if (s_rebake_show && !s_orbit_sweeping) {
        s_rebake_show = false;
        moon_invalidate_all();
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        moon_panel_drain_verify("rebake");
    }
    /* Orbit sweep consumer: show the strips of the freshly baked frame
     * the task has fully converted, top to bottom, so the new viewport
     * sweeps in instead of popping in. The drain below is the only s_rgb
     * reader while the sweep runs -- every other writer is gated on the
     * orbit flags. */
    if (s_orbit_sweeping) {
        int strips_ready = (int)((uint32_t)s_orbit_pub / MOON_STRIP_ROWS);
        if (strips_ready > MOON_NSTRIPS)
            strips_ready = MOON_NSTRIPS;
        int show = strips_ready - s_orbit_strip_shown;
        if (show > MOON_ORBIT_BATCH)
            show = MOON_ORBIT_BATCH; /* the atomic whole-frame display */
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
            /* The mutex-guarded drain: pixels out of s_rgb into LVGL's
             * draw buffers before the next strip may be reused. */
            xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
            lv_refr_now(NULL);
            xSemaphoreGive(s_rgb_mtx);
            /* The batch right before the orbit's last one is the frame
             * the next writer will follow -- catch a lost hand here. */
            moon_panel_drain_verify("orbit batch");
        }
        if (s_orbit_strip_shown >= MOON_NSTRIPS) {
            s_orbit_sweeping = false; /* the orbit frame is fully on screen */
            if (!clock_is_idle())
                clock_idle_set_enabled(true);
        }
    }
    moon_imu_tick();    /* tilt drives the orbit target (no-op without an IMU) */
    moon_orbit_drain(); /* latest-wins: start a pending orbit bake */
}

/* ---------------- public API ---------------- */

/* The panel-io watchdog task (see the s_panel_mtx block): take the panel
 * mutex with a finite timeout -- a normal take returns in <100 ms (a
 * single strip or a whole-panel flush), so a wait that overruns the
 * limit means the current holder is stuck inside a panel-io write on a
 * bus that will never free. Log and restart: the device comes back
 * clean (the moon re-bakes from the RTC) instead
 * of freezing. Priority 3; the take is the ONLY work, so it cannot
 * starve anything. */
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
         * of PANEL_IO_WATCHDOG_LIMIT_MS of idle. First ask the holder
         * what it is waiting for: a flush strip riding out the storm's
         * internal-RAM squeeze (every rejected attempt was NO_MEM -- the
         * polling commands land, the bounce just cannot get 23,300
         * contiguous DMA bytes right now) is NOT a wedged bus; restarting
         * there traded a few seconds of late pixels for a full reboot
         * (field 2026-11-17). Anything else -- the holder parked inside
         * spi_device_acquire_bus on a bus lock that will never clear --
         * restarts so the user gets a working clock instead of a dead
         * one; the log marks it unambiguously for the field. */
        if (s_panel_no_mem) {
            ESP_LOGW(TAG,
                     "panel mutex held %lu ms by task %s riding out a no-mem strip bounce (storm pressure, "
                     "bus healthy) -- not restarting",
                     (unsigned long)(xTaskGetTickCount() - t0) * portTICK_PERIOD_MS,
                     pcTaskGetName(xSemaphoreGetMutexHolder(s_panel_mtx)));
            continue;
        }
        ESP_LOGE(TAG,
                 "PANEL IO STUCK: panel mutex held %lu ms after a %lu ms wait by task %s -- the QSPI "
                 "bus lock is stuck (a DMA completion was lost); restarting",
                 (unsigned long)(xTaskGetTickCount() - t0) * portTICK_PERIOD_MS,
                 (unsigned long)PANEL_IO_WATCHDOG_LIMIT_MS, pcTaskGetName(xSemaphoreGetMutexHolder(s_panel_mtx)));
        vTaskDelay(pdMS_TO_TICKS(100)); /* let the log flush out the UART */
        esp_restart();
    }
}

void moon_ui_init(void) {
    if (s_ready)
        return;

    s_frame = heap_caps_malloc(MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
    s_rgb = heap_caps_malloc((size_t)MOON_PX * MOON_PX * 2, MALLOC_CAP_SPIRAM);
    if (!s_frame || !s_rgb) {
        ESP_LOGE(TAG, "allocation failed (PSRAM); moon disabled");
        return;
    }
    if (CONFIG_UI_MOON_HOURLY_ENABLE) {
        /* The hourly producer's PSRAM bounce (fetch target / bake output),
         * kept separate from s_frame so the on-screen frame stays intact
         * until the publish memcpy. A failed alloc degrades the hourly to a
         * plain re-bake (moon_hourly_probe falls back), not a failure. */
        s_hourly_gray = heap_caps_malloc((size_t)MOON_PX * MOON_PX, MALLOC_CAP_SPIRAM);
        if (!s_hourly_gray)
            ESP_LOGW(TAG, "hourly bounce buffer failed; the hourly refresh degrades to a plain re-bake");
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
     * (orbit/re-bake). The build is the one slow, non-idempotent op (one 4 MB
     * packed PSRAM array from a one-shot whole-DEM read +
     * moon_relief_build); running it once up front means the later
     * moon_bake_epoch / orbit moon_relief() calls are cheap
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
    /* Sync: a buffer mutex serializing every s_rgb / s_frame
     * reader+writer (the orbit's row sweep, the static LUT convert, the
     * re-bake pipeline), and a PANEL mutex serializing every panel
     * write (LVGL's flush strips, the 0x51 ramp steps, the drain
     * probes) -- which reach the panel through the draw_bitmap wrapper
     * installed below (every LVGL flush funnels through
     * esp_lcd_panel_draw_bitmap, proven by the adapter's flush path). */
    s_rgb_mtx = xSemaphoreCreateMutex();
    s_panel_mtx = xSemaphoreCreateMutex();
    if (!s_rgb_mtx || !s_panel_mtx) {
        ESP_LOGE(TAG, "sync allocation failed; moon disabled");
        s_ready = false;
        return;
    }
    /* The panel handle + the draw_bitmap wrapper (the serialization
     * point with LVGL). bsp_display_start runs before moon_ui_init
     * (app_main), so the handle is valid. */
    s_panel = board_panel();
    if (s_panel) {
        s_orig_draw = s_panel->draw_bitmap;
        s_panel->draw_bitmap = moon_panel_draw_bitmap;
        s_panel_wrap = true;
        ESP_LOGI(TAG, "panel draw_bitmap wrapper installed (panel mutex + drain accounting)");
    } else {
        ESP_LOGE(TAG, "panel handle unavailable; panel wrapper off");
    }
    /* Capture the adapter's flush_cb for the sync-storm hold's guard swap
     * (bsp_display_start ran first, so the adapter has installed it). */
    lv_display_t *ldisp = lv_display_get_default();
    if (ldisp) {
        s_flush_cb_real = lv_display_get_flush_cb(ldisp);
        /* Everything that runs with the "real" flush now goes through the
         * probe (the hold swaps between hold_flush_cb and the probe). */
        lv_display_set_flush_cb(ldisp, flush_cb_probe);
        /* Bound the flush wait (lost trans-done hand = frozen touch;
         * see moon_flush_wait_cb). */
        lv_display_set_flush_wait_cb(ldisp, moon_flush_wait_cb);
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
     * the core-1 pipeline tasks are never starved by it. */
    if (s_panel) {
        if (xTaskCreatePinnedToCore(moon_panel_watchdog_task, "panel_wdg", 4096, NULL, 3, &s_panel_watchdog, 0) !=
            pdPASS) {
            ESP_LOGW(TAG, "panel watchdog task failed to start");
        } else {
            s_panel_watchdog_armed = true;
            ESP_LOGI(TAG, "panel-io watchdog armed (%lu ms stuck limit)", (unsigned long)PANEL_IO_WATCHDOG_LIMIT_MS);
        }
    }
#if !defined(CONFIG_BOARD_P4)
    /* The io trans-done witness (the drain probe's own-completion proof;
     * moon_panel_witness_arm chains the LVGL adapter bridge's handler,
     * which registered first -- bsp_display_start precedes moon_ui_init). */
    moon_panel_witness_arm();
#endif
    /* The sky-event day-scan worker (see moon_event_classify_here):
     * core 1 (IDLE1 is not WDT-watched, so its 15-20 s scan can never
     * trip the watchdog), below the bake tasks in priority, and 12 KB
     * of stack -- the era chain needs the headroom
     * (2026-11-04 lesson). */
    if (xTaskCreatePinnedToCoreWithCaps(moon_event_task, "moon_event", 12288, NULL, configMAX_PRIORITIES - 4,
                                        &s_event_task, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGW(TAG, "event worker task failed to start; day scans stay on the pipeline tasks");
    }
    s_ready = true;
    ESP_LOGI(TAG, "moon UI ready (frame=%p rgb=%p)", (void *)s_frame, (void *)s_rgb);
}

lv_obj_t *moon_ui_create_image(lv_obj_t *parent) {
    if (!s_ready)
        return NULL;
    /* The static moon image: the MOON_PX disk at the buffer's native
     * size -- no animation scales or rotates it any more. */
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

void moon_ui_bake_now(int64_t utc_epoch) {
    if (!s_ready)
        return;
    s_baked_day = utc_epoch / 86400;
    moon_bake_epoch(utc_epoch);
}

void moon_ui_start(void) {
    if (!s_ready || s_started)
        return;
    s_started = true;
    int64_t now = 0;
    const bool have_time = (clock_get_utc_epoch(&now) == ESP_OK);
#if CONFIG_UI_MOON_BOOT_REPLAY && !CONFIG_UI_DEMO_MODE
    /* Boot replay (UI_MOON_BOOT_REPLAY, the replay section): ALL stored
     * TF hour frames play ascending on the panel's boot black and stop
     * on the newest; the live-computed current phase lands as the final
     * frame right after -- produced on core 1, so the boot task never
     * bakes synchronously on this path. With no usable cache the panel
     * simply stays black until that now-frame lands (the replay itself
     * needs no clock -- it is pure storage playback; only the final bake
     * does). The presentation holds the idle->sleep ladder (the boot
     * re-armed it via action_wake_screen right before this call -- a 60 s
     * idle timeout must not sleep into a minutes-long animation); the
     * tick's end leg re-arms it when the presentation is over. */
    moon_orbit_interrupt(); /* the replay producer owns the buffers */
    s_replay_abort = false;
    s_replay_deadline_ms = esp_timer_get_time() / 1000 + CONFIG_UI_MOON_REPLAY_BUDGET_MS;
    s_replay_presenting = true;
    if (!clock_is_idle())
        clock_idle_set_enabled(false);
    if (xTaskCreatePinnedToCore(moon_boot_replay_task, "moon_replay", 12288, NULL, configMAX_PRIORITIES - 3,
                                &s_replay_task, 1) == pdPASS) {
        lv_timer_resume(s_tick);
        return;
    }
    /* Producer could not start: restore the ladder and fall through to
     * the synchronous boot bake below (today's exact posture). */
    ESP_LOGW(TAG, "boot replay task failed; baking the current phase inline");
    s_replay_presenting = false;
    if (!clock_is_idle())
        clock_idle_set_enabled(true);
#endif
    if (!have_time) {
        /* No trustworthy time (RTC not set / no NTP): show nothing rather
         * than a wrong phase -- the moon starts on the black screen the
         * init cleared and STAYS black. The daily day-tick (re-bake)
         * picks it up once the clock syncs. */
        ESP_LOGW(TAG, "no UTC time yet; moon stays blank until the clock syncs");
        lv_timer_resume(s_tick);
        return;
    }
    /* Boot (replay off / task-create fallback): bake the CURRENT phase
     * directly (the synodic boot sweep was removed 2026-11-13 -- the home
     * is the static phase from the first frame). The screen shows black
     * until the bake lands and the tick displays it as one atomic frame;
     * moon_bake_epoch records the phase for the orbit pipeline
     * (s_orbit_epoch) and the day-tick (s_baked_day below, so the minute
     * path does not queue a second bake). */
    moon_orbit_interrupt(); /* the boot bake owns the buffers */
    s_baked_day = now / 86400;
    moon_bake_epoch(now);
    lv_timer_resume(s_tick);
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
    bool new_day = day != s_baked_day;
    /* Sky-event + hourly probe (runs every minute, from the minute
     * tick): a lunar eclipse starts and ends at the hour it happens,
     * NOT at the UTC midnight the date change sees, and the harvest /
     * blue / super tints only make sense when the day is actually full
     * (the is_full test's ±18 h window around F). Re-bake when the event's
     * 5-bit signature changes (blood turns on as the Moon enters the
     * shadow, off at the exit) -- the re-bake picks up the per-pixel
     * occlusion and the tint LUT. The once-per-hour refresh
     * (moon_hourly_probe, the hourly section) subsumes the old per-hour
     * blood-moon re-bake: while a blood moon is up the hour-centre bake
     * already carries the occlusion, so the hourly refresh tracks the
     * bite's drift across the disk on its own. The probe is two
     * moon_geometry calls plus the blue-moon back-step, cached per
     * calendar day in moon_event.c (one ~350-step scan per day, not per
     * minute -- taken on the event worker / a pipeline task, never on
     * the LVGL task; see moon_event_classify_here). */
    if (s_orbit_baking || s_orbit_sweeping || s_hourly_baking || s_hourly_start || moon_ui_replaying()) {
        /* A pipeline owns s_frame/s_rgb until it finishes (the boot
         * replay included -- its producer commits s_baked_day only when
         * the now-frame lands, so deferring here costs nothing); defer
         * (do NOT advance s_baked_day) so the next tick re-tries and the
         * bakes never race on the buffers. The guard sits BEFORE the
         * classify probe on purpose: while a pipeline task runs, that
         * task classifies its own epoch and warms the day cache -- the
         * LVGL task never pays for the scan here
         * (moon_event_classify_here). */
        return;
    }
    moon_event_t ev;
    if (!moon_event_classify_here(now, &ev))
        return; /* cold day: the event worker (core 1) takes the scan;
                 * the probe continues on a later tick, once warm */
    const bool ev_change = moon_event_signature(&ev) != s_baked_event;
    /* Once-per-hour refresh: on the hour it fades to black and redraws
     * (the blood bite rides the hour-centre bake). Taking the hour here
     * skips the competing day/event re-bake for it. */
    const bool hourly = moon_hourly_probe(now);
    if (!new_day && !ev_change && !hourly)
        return;
    ESP_LOGI(TAG, "deferring moon re-bake (new day=%d event sig=%d->%d)", new_day, s_baked_event,
             moon_event_signature(&ev));
    s_baked_day = day;
    if (!hourly)
        moon_rebake_request(now);
}

void moon_ui_pause(bool pause) {
    if (!s_tick)
        return;
    if (pause) {
        /* An hourly cycle caught mid-flight must never hand a black panel
         * to the sleep screens (the #34 fade-starve lesson, this time on
         * the sleep path): drop the pending legs and restore the brightness
         * NOW, before the tick pauses. The producer, if it is still baking,
         * keeps filling s_hourly_gray/s_rgb off the LVGL task -- harmless
         * while the tick is paused (nothing flushes), and the wake shows the
         * fresh frame (the next day-tick commits s_baked_hour when it
         * finally shows). */
        if (s_hourly_start || s_hourly_baking || s_hourly_done || s_hourly_failed || s_hourly_faded) {
            s_hourly_start = s_hourly_done = s_hourly_failed = s_hourly_baking = false;
            moon_hourly_finish(0); /* restores brightness if we had dimmed */
            ESP_LOGI(TAG, "sleep entry mid-hourly: brightness restored, legs dropped");
        }
        /* A boot replay caught mid-flight by a sleep entry / the alarm
         * screen: abort the animation and drop the un-shown frames (no
         * brightness is owed -- the replay never fades). The producer is
         * still parked on core 1 (fetch or the bounded show handshake):
         * it observes the abort, skips its final bake (the wake's
         * day-tick re-bake covers the stale stored frame via the
         * untouched s_baked_day = 0), and it -- NOT us -- clears
         * baking/presenting on exit. The ladder is owned by the sleep
         * path from here; the wake re-arms it as always. */
        if (moon_ui_replaying()) {
            s_replay_abort = true;
            s_replay_show = false;
            s_replay_final = false;
#if CONFIG_UI_MOON_BAKE_REVEAL
            if (s_reveal_dirty) { /* the final "now" bake caught mid-reveal:
                                   * heal the last complete frame over the
                                   * half-drawn square BEFORE the tick pauses
                                   * (the sleep screens must not inherit the
                                   * black canvas + strips). The producer
                                   * sees the abort, skips its publish. */
                s_reveal_stop = true;
                moon_reveal_restore();
            }
#endif
            /* s_replay_end stays as the producer left it: the tick's end
             * leg re-arms only on a moon screen while not idle, so the
             * sleep path is never re-armed underneath -- and a race with
             * the wake's own re-arm is idempotent. */
            ESP_LOGI(TAG, "sleep entry mid-replay: animation aborted (baking=%d)", (int)s_replay_baking);
        }
        /* Sleep entry (and the alarm screen): interrupt the orbit
         * (sleep shows the last baked viewport), then pause the tick.
         * The on-screen frame simply stays as it is under the sleep
         * screens -- the static dim IS the whole sleep transition now
         * (the dusk pole spin retired 2026-11-13). The join is bounded
         * and runs in the LVGL task, so the display stays live the
         * whole way. */
        moon_orbit_interrupt(); /* sleep shows the last baked viewport */
        lv_timer_pause(s_tick);
    } else {
        /* Wake (any input: touch, tilt, alarm dismiss): a mid-bake
         * abort (a sleep transition that landed on an in-flight bake)
         * left s_frame/s_rgb partial -- re-bake the current phase,
         * DEFERRED: the frame stays flagged stale (a completed bake
         * clears it) and the tick's rebake leg displays the fresh
         * frame as ONE atomic whole-panel flush once the core-1 bake
         * lands it (~1.3 s, off the LVGL task, the UI stays live). The
         * rolled-over-day re-bake is moon_ui_day_tick's, which
         * action_wake_screen runs right after this. */
        if (s_frame_stale && s_started) {
            int64_t now;
            if (clock_get_utc_epoch(&now) == ESP_OK) {
                ESP_LOGI(TAG, "frame was mid-bake at sleep; deferring the wake re-bake");
                s_baked_day = now / 86400;
                moon_rebake_request(now);
            }
        }
        lv_timer_resume(s_tick);
    }
}

/* A screen switch happened (the plain lv_scr_load in loadScreen):
 * display the new screen's contents -- the moon image included -- as
 * ONE atomic whole-panel flush, the same atomic rule every pipeline
 * frame obeys. lv_scr_load invalidates the whole new screen; without
 * this immediate flush LVGL's async 25-row strip drain would repaint it
 * top-to-bottom over ~a second (the wake artifact).
 *
 * The flush is safe whenever s_rgb holds a complete frame -- the steady
 * state (s_rgb holds the last complete frame) and an orbit sweep in
 * flight (s_rgb fully converted, the tick is draining its strips). It
 * is unsafe ONLY while a bake task is actively rewriting s_rgb row by
 * row (s_orbit_baking): that leaves a temporarily
 * mixed frame (new top rows over old bottom rows) that a full flush
 * would put on screen. Skip in that case: the bake's completion display
 * (or another pipeline's completion leg) lands the new screen atomically.
 *
 * Must be on the LVGL task (8 KB stack) -- a whole-panel lv_refr_now
 * overflows the 3.5 KB main task at boot; off it the invalidated screen
 * is painted by the LVGL task's async refresh (a boot crawl is
 * acceptable, the moon is not baked yet anyway). */

/* Does the screen just loaded carry a moon image? The images persist
 * (created once at boot); only the ACTIVE screen matters. This is the
 * pipelines' display gate: frames are only produced and shown while a
 * moon-carrying screen is active (a bake on a moon-less screen would
 * park on the withheld display handshake). */
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
 * scales everything on screen at once (the moon, the labels) with no
 * overlay to composite and no per-step
 * re-render. The old overlay fade charged one full-screen lv_refr_now
 * per step (~175 ms of QSPI strip traffic each, 4 steps per leg) and
 * the panel's left-to-right strip fill showed as a slow crawl on every
 * switch; here a fade step is one ~100 us SPI param transaction, so
 * the steps are many and short.
 *
 *   fade-out: the brightness ramps 100 -> 0 in FADE_STEPS steps of
 *   CONFIG_UI_SCREEN_SWITCH_FADE / FADE_STEPS ms. Nothing animates --
 *   the static panel simply dims. s_fade_pending is left set for the
 *   fade-in.
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
static volatile bool s_fade_busy; /* a 0x51 ramp is running: the other
                                      brightness writers hold off and the LVGL
                                      task is between flushes, so the ramp's
                                      0x51 writes are the ONLY panel-io traffic
                                      in flight */

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
 * (0..100) in FADE_STEPS steps, paced `ms / FADE_STEPS` ms between steps.
 * Callers pass the leg time: the screen-switch fade / the dim ramp pass
 * CONFIG_UI_SCREEN_SWITCH_FADE, the hourly refresh passes
 * CONFIG_UI_MOON_HOURLY_FADE_MS. Each step is one 0x51 SPI param
 * transaction (~100 us): the ramp is free, so the steps are many and
 * short (the luminance steps stay small enough not to read as
 * discrete steps). Each write takes the panel mutex: a 0x51 write must
 * not land in the middle of a whole-panel flush (the
 * panel-io driver's bus lock serializes them, but a 0x51 queued behind
 * a queued strip that never completes parks the ramp's writer forever --
 * the freeze this project hit). The ramp runs on the LVGL task between
 * the atomic flushes, and s_fade_busy makes the 0x51 ramps mutually
 * exclusive, so in practice the mutex is free when the ramp runs. */
static void fade_ramp(int from_pct, int to_pct, int ms) {
    if (panel_hold_live())
        return; /* storm: no write lands, so the panel stays at its tracked
                 * level -- s_panel_pct untouched is the truthful bookkeeping */
    for (int s = 1; s <= FADE_STEPS; s++) {
        int pct = from_pct + (to_pct - from_pct) * s / FADE_STEPS;
        xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
        bsp_display_brightness_set(pct);
        xSemaphoreGive(s_panel_mtx);
        if (s < FADE_STEPS)
            vTaskDelay(pdMS_TO_TICKS(ms / FADE_STEPS));
    }
    s_panel_pct = to_pct; /* the steps are portMAX_DELAY: by the end the
                           * panel is unambiguously at the target */
}

/* The hourly refresh's completion (LVGL task), called from moon_hourly_service
 * and from moon_ui_pause's sleep hand-off. show_new=1 (the producer landed a
 * frame): display it as ONE atomic whole-panel flush, then (the fade's rule)
 * ramp the brightness back -- and commit s_baked_hour / s_baked_event now
 * that the frame is actually up (commit-on-show). show_new=0 (a failed or
 * blown-deadline cycle, or the sleep hand-off): no flush (s_rgb still holds
 * the last complete frame), only restore the brightness if we had dimmed.
 * The fade-in is owed exactly while s_hourly_faded is set, so a cycle that
 * never dimmed (spawn failure before the ramp) restores nothing -- and the
 * brightness never strands black (the #34 rule, honored on every exit).
 * With the bake reveal on, the FIRST completed strip (the reveal leg)
 * usually paid the fade-in before this runs and the flag is found false
 * here; any path that reaches this point with a half-revealed square on
 * screen (s_reveal_dirty) heals the old frame from the untouched s_frame
 * first -- before the ramp back, so the brightness never rises over a
 * half-moon. */
static void moon_hourly_finish(int show_new) {
    if (show_new) {
        moon_invalidate_all();
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        moon_panel_drain_verify("hourly fade-in");
        s_baked_hour = s_hourly_epoch / 3600; /* commit-on-show */
        s_baked_event = s_hourly_event_sig;
#if CONFIG_UI_MOON_BAKE_REVEAL
        s_reveal_dirty = false; /* the publish rewrote all of s_rgb and this
                                 * flush made it the visible truth: no heal
                                 * owed, even after a strip-by-strip reveal */
#endif
    }
#if CONFIG_UI_MOON_BAKE_REVEAL
    if (s_reveal_dirty) { /* failed / blown-budget / sleep-hand-off exit
                           * caught mid-reveal: stop the producer's reveal and
                           * heal the old frame back from the untouched
                           * s_frame, before the brightness climbs. */
        s_reveal_stop = true;
        moon_reveal_restore();
    }
#endif
    if (s_hourly_faded) {
        s_hourly_faded = false;
        fade_ramp(0, 100, CONFIG_UI_MOON_HOURLY_FADE_MS);
        s_fade_busy = false; /* the other 0x51 writers may run again */
        ESP_LOGI(TAG, "hourly: panel brightness 0 -> 100 (%d ms)", CONFIG_UI_MOON_HOURLY_FADE_MS);
    }
}

/* Dim the panel to black ahead of a screen switch (LVGL task;
 * loadScreen calls it before lv_scr_load on every switch except the
 * both-moon one). No pixels are written; the 0x51 ramp is the only
 * panel-io traffic in flight (s_fade_busy -- the other brightness ramps
 * hold off) so a lost-DMA-completion stall cannot strand it
 * behind a queued strip (~240 ms of dark panel while the switch
 * completes). The fade-in leg (moon_ui_screen_switched)
 * displays the new screen while the panel is still black and ramps the
 * brightness back. No-op when the fade is off, the moon UI is not
 * ready, or a bake pipeline owns the buffers. */
void moon_ui_fade_out(void) {
    if (!s_ready || !moon_on_lvgl() || CONFIG_UI_SCREEN_SWITCH_FADE <= 0 || s_orbit_baking)
        return;
    int from = s_panel_pct;
    s_fade_pending = true; /* the fade-in (0 -> 100) is owed either way */
    if (from > 0) {
        s_fade_busy = true; /* the other 0x51 writers hold off until the
                             * screen switch (moon_ui_screen_switched) */
        fade_ramp(from, 0, CONFIG_UI_SCREEN_SWITCH_FADE);
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
 * flush's in-flight strip DMA -- the panel-io driver's bus lock
 * would serialize it anyway, and if one of those DMAs never completes the
 * queued 0x51 parks this caller forever (the settings-entry freeze). The
 * fade ramp (same file) and the LVGL flushes (s_panel_mtx) are the
 * other users of the lock; this is the one for the wake / deep-sleep /
 * boot backlight paths (actions.c, main.cpp). When the moon UI is not
 * ready the BSP call runs directly (the panel is quiet at boot).
 *
 * The mutex keeps the 0x51 from QUEUEING behind a strip; it does not make
 * it the only traffic IN FLIGHT -- and a 0x51 interleaved with strip DMAs
 * is the one ingredient common to every lost-DMA-completion
 * stall (the wake-repro hammer: PANEL IO STUCK ~6 s after a DEEP wake,
 * while the wake's 0x51 restore raced LVGL's flushes). So this caller
 * obeys the fade's rule too: hold s_fade_busy across the write so the
 * other 0x51 writers (fade/dim ramps) hold off (the LVGL flushes are
 * already serialized by the mutex). If a real fade/dim ramp already owns
 * the flag, take/restore nothing (the held check
 * also keeps a nested call from clearing a fade's flag early). */
esp_err_t moon_ui_panel_brightness_set(int pct) {
    esp_err_t r;
    if (s_ready && panel_hold_live())
        return ESP_OK; /* storm: the 0x51 would ride the same bus; the wake
                        * restored the level BEFORE the hold arms, so nothing
                        * is owed -- and s_panel_pct stays truthful */
    if (s_ready && s_panel_mtx) {
        bool held = s_fade_busy;
        if (!held)
            s_fade_busy = true; /* the 0x51 rides alone (the fade's rule) */
        xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
        r = bsp_display_brightness_set(pct);
        xSemaphoreGive(s_panel_mtx);
        if (!held)
            s_fade_busy = false;
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
 * s_fade_busy WITHOUT s_fade_pending -- the other 0x51 writers hold off
 * for the ramp's duration (the 0x51 steps must be the only panel-io
 * traffic in flight, the fade's rule) but no fade-in is owed afterwards:
 * the next screen switch paints plain (s_fade_pending is untouched).
 * The sleep frame is static -- there is nothing displaying behind the
 * ramp to resume. No-op when
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
    if (panel_hold_live())
        return;                                            /* storm: skipped whole (fade_ramp drops its steps anyway;
                                                            * s_panel_pct stays truthful -- the panel never moved) */
    s_fade_busy = true;                                    /* the other 0x51 writers hold off for the ramp */
    fade_ramp(from, to_pct, CONFIG_UI_SCREEN_SWITCH_FADE); /* also tracks s_panel_pct */
    s_fade_busy = false;
    ESP_LOGI(TAG, "panel dim ramp: brightness %d -> %d%% (%d ms)", from, to_pct, CONFIG_UI_SCREEN_SWITCH_FADE);
}

void moon_ui_screen_switched(void) {
    if (s_ready && moon_on_lvgl()) {
        /* Track whether the active screen carries a moon image (the
         * pipelines' display gate: rendering on a moon-less screen
         * would park on the withheld display handshake). */
        s_moon_screen = moon_screen_has_image();
        /* Left the moon screen mid-replay (settings, an alarm hand-off):
         * abort the animation and drop the un-shown frames -- showing a
         * stored-hour frame on a non-moon screen is moot and would burn
         * panel traffic. The producer observes the abort, skips its final
         * bake (s_baked_day stays 0 -> the day-tick re-bakes the current
         * phase once the user is home), and clears baking/presenting on
         * exit; the end leg re-arms the ladder only if the moon screen
         * is back up by then, else the airplane-screen entry does it. */
        if (!s_moon_screen && moon_ui_replaying()) {
            s_replay_abort = true;
            s_replay_show = false;
            s_replay_final = false;
            ESP_LOGI(TAG, "screen left mid-replay: animation aborted (baking=%d)", (int)s_replay_baking);
        }
    }
    if (!s_ready || !moon_on_lvgl() || s_orbit_baking) {
        if (s_fade_pending && s_panel_pct == 0) {
            /* A starved fade-in: the fade-out ramp LANDED physically
             * (the panel sits at true black, s_panel_pct says so), but
             * this branch paints the new screen without the fade-in
             * ramp. The bake flag won the race between
             * moon_ui_fade_out() and here -- the boot bake sets it
             * before the switch completes -- and without this rescue
             * the panel strands BLACK with a fully live app (P4 bench,
             * 2026-11-13). Restore now, holding s_panel_mtx per the
             * 0x51 rule; s_fade_busy was set by the fade-out and
             * clears at the tail below. Ignore
             * the hold here: strand-black outranks one 0x51 riding the
             * storm bus -- the wake path makes the same write. */
            if (s_ready && s_panel_mtx) {
                xSemaphoreTake(s_panel_mtx, portMAX_DELAY);
                bsp_display_brightness_set(100);
                xSemaphoreGive(s_panel_mtx);
            } else {
                bsp_display_brightness_set(100);
            }
            s_panel_pct = 100;
            ESP_LOGI(TAG, "screen switch: fade-in starved by the bake guard, brightness 0 -> 100");
        }
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
         * and the ramp's 0x51 writes take it per step. s_fade_busy
         * keeps the other 0x51 writers off, so the only panel-io
         * traffic in flight is the flush's own. */
        s_fade_pending = false;
        s_hold_pass_switch = true; /* the switch frame passes the hold guard */
        xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
        lv_refr_now(NULL);
        xSemaphoreGive(s_rgb_mtx);
        s_hold_pass_switch = false;
        moon_panel_drain_verify("switch fade-in");
        fade_ramp(0, 100, CONFIG_UI_SCREEN_SWITCH_FADE);
        s_fade_busy = false; /* the other 0x51 writers may run again */
        ESP_LOGI(TAG, "screen switch fade-in: panel brightness 0 -> 100 (%d ms)", CONFIG_UI_SCREEN_SWITCH_FADE);
        return;
    }
    s_hold_pass_switch = true; /* the switch frame passes the hold guard */
    xSemaphoreTake(s_rgb_mtx, portMAX_DELAY);
    lv_refr_now(NULL);
    xSemaphoreGive(s_rgb_mtx);
    s_hold_pass_switch = false;
    moon_panel_drain_verify("screen switch");
}

/* Hang-diagnostic context dump (TEST_HANG_WATCH only; the hang-watcher in
 * app_main calls it when the LVGL heartbeat goes stale). Prints the volatile
 * pipeline flags around the fade / flush path, so the task-state dump
 * (which names the BLOCKED tasks) can be cross-referenced with the pipeline
 * state to identify WHICH portMAX_DELAY take a stuck task is parked on. The
 * only project locks on that path are s_rgb_mtx (buffer) and s_panel_mtx
 * (panel); a deadlock must involve one of them (or the panel io's internal
 * SPI lock). Safe from any task (volatile reads only). */
void moon_ui_dump_hang_context(void) {
    ESP_LOGE(TAG, "  flags: ready=%d moon_screen=%d fade_pending=%d", (int)s_ready, (int)s_moon_screen,
             (int)s_fade_pending);
    ESP_LOGE(TAG, "  flags: panel_pct=%d fade_busy=%d", (int)s_panel_pct, (int)s_fade_busy);
    ESP_LOGE(TAG, "  flags: orbit_baking=%d orbit_sweep=%d frame_stale=%d", (int)s_orbit_baking, (int)s_orbit_sweeping,
             (int)s_frame_stale);
    ESP_LOGE(TAG, "  flags: rebake_baking=%d rebake_show=%d panel_wrap=%d", (int)s_rebake_baking, (int)s_rebake_show,
             (int)s_panel_wrap);
    ESP_LOGE(TAG, "  flags: hourly start=%d baking=%d done=%d failed=%d faded=%d", (int)s_hourly_start,
             (int)s_hourly_baking, (int)s_hourly_done, (int)s_hourly_failed, (int)s_hourly_faded);
    ESP_LOGE(TAG, "  flags: replay presenting=%d baking=%d show=%d final=%d end=%d abort=%d", (int)s_replay_presenting,
             (int)s_replay_baking, (int)s_replay_show, (int)s_replay_final, (int)s_replay_end, (int)s_replay_abort);
    ESP_LOGE(TAG, "  flags: reveal owner=%d pub=%u shown=%u clear=%d dirty=%d stop=%d", (int)s_reveal_owner,
             (unsigned)s_reveal_pub, (unsigned)s_reveal_shown, (int)s_reveal_clear, (int)s_reveal_dirty,
             (int)s_reveal_stop);
}
