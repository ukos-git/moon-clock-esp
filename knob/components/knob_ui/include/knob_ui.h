/**
 * @file knob_ui.h
 * @brief The knob's single view (K7b static + K7c sweep/day tick + K7d
 *        real time): the physical moon disk + a caption row. Glue only —
 *        the bake path lives in knob_moon (the verbatim ported core), the
 *        wall clock in knob_time; the dial stays idle until K7e
 *        (settings/power) gives it a job.
 *
 * Threading (K7c): a producer task bakes into the back PSRAM buffer and
 * publishes a frame counter; a swap task (holding the LVGL mutex handed
 * in via knob_ui_set_lvgl_lock) swaps the image descriptor, runs ONE
 * atomic lv_refr_now and acks. The producer never touches a buffer the
 * panel might still be reading (it waits for the ack before reusing).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h" /* must precede semphr.h (IDF rule) */
#include "freertos/semphr.h"
#include "lvgl.h" /* lv_obj_t for the P1 attach API */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hand the view the mutex that guards LVGL (the one lvgl_task
 *         holds around lv_timer_handler in main.c), BEFORE knob_ui_create.
 *         The swap task takes it around every descriptor swap + flush. */
void knob_ui_set_lvgl_lock(SemaphoreHandle_t mux);

/** @brief Build the screen (image objects + caption), before the LVGL task
 *         starts — same context as the old K1 test screen. Allocates the
 *         PSRAM double buffer; the caption reads "moon unavailable" until
 *         the first frame lands. */
void knob_ui_create(void);

/** @brief The honest pre-frame state (K7d): assets loaded, no trusted
 *         time yet — caption "no time", disk stays hidden. A no-op once
 *         a frame path is armed or the buffers failed (the caption then
 *         already names the real failure). */
void knob_ui_show_no_time(void);

/** @brief Cold-boot sweep capability (Kconfig KNOB_MOON_SWEEP_FRAMES > 0). */
bool knob_ui_sweep_enabled(void);

/**
 * @brief Show one UTC instant statically and drop into the idle day-tick
 *        loop (1 Hz UTC check; re-bakes on date change or event-signature
 *        change, K7c). Also the deep-sleep-wake posture: current phase,
 *        no sweep (the clock's wake policy).
 *
 * Arms only: the classify + crisp bake run on the producer task (the
 * ~20 s core scan must never touch a TWDT-registered task), so the
 * caller returns at once and the producer + swap tasks are up. Calling
 * it again while armed (the K7d SNTP hook: a real time landing after
 * the sweep window) re-bakes the current phase within a second.
 *
 * @return false if PSRAM or the assets failed (caption stays honest)
 */
bool knob_ui_show_moon(int64_t utc_epoch);

/**
 * @brief Cold-boot-only synodic sweep (new moon -> now), then the same
 *        idle day-tick loop. Never-black: the caption counts
 *        "sweeping n/N" from frame 1, and a screen press aborts the
 *        sweep — the display then lands on the true current phase.
 *
 * Returns immediately after arming the sweep (the boot core must go on
 * starting the LVGL task; the bake runs on the producer).
 */
void knob_ui_start_sweep(int64_t now_epoch);

/* ---- P1 render-seam surface (used by knob_moon_render.c) --------------
 * The shared home screen (components/ui screens.c moon_screen_prepare)
 * hosts the moon lv_image; the producer/swap core above drives it through
 * the SAME descriptor path as the K7 own-screen view. All attach calls
 * share the one PSRAM double buffer, exactly like the clocks'
 * moon_ui_create_image across its four moon screens. */

/** @brief Allocate the PSRAM double buffer + descriptor without building
 *         any LVGL objects (shared-UI mode: the screens come from
 *         ui_init/create_screens; knob_ui_create stays the legacy
 *         own-screen path). @return false on PSRAM failure. */
bool knob_ui_alloc_frames(void);

/** @brief Create one moon lv_image on parent pointing at the shared
 *         descriptor (hidden until the first frame lands, visible
 *         immediately if a frame is already up). Returns NULL when the
 *         buffers never came up or the attach list is full (8). */
lv_obj_t *knob_ui_attach_moon_image(lv_obj_t *parent);

/** @brief Invalidate every attached moon image (LVGL task; the clock's
 *         moon_ui_refresh posture for external reparent/visibility
 *         changes). */
void knob_ui_invalidate_all(void);

/** @brief True while the synodic boot sweep is playing (the shared
 *         on-demand-sync flow gates on it, actions.c). */
bool knob_ui_sweeping(void);

/** @brief A screen press during the synodic boot sweep (task #28): the
 *         shared home's press observer rides this through the render
 *         seam; the legacy own-screen view calls it directly. The row
 *         loop honors the abort within one row, then lands the true
 *         current phase. @return true if a sweep was playing and was
 *         told to abort (the caller may then consume the gesture). */
