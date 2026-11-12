/**
 * @file knob_moon_render.c
 * @brief The P1 render seam: the moon_ui.h API implemented over the
 *        proven knob_ui producer/swap core (the plan's D-seam).
 *
 * The shared screens (components/ui screens.c/ui.c/actions.c) call the
 * moon_ui.h surface unchanged. On the two AMOLED clocks that surface is
 * moon_ui.c: PSRAM buffers, spin, orbit, dusk, TF cache, 0x51 fades,
 * keep-masks. The knob is a backlit LCD with no IMU, no codec and no
 * direct-panel-write path, so most of that machinery has no analogue --
 * the honest mapping:
 *
 *   buffers/images/refresh  -> knob_ui double buffer + attach list
 *   sweep / bake-now        -> knob_ui producer (already family-correct)
 *   day tick                -> NO-OP: the knob producer runs the day tick
 *                              itself on CPU1 (1 Hz UTC check; the clock's
 *                              LVGL-task probe was the S3 minute-freeze
 *                              root cause -- the knob never puts it there)
 *   pause                   -> NO-OP (a static moon needs no parking)
 *   dusk                    -> false (no pole spin; the caller's fallback
 *                              arms the esp_timer countdown, which is
 *                              awake-only here -- knob_power owns the
 *                              real deep-sleep arming)
 *   tilt settled            -> true (no IMU: always at rest)
 *   fade / dim / brightness -> LEDC ramps (no 0x51 to wedge: the backlight
 *                              is a GPIO PWM, safe from any task)
 *   screen_switched         -> ONE atomic lv_refr_now (the family rule;
 *                              LVGL partial flush carries it to the panel)
 *   panel step / keep-mask  -> NO-OP (no direct panel writes to pre-rotate)
 *
 * Exactly two lv_refr_now owners stay true: the knob swap task and this
 * seam (the clock's screen-switch path). Settings screens (moon-less)
 * never receive moon-frame refreshes -- the swap task only ever touches
 * the attached moon images.
 */
#include "moon_ui.h"

#include "actions.h"

#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_ui.h"

static const char *TAG = "knob_render";

/* #ifndef-seeded (repo invariant: Kconfig 'n'/absent leaves the macro
 * UNDEFINED; these seed the Kconfig defaults, never redefine them). */
#ifndef CONFIG_UI_SCREEN_SWITCH_FADE
#define CONFIG_UI_SCREEN_SWITCH_FADE 240
#endif
#ifndef CONFIG_UI_CLOCK_ALPHA_PCT
#define CONFIG_UI_CLOCK_ALPHA_PCT 20
#endif

#define FADE_RAMP_STEP_MS 10
#define BRIGHT_STEPS 100 /* LEDC duty quantisation for percent math */

static bool s_ready;              /* buffers + descriptor up (moon_ui_init) */
static bool s_started;            /* moon_ui_start seen (log parity only) */
static volatile int s_duty;       /* LEDC mirror 0..255 (single writer per ramp) */
static bool s_fade_pending;       /* fade_out ran; screen_switched ramps back */
static lv_obj_t *s_moon_roots[8]; /* screen roots that carry a moon image */
static int s_n_roots;

/* ---- LEDC brightness (the knob's answer to the clocks' 0x51 paths) --- */

static void ramp_to(int target_duty) {
    const int from = s_duty;
    if (from == target_duty) {
        return;
    }
    const int steps = (CONFIG_UI_SCREEN_SWITCH_FADE + FADE_RAMP_STEP_MS - 1) / FADE_RAMP_STEP_MS;
    if (steps <= 1 || CONFIG_UI_SCREEN_SWITCH_FADE == 0) {
        (void)knob_board_backlight_set((uint8_t)target_duty);
        s_duty = target_duty;
        knob_ui_gasp_backlight(s_duty, s_fade_pending);
        return;
    }
    for (int i = 1; i <= steps; i++) {
        const int d = from + ((target_duty - from) * i) / steps;
        (void)knob_board_backlight_set((uint8_t)d);
        s_duty = d;
        knob_ui_gasp_backlight(s_duty, s_fade_pending);
        vTaskDelay(pdMS_TO_TICKS(FADE_RAMP_STEP_MS));
    }
    s_duty = target_duty;
    (void)knob_board_backlight_set((uint8_t)target_duty);
    knob_ui_gasp_backlight(s_duty, s_fade_pending);
}