bool knob_ui_sweep_press(void);

/** @brief Attach the shared home's progress row to the producer's
 *         caption staging (task #28): the swap task writes it under the
 *         LVGL lock exactly like the legacy caption label, but GATED --
 *         it reveals itself only while the staged text is the sweep
 *         progress ("sweeping n/N") or an honest failure state ("no
 *         time", "moon unavailable"); the shared clock row owns the
 *         daily text there. The render seam owns the label; passing it
 *         also enables the gated visibility (NULL detaches). */
void knob_ui_bind_caption(lv_obj_t *label);

/** @brief Override the baked disk geometry (task #35, 2026-11-13). The
 *         legacy own-screen view bakes r 150 at cy 168 — its bottom
 *         caption row owns the foot band. The SHARED home has no bottom
 *         caption (the clock row owns the daily text), so its render
 *         seam centres and fills the disk like the clocks' MOON_PX one
 *         and lets the battery gauge / progress row overlap it. Set
 *         before the first bake (the seam does it in moon_ui_init). */
void knob_ui_set_disk_geom(float r, int cy);

/** @brief True once the first frame has been applied to the panel. */
bool knob_ui_shown(void);

/** @brief Take/give the LVGL mutex handed in via knob_ui_set_lvgl_lock
 *         (the seam's own object writes ride the same lock; recursive
 *         take from the LVGL task itself is fine — IDF mutexes are). */
void knob_ui_lvgl_lock(void);
void knob_ui_lvgl_unlock(void);

/** @brief Dim parity with the clocks' dusk (P1): drop the shared clock
 *         widget to CONFIG_UI_CLOCK_ALPHA_PCT opacity alongside the
 *         board's 25 % LEDC backlight, restore on activity. Registered
 *         as the knob_power ladder's dim observer; takes the LVGL lock
 *         itself, so it is safe from the supervisor task. No-op on the
 *         legacy own-screen view (no shared objects were ever created). */
void knob_render_set_home_dim(bool dimmed);

/* ---- last-gasp black box (field-crash witness) -------------------------
 * The 2026-11-11 sweep-phase TG0WDT panic reset drained the USB-CDC TX
 * FIFO before any backtrace flushed: the crash was physically unwitness-
 * able (three reproduced-free bench sessions confirmed the freeze cannot
 * be provoked on demand). The gasp marker is 24 bytes of RTC_NOINIT mem-
 * ory updated with single atomic word/byte stores on the producer/swap/
 * seam/flush hot path, so the board reports WHERE it was when the last
 * crash hit, on the boot after. Reported once (magic cleared) and only
 * for crash reset reasons, so deep-sleep/dial wakes stay quiet. */
typedef enum {
    KNOB_GASP_IDLE = 0,
    KNOB_GASP_BAKE = 10,         /* producer inside bake_px (moon core) */
    KNOB_GASP_WAIT_ACK = 11,     /* producer waiting for the swap to ack */
    KNOB_GASP_PUBLISH = 12,      /* producer between bake return and s_pub++ */
    KNOB_GASP_SWAP_LOCK = 20,    /* swap blocked taking the LVGL lock */
    KNOB_GASP_SWAP_REFR = 21,    /* swap inside lv_refr_now (panel drain) */
    KNOB_GASP_SWAP_DONE = 22,    /* swap released the lock, frame visible */
    KNOB_GASP_SWITCH_ENTER = 30, /* seam: screen_switched entry */
    KNOB_GASP_SWITCH_LOCK = 31,  /* seam: taking lock for atomic refr */
    KNOB_GASP_SWITCH_RAMP = 32,  /* seam: LEDC fade-in ramp */
    KNOB_GASP_FLUSH = 40,        /* draw_bitmap call issued */
    KNOB_GASP_FLUSH_DONE = 41,   /* GPSPI trans-done ISR (frame streamed) */
    KNOB_GASP_LAND_BAKE = 50,    /* crisp current-phase bake after sweep */
} knob_gasp_stage_t;

/** @brief Record the executing section (hot path: one byte store). */
void knob_ui_gasp(knob_gasp_stage_t stage);

/** @brief Refresh the frame/publish mirrors (producer + swap call sites). */
void knob_ui_gasp_frames(uint32_t pub, uint32_t applied);

/** @brief Refresh the backlight mirrors (seam's LEDC paths). */
void knob_ui_gasp_backlight(int duty, bool fade_pending);

/** @brief Print the gasp marker + reset reason exactly once, on a boot
 *         whose reset reason is a crash (WDT/panic/int-wdt); no-op for
 *         power-on/dial/tap/deep-sleep wakes. Call from app_main once
 *         logging is up. */
void knob_ui_gasp_boot_report(void);

#ifdef __cplusplus
}
#endif