esp_err_t moon_ui_panel_brightness_set(int pct) {
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    const int duty = (pct * 255 + 50) / 100;
    (void)knob_board_backlight_set((uint8_t)duty);
    s_duty = duty;
    knob_ui_gasp_backlight(s_duty, s_fade_pending);
    return ESP_OK;
}

void moon_ui_panel_dim_ramp(int to_pct) {
    if (to_pct < 0) {
        to_pct = 0;
    }
    if (to_pct > 100) {
        to_pct = 100;
    }
    const int duty = (to_pct * 255 + 50) / 100;
    if (duty == s_duty) {
        return; /* "no-op when already at the target" (moon_ui.h) */
    }
    ESP_LOGI(TAG, "panel dim ramp %d%% -> %d%% (%d ms, LEDC)", (s_duty * 100 + 127) / 255, to_pct,
             (int)CONFIG_UI_SCREEN_SWITCH_FADE);
    ramp_to(duty);
}

void moon_ui_fade_out(void) {
    if (!s_ready || CONFIG_UI_SCREEN_SWITCH_FADE == 0) {
        return;
    }
    const int from = s_duty;
    if (from == 0) {
        s_fade_pending = true;
        knob_ui_gasp_backlight(s_duty, true);
        return;
    }
    ESP_LOGI(TAG, "screen switch fade-out: backlight %d -> 0 (%d ms)", from, (int)CONFIG_UI_SCREEN_SWITCH_FADE);
    ramp_to(0);
    s_fade_pending = true;
    knob_ui_gasp_backlight(s_duty, true);
}

/* ---- screens + frames ------------------------------------------------- */

void moon_ui_init(void) {
    s_ready = knob_ui_alloc_frames();
    ESP_LOGI(TAG, "moon render seam %s (buffers %s)", s_ready ? "ready" : "DEGRADED",
             s_ready ? "up" : "failed (moon-less screens)");
}

lv_obj_t *moon_ui_create_image(lv_obj_t *parent) {
    if (!s_ready) {
        return NULL;
    }
    lv_obj_t *img = knob_ui_attach_moon_image(parent);
    if (img && s_n_roots < (int)(sizeof(s_moon_roots) / sizeof(s_moon_roots[0]))) {
        s_moon_roots[s_n_roots++] = parent;
    }
    return img;
}

bool moon_ui_screen_is_moon(enum ScreensEnum id) {
    lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
    if (!root) {
        return false;
    }
    for (int i = 0; i < s_n_roots; i++) {
        if (s_moon_roots[i] == root) {
            return true;
        }
    }
    return false;
}

void moon_ui_screen_switched(void) {
    /* ONE atomic whole-panel flush of the freshly loaded screen (the
     * family rule; on the knob LVGL's own partial drain carries it --
     * there is no direct-write path to race). Then the fade-in back,
     * while the new screen is already correct on the back buffer. */
    knob_ui_gasp(KNOB_GASP_SWITCH_ENTER);
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    ESP_LOGI(TAG, "switched: enter (invalidate)");
#endif
    knob_ui_invalidate_all();
    knob_ui_gasp(KNOB_GASP_SWITCH_LOCK);
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    ESP_LOGI(TAG, "switched: taking lock for atomic refr");
#endif
    knob_ui_lvgl_lock();
    lv_refr_now(NULL);
    knob_ui_lvgl_unlock();
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    ESP_LOGI(TAG, "switched: refr done, fade_pending=%d", (int)s_fade_pending);
#endif
    if (s_fade_pending) {
        s_fade_pending = false;
        knob_ui_gasp_backlight(s_duty, false);
        knob_ui_gasp(KNOB_GASP_SWITCH_RAMP);
        ramp_to(255); /* the clock ramps back after the atomic flush */
        knob_ui_gasp(KNOB_GASP_IDLE);
#if CONFIG_KNOB_DIAG_FRAME_TRACE
        ESP_LOGI(TAG, "switched: fade-in ramp complete");
#endif
    }
}

void moon_ui_refresh(void) { knob_ui_invalidate_all(); }

void moon_ui_bake_now(int64_t utc_epoch) { (void)knob_ui_show_moon(utc_epoch); }

void moon_ui_start(void) {
    s_started = true;
    ESP_LOGI(TAG, "moon started (static posture; the boot arm owns sweep-vs-static)");
}

void moon_ui_replay_animation(void) {
    /* The shared wake flow asks for the sweep; the knob's sweep is
     * boot-only by policy (the clock's wake posture = static current
     * phase), and boot_arm_task already decides it. Static re-arm: */
    int64_t now = 0;
    if (clock_get_utc_epoch(&now) == ESP_OK && now != 0) {
        (void)knob_ui_show_moon(now);
    }
}

void moon_ui_day_tick(void) {
    /* NO-OP by design: the knob producer task runs the 1 Hz day/event
     * probe on CPU1 (knob_ui moon_task). The clock's variant of this
     * call once froze the minute roll by running the ERFA day scan on
     * the LVGL task (fixed 2026-11-09 by the worker offload) -- the
     * knob keeps the scan off the render core by construction. */
}

void moon_ui_pause(bool pause) { (void)pause; /* a static frame parks itself: no spin on this board */ }

bool moon_ui_dusk_start(void) {
    /* No pole spin on the knob (backlit LCD, no burn-in duty). Returning
     * false takes the caller's documented fallback. */
    return false;
}

bool moon_ui_dusk_complete(void) { return false; }

bool moon_ui_tilt_settled(void) { return true; } /* no IMU: always at rest */

bool moon_ui_sweeping(void) { return knob_ui_sweeping(); }

void moon_ui_set_panel_step(int step) { (void)step; /* no direct panel writes to pre-rotate */ }

void moon_ui_rebuild_ui_mask(void) { /* No keep-mask: the spin never writes the panel behind LVGL here. */ }

void moon_ui_dump_hang_context(void) {
    ESP_LOGI(TAG, "hang ctx: ready=%d started=%d sweeping=%d shown=%d duty=%d fade_pending=%d roots=%d", (int)s_ready,
             (int)s_started, (int)knob_ui_sweeping(), (int)knob_ui_shown(), s_duty, (int)s_fade_pending, s_n_roots);
}

/* ---- home-screen dim parity (the knob power ladder's UI half) ---------
 * The clocks' dim look = hardware 25 % panel + the shared clock widget at
 * UI_CLOCK_ALPHA_PCT (20 %). knob_power owns the LEDC rung; this is the
 * object rung for the home screen (the same application point as the
 * shared light-sleep entry, actions.c, applied to the home container). */
void knob_render_set_home_dim(bool dimmed) {
    if (!objects.modem_sleep_clock) {
        return;
    }
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    ESP_LOGI(TAG, "home_dim(%d): taking lock", (int)dimmed);
#endif
    knob_ui_lvgl_lock();
    lv_obj_set_style_opa(objects.modem_sleep_clock,
                         dimmed ? (lv_opa_t)(LV_OPA_MAX * CONFIG_UI_CLOCK_ALPHA_PCT / 100) : LV_OPA_COVER, 0);
    /* The seconds arc is a SIBLING of the clock container (screens.c
     * create_screen_modem_sleep), so the container's opa never cascades
     * to it -- it ticked at full brightness straight through the dim
     * phase (user 2026-11-11: "still shows the seconds tick arc in light
     * sleep"). The clocks PAUSE the seconds timer at their light-sleep
     * entry (actions.c); the knob has no light-sleep screen, so the dim
     * rung hides the arc — hiding also erases the ticking itself. */
    if (objects.arc_clock_seconds_modem) {
        lv_obj_set_hidden(objects.arc_clock_seconds_modem, dimmed);
    }
    knob_ui_lvgl_unlock();
}

/* The knob has no demo tour (no harness, no camera story): the shared
 * actions.c calls this unguarded on several paths, and on the clock
 * boards the production demo.c answers with false -- the same honest
 * answer lives here, since the knob build excludes demo.c. */
bool action_demo_active(void) { return false; }
