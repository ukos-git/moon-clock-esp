/**
 * @file knob_ui.c
 * @brief The knob's moon display (K7b static + K7c sweep/day tick, task
 *        #24): one physical disk + a caption row.
 *
 * Frame path = the plan's D7: ONE lv_image carrying a PSRAM RGB565
 * square through the EXISTING partial flush. The descriptor declares
 * plain LV_COLOR_FORMAT_RGB565 and the draw unit byte-swaps it into the
 * SWAPPED canvas -- the same route the K4 screens took after the
 * purple-wash fix (the bench acceptance explicitly looks for a purple
 * wash; the F1 single-window direct draw is NOT built unless seam lines
 * show on glass). The bake itself lives in knob_moon (the verbatim
 * ported clock core); everything here is glue. No burn-in machinery: the
 * SH8601 is a backlit LCD, not an AMOLED.
 *
 * K7c threading (the clock's moon_ui model, stripped of every AMOLED
 * mechanism): PRODUCER (prio 5, 12 KB -- the classify scan needs it, the
 * clock's "12 KB, not 8" lesson) bakes into the BACK buffer and
 * publishes a counter; SWAP (prio 7, below the LVGL task's 10) takes the
 * main-injected LVGL mutex, swaps the descriptor pointer, runs ONE
 * atomic lv_refr_now and acks. The producer never touches the displayed
 * buffer: it waits for the ack (5 s bail = the clock's cap) before
 * reusing the back buffer, and a stale-unacked back buffer is always
 * safe to overwrite. The sweep's final frame (and the always-run
 * landing bake) is the crisp full-size bake with the event passed, so
 * the sweep lands byte-identically on the FRAMES=0 static build.
 * Never-black: the caption counts "sweeping n/N" from frame 1, and a
 * screen press aborts the sweep (the row loop honors it within one row)
 * -- the display still lands on the true current phase.
 *
 * The companion mirror views (KEYBOARD/STEPPER, K4) were retired
 * 2026-11-08 with the companion approach; the dial stays initialized and
 * idle until K7e (settings/power) gives it a job.
 */
#include "knob_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_moon.h"
#include "knob_power.h"
#include "lvgl.h"

static const char *TAG = "knob_ui";

/* The disk: the full 360 px square (the round window crops the corners
 * itself), disk r 150 at cy 168 — the 12 px below centre stays free of
 * lunar limb for the caption row at the bottom edge. */
#define MOON_SIZE KNOB_LCD_H_RES
#define MOON_R 150.0f
#define MOON_CX (KNOB_LCD_H_RES / 2)
#define MOON_CY 168

/* #ifndef-seeded (the repo invariant: a Kconfig 'n' leaves the macro
 * UNDEFINED; these seed the Kconfig defaults, never redefine them). */
#ifndef CONFIG_KNOB_MOON_SWEEP_FRAMES
#define CONFIG_KNOB_MOON_SWEEP_FRAMES 1344 /* task #17's no-jump count */
#endif
#ifndef CONFIG_KNOB_MOON_SWEEP_BAKE_PX
#define CONFIG_KNOB_MOON_SWEEP_BAKE_PX 128 /* bake cost ~px^2; 96 = the clock's sharpness floor */
#endif

/* P1: the moon image is no longer a single own-screen object — the shared
 * home screen (and every future moon screen) attaches its own lv_image
 * through the SAME descriptor, mirroring the clocks' moon_ui_create_image
 * / s_images[] posture. The legacy knob_ui_create attaches one to the
 * active screen, so the swap task only ever drives the list. */
#define KNOB_MAX_IMGS 8
static lv_obj_t *s_imgs[KNOB_MAX_IMGS];
static int s_n_imgs;
static lv_obj_t *s_caption; /* NULL in shared-UI mode (no caption row) */
static lv_image_dsc_t s_moon_dsc;
static uint16_t *s_buf[2]; /* PSRAM, each MOON_SIZE*MOON_SIZE RGB565 */
static bool s_nomem;       /* the double-buffer PSRAM alloc failed */
static bool s_alloced;     /* buffers + descriptor up (alloc_frames) */
static bool s_started;     /* producer + swap task live */

/* Producer/consumer handshake (the clock's shown/published posture).
 * Only the swap task writes s_front/s_applied; only the producer writes
 * s_pub/s_caption_text(+dirty)/s_abort-clearing; s_abort is set by the
 * touch callback (LVGL task) and polled by the producer. */
static volatile uint32_t s_pub;
static volatile uint32_t s_applied;
static int s_front;              /* buffer the panel shows */
static volatile bool s_shown;    /* first frame landed -> unhide */
static volatile bool s_abort;    /* touch during the sweep */
static volatile bool s_sweeping; /* press only aborts a sweep */
static volatile bool s_caption_dirty;
static char s_caption_text[48]; /* producer-written, swap applies */
static int64_t s_last_bake_ms;  /* producer-private */
static SemaphoreHandle_t s_lvgl_mux;

/* #ifndef-seeded KNOB_TZ (repo invariant): the caption clock's POSIX TZ
 * (knob_time/Kconfig). UTC0 default; the caption then reads like the
 * clocks' home screen (local wall time), which is what makes the bench
 * show one time on three boards. */
#ifndef CONFIG_KNOB_TZ
#define CONFIG_KNOB_TZ "UTC0"
#endif

/* Time source (family seam, P0): the knob has no RTC, so "now" is the
 * clock component's UTC epoch whenever the wall clock holds one (SNTP or
 * NVS-restored -- clock_get_utc_epoch answers ESP_ERR_INVALID_STATE on an
 * implausible system clock) -- reading the wall clock directly means the
 * hourly POLL re-sync's correction takes effect without re-arming.
 * Without a plausible time ui_now_epoch() falls back to the boot epoch
 * plus uptime: the bench frozen-KNOB_MOON_TEST_EPOCH path (pinned near
 * midnight makes the day tick fire its ONE re-bake, the K7c
 * acceptance), and production never reaches it for a BAKE -- main only
 * arms with a valid instant and the day tick never sees invalid time. */
static int64_t s_now_base_epoch;
static int64_t s_now_base_us;
static uint32_t s_baked_day;         /* UTC day key of the frame on screen */
static int s_baked_sig;              /* event signature it was baked with */
static volatile bool s_force_rebake; /* K7d: show_moon(epoch) after time-sync */

/* Producer's opening act, set by the entry point that creates it. */
enum {
    MODE_IDLE = 0,  /* (unused today: tasks always open with a frame) */
    MODE_SWEEP = 1, /* cold boot: synodic sweep, then land on now */
    MODE_STATIC = 2 /* wake / FRAMES=0: crisp now-frame, then day tick */
};
static int s_mode;

/* ---- small helpers ------------------------------------------------- */

/* One reentrant wrapper for the APP-WIDE mutex. The LVGL task holds it
 * across lv_timer_handler, and LVGL event handlers run inside that
 * context while calling code that locks again — the home tap's
 * loadScreen -> moon_ui_screen_switched -> knob_ui_invalidate_all was
 * the first owner-recursion and froze the device at CLICKED (bench
 * 2026-11-10; a FreeRTOS mutex is NOT recursively takable by its
 * owner). A same-task take is therefore a depth bump; other tasks keep
 * blocking semantics. main.c's local lvgl_lock/unlock pair delegates
 * here — nothing else takes s_lvgl_mux directly. */
static TaskHandle_t s_lvgl_holder;
static int s_lvgl_depth;

static void lvgl_lock(void) {
    if (!s_lvgl_mux) {
        return;
    }
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self == s_lvgl_holder && s_lvgl_depth > 0) {
        s_lvgl_depth++;
        return;
    }
    xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    s_lvgl_holder = self;
    s_lvgl_depth = 1;
}

static void lvgl_unlock(void) {
    if (!s_lvgl_mux) {
        return;
    }
    if (xTaskGetCurrentTaskHandle() != s_lvgl_holder) {
        ESP_LOGE(TAG, "lvgl_unlock from non-owner %s — ignored", pcTaskGetName(NULL));
        return;
    }
    if (--s_lvgl_depth > 0) {
        return;
    }
    s_lvgl_holder = NULL;
    xSemaphoreGive(s_lvgl_mux);
}

void knob_ui_set_lvgl_lock(SemaphoreHandle_t mux) { s_lvgl_mux = mux; }

static int sweep_frames_get(void) { return CONFIG_KNOB_MOON_SWEEP_FRAMES; }

static int sweep_bake_px_get(void) {
    int s = CONFIG_KNOB_MOON_SWEEP_BAKE_PX;
    if (s < 96) {
        s = 96; /* never below the clock's sharpness floor */
    }
    if (s > MOON_SIZE) {
        s = MOON_SIZE; /* above the panel adds cost, not pixels */
    }
    return s;
}

bool knob_ui_sweep_enabled(void) { return sweep_frames_get() >= 2; /* 0/1 frames = static only (bench builds) */ }

static int64_t ui_now_epoch(void) {
    int64_t wall = 0;
    if (clock_get_utc_epoch(&wall) == ESP_OK && wall != 0) {
        return wall;
    }
    return s_now_base_epoch + (esp_timer_get_time() - s_now_base_us) / 1000000;
}

static uint32_t day_key(int64_t epoch) {
    time_t t = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    return (uint32_t)(tm_utc.tm_year + 1900) * 512u + (uint32_t)(tm_utc.tm_mon + 1) * 32u + (uint32_t)tm_utc.tm_mday;
}

/**
 * @brief The glass clock: local wall time "Nov 8 17:29" through the
 * KNOB_TZ POSIX zone -- deliberately short. The caption sits 28 px off
 * the bottom of the ROUND 360 panel, where the round window leaves a
 * band only ~205 px wide: the old "2026-11-08 16:27 UTC" (~170 px)
 * clipped BOTH ends on the bench photo, and the clocks' home row is
 * plain local time, so a UTC-only caption made one bench moment show
 * two different times on glass.
 */
static void time_caption(char *dst, size_t dst_sz, int64_t epoch) {
    static bool tz_applied;
    if (!tz_applied) {
        setenv("TZ", CONFIG_KNOB_TZ, 1);
        tzset();
        tz_applied = true;
    }
    static const char *const MON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    time_t t = (time_t)epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(dst, dst_sz, "%s %d %02d:%02d", MON[lt.tm_mon], lt.tm_mday, lt.tm_hour, lt.tm_min);
}

/* Minute key of the clock caption currently on the double buffer -- the
 * day tick uses it to roll the glass clock WITHOUT a re-bake (the frame
 * stays; only the label text moves). Producer-task-owned like the
 * caption staging itself. */
static int64_t s_caption_min;

/**
 * @brief The previous frame must be on screen before the back buffer is
 *        reused (the clock's shown/published backpressure; the 5 s bail
 *        is its safety cap). The swap task polls at 15 ms and the bake
 *        dominates, so this wait is usually ~0.
 */
static void wait_ack(void) {
    int waited = 0;
    while (s_applied != s_pub && waited < 5000) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if (s_applied != s_pub) {
        ESP_LOGW(TAG, "ack timeout (5 s): swap task stalled?");
    }
}

/**
 * @brief Bake into the back buffer, stage the caption, publish.
 * bakes at bake_px (0 = crisp full size). caption NULL = caption stays.
 * NEVER publish an aborted frame: the bake returns false mid-rows.
 */
static bool bake_publish(int64_t epoch, int bake_px, bool pass_event, const volatile bool *abort_flag,
                         const char *caption) {
    if (s_nomem) {
        return false;
    }
    const int back = 1 - s_front;
    if (!knob_moon_bake_display(s_buf[back], MOON_SIZE, MOON_R, MOON_CX, MOON_CY, epoch, bake_px, pass_event,
                                abort_flag, &s_last_bake_ms)) {
        return false;
    }
    if (caption) {
        snprintf(s_caption_text, sizeof(s_caption_text), "%s", caption);
        s_caption_dirty = true;
    }
    /* Publish/acquire ordering across cores: the buffer/caption writes
     * must be visible before the counter move (Xtensa store buffer; the
     * clock's equivalent rides its buffer mutex, our producer publishes
     * lock-free so it carries the explicit fence). */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    s_pub++;
    return true;
}

static void caption_unavailable(void) {
    snprintf(s_caption_text, sizeof(s_caption_text), "moon unavailable");
    s_caption_dirty = true;
}

/* ---- the producer task ---------------------------------------------- */

/** The crisp current-phase frame, always landed (sweep end OR abort OR
 *  bake failure): full size, event passed, caption = the UTC instant. */
static void land_current_phase(void) {
    const int64_t now = ui_now_epoch();
    wait_ack();
    char cap[40];
    time_caption(cap, sizeof(cap), now);
    if (!bake_publish(now, 0, true, NULL, cap)) {
        ESP_LOGE(TAG, "landing bake failed (bake_px=crisp)");
        caption_unavailable();
        return;
    }
    s_caption_min = now / 60;
    s_baked_day = day_key(now);
    s_baked_sig = knob_moon_classify_signature(now);
}

/* The first frame of the static (wake / sweep-disabled) boot, on the
 * PRODUCER (unregistered, pinned to CPU1): the display_begin classify
 * scan is the long CPU-bound stretch here -- the core yields on a
 * 20 ms wall-clock budget inside its CONFIG_BOARD_KNOB branches
 * (moon_event.c, bench 2026-11-11) so the unregistered task never
 * starves IDLE1 (whose check is ON, with panic, in
 * knob/sdkconfig.defaults). Caption + disk
 * appear when the frame applies; "moon unavailable" stays if it fails. */
static void static_once_body(void) {
    const int64_t now = ui_now_epoch();
    knob_moon_display_begin(now);
    char cap[40];
    time_caption(cap, sizeof(cap), now);
    if (!bake_publish(now, 0, true, NULL, cap)) {
        ESP_LOGE(TAG, "static bake failed");
        caption_unavailable();
    } else {
        s_caption_min = now / 60;
    }
    s_baked_day = day_key(now);
    s_baked_sig = knob_moon_classify_signature(now);
}

/**
 * Cold-boot synodic sweep (the clock's moon_sweep_task, minus the spin
 * pause / idle hold / AMOLED machinery): frames evenly spaced from the
 * last NEW MOON (dark) to NOW, intermediate frames baked at
 * KNOB_MOON_SWEEP_BAKE_PX (the disk geometry scales with the bake -- the
 * knob's disk does NOT fill the square, unlike the clock's MOON_PX one),
 * the FINAL frame crisp with the event. LUT authority: classify now
 * ONCE before the loop (the special moon's color foreshadows as the
 * disk brightens; the occlusion only rides the final frame).
 */
static void sweep_body(void) {
    const int frames = sweep_frames_get();
    const int bake_px = sweep_bake_px_get();
    const int64_t now = ui_now_epoch();
    ESP_LOGI(TAG, "synodic boot sweep: %d frames @%d px (new moon -> now)", frames, bake_px);
    const int sig = knob_moon_display_begin(now);
    const int64_t nm = knob_moon_new_moon_epoch(now, &s_abort);
    const int64_t SYNODIC_S = (int64_t)(29.53 * 86400);
    int64_t span = now - nm;
    if (span <= 0) {
        span = SYNODIC_S; /* ephemeris degenerate: fall back to a full month */
    }
    if (span > SYNODIC_S) {
        span = SYNODIC_S;
    }
    ESP_LOGI(TAG, "sweep span: %lld d (new moon %lld s before now, sig 0x%02x)", (long long)(span / 86400),
             (long long)span, sig);
    const int64_t t0 = esp_timer_get_time();
    int f = 0;
    for (; f < frames; f++) {
        if (s_abort) {
            break;
        }
        wait_ack();
        if (s_abort) {
            break;
        }
        const bool final = (f == frames - 1);
        /* Frame f is (frames-1-f) steps before the current phase:
         * f=0 is the last NEW MOON, f=frames-1 is NOW. */
        const double t = (double)(frames - 1 - f) / (double)(frames - 1);
        const int64_t epoch = now - (int64_t)(t * span);
        char cap[40];
        snprintf(cap, sizeof(cap), "sweeping %d/%d", f + 1, frames);
        if (!bake_publish(epoch, final ? 0 : bake_px, final, &s_abort, cap)) {
            if (s_abort) {
                break;
            }
            ESP_LOGE(TAG, "sweep bake failed at frame %d -- landing on current phase", f);
            break;
        }
        if ((f & 63) == 0 || final) {
            ESP_LOGI(TAG, "sweeping %d/%d (bake %lld ms%s)", f + 1, frames, (long long)s_last_bake_ms,
                     final ? ", final crisp" : "");
        }
        /* wait_ack() only blocks while the swap task is behind -- when
         * it keeps up (the normal case) the bakes run back-to-back and
         * IDLE never runs for the whole 314 s sweep (task-WDT reset,
         * bench 2026-11-11). One 1 ms delay per frame makes IDLE's
         * quantum a structural guarantee, not a timing accident. */
        vTaskDelay(1);
    }
    land_current_phase();
    s_sweeping = false;
    s_abort = false;
    ESP_LOGI(TAG, "sweep done: %d/%d frames in %lld ms", f, frames, (long long)((esp_timer_get_time() - t0) / 1000));
}

#ifdef CONFIG_KNOB_DEMO_MODE
/**
 * Bench demo tour (KNOB_DEMO_MODE) -- the knob half of the scripted
 * on-device comparison (docs/verification/demo-mode, mirrors the
 * clocks' UI_DEMO_MODE tour). Runs INSTEAD of the day-tick loop after
 * the boot presentation: eight canonical phases of the current synodic
 * month (epochs pinned at k/8 of the month, so the two clocks' and the
 * knob's "same phase" frames are comparable frame for frame), plus a
 * ninth step on the LIVE current phase (re-baked every cycle, events
 * on -- the same now-frame the clocks show at home). The power ladder
 * stays held (the caller never releases it): no 15 s dim, no deep
 * sleep, the recording never goes dark. Each step logs
 * `phase=<name> cycle=<n>` under tag DEMO for the harness.
 */
static void demo_tour(void) {
    static const char *const names[8] = {"new-moon",  "waxing-crescent", "first-quarter", "waxing-gibbous",
                                         "full-moon", "waning-gibbous",  "last-quarter",  "waning-crescent"};
    const int64_t SYNODIC_S = (int64_t)(29.53 * 86400);
    const uint32_t step_ms = (uint32_t)CONFIG_KNOB_DEMO_PHASE_SEC * 1000u;
    unsigned cycle = 0;
    ESP_LOGI("DEMO", "knob tour armed: 8 canonical phases + live now, %d s each", CONFIG_KNOB_DEMO_PHASE_SEC);
    for (;;) {
        const int64_t now = ui_now_epoch();
        const int64_t nm = knob_moon_new_moon_epoch(now, NULL);
        for (int k = 0; k < 8; k++) {
            const int64_t epoch = nm + (int64_t)((double)k / 8.0 * (double)SYNODIC_S);
            wait_ack();
            knob_moon_display_begin(epoch);
            char cap[48];
            snprintf(cap, sizeof(cap), "demo %d/8 %s", k + 1, names[k]);
            if (!bake_publish(epoch, 0, true, NULL, cap)) {
                ESP_LOGE("DEMO", "phase %s bake failed", names[k]);
                caption_unavailable();
            }
            ESP_LOGI("DEMO", "phase=%s cycle=%u screen=moon epoch=%lld", names[k], cycle, (long long)epoch);
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
        /* The live now-frame (events on) -- re-baked every cycle. Read
         * "now" HERE, not at cycle top: the canonical walk already ran
         * 8 * KNOB_DEMO_PHASE_SEC of dwell, so the cycle-top instant
         * was minutes stale -- the step called "live now" must be live. */
        const int64_t live = ui_now_epoch();
        wait_ack();
        knob_moon_display_begin(live);
        char cap[48];
        time_caption(cap, sizeof(cap), live);
        if (!bake_publish(live, 0, true, NULL, cap)) {
            ESP_LOGE("DEMO", "now bake failed");
            caption_unavailable();
        }
        s_caption_min = live / 60;
        ESP_LOGI("DEMO", "phase=now cycle=%u screen=moon", cycle);
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        cycle++;
    }
}
#endif /* CONFIG_KNOB_DEMO_MODE */

/** The clock's day tick, minus the IMU/alarm coupling: 1 Hz UTC check;
 *  one re-bake when the UTC day OR the event signature changes (an
 *  eclipse starts at the hour it happens, not at UTC midnight). */
static void moon_task(void *arg) {
    (void)arg;
    /* K7e: the opening act is a PRESENTATION (sweep or first frame) --
     * hold the power ladder through it (~5 min sweep, ~20 s classify +
     * bake on the static path); afterwards the ladder runs free and
     * the day tick bakes may dim/sleep the idle display. */
    knob_power_set_hold(true);
    if (s_mode == MODE_SWEEP) {
        sweep_body();
    } else if (s_mode == MODE_STATIC) {
        static_once_body();
    }
#ifdef CONFIG_KNOB_DEMO_MODE
    demo_tour(); /* never returns; the power ladder stays held */
#endif
    knob_power_set_hold(false);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_nomem) {
            continue;
        }
        const int64_t now = ui_now_epoch();
        const uint32_t day = day_key(now);
        const int sig = knob_moon_classify_signature(now);
        if (s_force_rebake || day != s_baked_day || sig != s_baked_sig) {
            s_force_rebake = false;
            ESP_LOGI(TAG, "day tick: re-bake (day %lu, sig 0x%02x -> 0x%02x)", (unsigned long)day,
                     (unsigned int)s_baked_sig, (unsigned int)sig);
            wait_ack();
            knob_moon_display_begin(now); /* re-arm the tint LUT + occlusion */
            char cap[40];
            time_caption(cap, sizeof(cap), now);
            if (!bake_publish(now, 0, true, NULL, cap)) {
                ESP_LOGE(TAG, "day-tick bake failed");
                caption_unavailable();
                continue;
            }
            s_caption_min = now / 60;
            s_baked_day = day_key(now);
            s_baked_sig = sig;
        } else if (s_shown && now / 60 != s_caption_min && strcmp(s_caption_text, "moon unavailable") != 0) {
            /* The glass clock rolls with the minute: only the label
             * text moves (no bake, no publish). Without this the
             * caption is frozen at the last bake instant (the bench
             * caught it: the clock read 17:29 local while the caption
             * still spelled the 16:27-UTC bake instant -- a frozen
             * minute compounded by a UTC spelling). The s_shown gate
             * never stamps a clock over "no time"/"moon unavailable". */
            char cap[40];
            time_caption(cap, sizeof(cap), now);
            snprintf(s_caption_text, sizeof(s_caption_text), "%s", cap);
            s_caption_dirty = true;
            s_caption_min = now / 60;
        }
    }
}

/* ---- the swap task --------------------------------------------------- */

/**
 * Applies published frames inside the LVGL lock: descriptor swap + one
 * atomic lv_refr_now (the clock's whole-panel posture; the producer may
 * only reuse this buffer after the ack), caption, ack. Polling at 15 ms
 * paces nothing (the bake dominates) and keeps the panel owner out of
 * the producer's timing path.
 */
static void swap_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_applied != s_pub && !s_nomem) {
            /* Pair with the producer's publish fence: read the buffer/
             * caption only after observing the new counter. */
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            lvgl_lock();
            const int back = 1 - s_front;
            s_moon_dsc.data = (const uint8_t *)s_buf[back];
            if (!s_shown) {
                s_shown = true;
            }
            for (int i = 0; i < s_n_imgs; i++) {
                lv_obj_set_hidden(s_imgs[i], false);
                lv_obj_invalidate(s_imgs[i]);
            }
            if (s_caption_dirty) {
                s_caption_dirty = false;
                if (s_caption) {
                    lv_label_set_text(s_caption, s_caption_text);
                    /* #31 probe: caption geometry after relayout, 1 s
                     * throttle (the sweep re-labels every frame). */
                    static char last_cap[48];
                    static int64_t last_cap_us;
                    int64_t cap_us = esp_timer_get_time();
                    if (strcmp(last_cap, s_caption_text) != 0 && cap_us - last_cap_us > 1000000) {
                        last_cap_us = cap_us;
                        strlcpy(last_cap, s_caption_text, sizeof(last_cap));
                        ESP_LOGI(TAG, "caption '%s' x=%d y=%d w=%d h=%d scr=%dx%d", s_caption_text,
                                 lv_obj_get_x(s_caption), lv_obj_get_y(s_caption), (int)lv_obj_get_width(s_caption),
                                 (int)lv_obj_get_height(s_caption), (int)lv_obj_get_width(lv_screen_active()),
                                 (int)lv_obj_get_height(lv_screen_active()));
                    }
                }
            }
            lv_refr_now(NULL);
            s_front = back;
            s_applied = s_pub;
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void start_tasks(int mode) {
    s_mode = mode;
    s_sweeping = (mode == MODE_SWEEP);
    s_abort = false;
    s_started = true;
    /* Producer = the clock's sweep-task posture (moon_ui.c:2776):
     * xTaskCreatePinnedToCoreWithCaps -- unregistered from TWDT (its
     * work never feeds the watchdog, so a spin of its own is invisible
     * to it), yet everything long-running on it yields: the classify
     * scan on a 20 ms budget (moon_event.c knob branches), the bake by
     * rows, the sweep per frame -- so sdkconfig.defaults keeps CPU1's
     * idle check ON with panic on expiry (bench 2026-11-11: the silent
     * TG1WDT boots were an unyielding scan with the check off),
     * pinned to CPU1 so the bake never starves the render/Wi-Fi core,
     * stack in PSRAM. 12 KB, not 8: the classify scan + the upcase sm[]
     * overflowed 8 KB on the clock (stack-protection fault, 2026-11-04).
     * The swap task stays a plain registered xTaskCreate (it polls and
     * delays -- a watchdog on it is a feature). */
    xTaskCreatePinnedToCoreWithCaps(moon_task, "knob_moon", 12288, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
    xTaskCreate(swap_task, "knob_swap", 8192, NULL, 7, NULL);
}

/* ---- touch: the only input with a job until K7e ---------------------- */

static void scr_press_cb(lv_event_t *e) {
    (void)e;
    knob_power_notify_activity(); /* K7e: a press is activity */
    if (s_sweeping) {
        s_abort = true; /* the row loop honors it within one row */
    }
}

/* ---- public API ------------------------------------------------------- */

bool knob_ui_alloc_frames(void) {
    if (s_alloced) {
        return !s_nomem;
    }
    const size_t buf_sz = (size_t)MOON_SIZE * MOON_SIZE * sizeof(uint16_t);
    s_buf[0] = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    s_buf[1] = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_buf[0] || !s_buf[1]) {
        free(s_buf[0]);
        free(s_buf[1]);
        s_buf[0] = s_buf[1] = NULL;
        s_nomem = true;
        ESP_LOGE(TAG, "PSRAM double buffer (%d KB x2) failed", (int)(buf_sz / 1024));
        return false;
    }
    s_moon_dsc.header.cf = LV_COLOR_FORMAT_RGB565; /* LVGL swaps into the canvas */
    s_moon_dsc.header.w = MOON_SIZE;
    s_moon_dsc.header.h = MOON_SIZE;
    s_moon_dsc.header.stride = MOON_SIZE * sizeof(uint16_t);
    s_moon_dsc.data = (const uint8_t *)s_buf[0];
    s_moon_dsc.data_size = (uint32_t)buf_sz;
    s_alloced = true;
    return true;
}

lv_obj_t *knob_ui_attach_moon_image(lv_obj_t *parent) {
    if (!s_alloced || s_n_imgs >= KNOB_MAX_IMGS) {
        return NULL;
    }
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_clickable(img, false);
    lv_image_set_src(img, &s_moon_dsc);
    /* Hidden until the first frame lands (keeps the "is a frame up?"
     * question answerable from the object tree); an attach AFTER frames
     * are already flowing (a later moon screen) shows it immediately. */
    lv_obj_set_hidden(img, !s_shown);
    s_imgs[s_n_imgs++] = img;
    return img;
}

void knob_ui_invalidate_all(void) {
    lvgl_lock();
    for (int i = 0; i < s_n_imgs; i++) {
        lv_obj_invalidate(s_imgs[i]);
    }
    lvgl_unlock();
}

bool knob_ui_sweeping(void) { return s_sweeping; }

bool knob_ui_shown(void) { return s_shown; }

void knob_ui_lvgl_lock(void) { lvgl_lock(); }

void knob_ui_lvgl_unlock(void) { lvgl_unlock(); }

/* Legacy own-screen view (K7, the KNOB_UI_ENABLE=n fallback): black
 * screen + one attached moon image + the honest caption row. The shared-
 * UI path skips this and attaches through the seams instead. */
void knob_ui_create(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (!knob_ui_alloc_frames()) {
        return;
    }
    if (!knob_ui_attach_moon_image(scr)) {
        return;
    }

    /* The round window clips hard: at 6 px off the bottom edge only a
     * ~139 px band survives, and the old auto-width label clipped both
     * ends of "2026-11-08 16:27 UTC" (bench photo, 2026-11-08). 28 px
     * up the band is ~205 px; the fixed 240 px box + DOT long mode
     * makes clipping impossible to miss -- it becomes an ellipsis. */
    s_caption = lv_label_create(scr);
    lv_label_set_text(s_caption, "moon unavailable");
    /* 16 px, not the 28 px default: at 28 the phase names ("demo 3/8
     * waxing-gibbous") and even "sweeping 1234/1344" would dot-truncate
     * inside the 240 px box the round window allows. */
    lv_obj_set_style_text_font(s_caption, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_caption, 240);
    lv_label_set_long_mode(s_caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_caption, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_clickable(s_caption, false);

    /* Screen press = sweep abort (the screen is clickable by default). */
    lv_obj_add_event_cb(scr, scr_press_cb, LV_EVENT_PRESSED, NULL);

    ESP_LOGI(TAG, "knob UI up (moon view, %d px disk r %.0f, sweep %s, PSRAM free %d KB)", MOON_SIZE, (double)MOON_R,
             knob_ui_sweep_enabled() ? "armed" : "off", (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

/* Arm-path hide (LVGL lock held by the caller): every attached image
 * goes hidden until the next applied frame (the swap task re-shows all). */
static void hide_all_images(void) {
    for (int i = 0; i < s_n_imgs; i++) {
        lv_obj_set_hidden(s_imgs[i], true);
    }
}

void knob_ui_show_no_time(void) {
    if (s_started || s_nomem) {
        return; /* a frame path owns the caption, or it names a real failure */
    }
    lvgl_lock();
    if (s_caption) {
        lv_label_set_text(s_caption, "no time");
    }
    hide_all_images();
    lvgl_unlock();
}

bool knob_ui_show_moon(int64_t utc_epoch) {
    s_now_base_epoch = utc_epoch;
    s_now_base_us = esp_timer_get_time();
    if (!knob_ui_alloc_frames()) {
        return false; /* the double buffer never came up (alloc logged it) */
    }
    if (s_started) {
        /* K7d hook: called again when SNTP lands a real time -- the day
         * tick re-bakes the current phase within a second (sweeps own
         * their epoch; the landing bake re-arms). */
        s_force_rebake = true;
        return true;
    }
    /* The boot core only ARMS and starts: the ~20 s classify + the crisp
     * bake run on the producer (CPU1-pinned, TWDT-unregistered) so
     * app_main returns at once -- never-black is the caption's job, and
     * it names the instant being baked while the disk is still hidden
     * (it flips to "moon unavailable" if the bake fails). */
    time_caption(s_caption_text, sizeof(s_caption_text), utc_epoch);
    s_caption_min = utc_epoch / 60;
    /* The boot core mutating LVGL while lvgl_task renders is the bug
     * #31's photo caught (caption clipped/shifted): every object touch
     * here rides the lock, like the swap task and show_no_time do. */
    lvgl_lock();
    if (s_caption) {
        lv_label_set_text(s_caption, s_caption_text);
    }
    hide_all_images();
    lvgl_unlock();
    s_front = 0;
    s_pub = 0;
    s_applied = 0;
    s_shown = false;
    start_tasks(MODE_STATIC);
    return true;
}

void knob_ui_start_sweep(int64_t now_epoch) {
    if (!knob_ui_sweep_enabled()) {
        (void)knob_ui_show_moon(now_epoch);
        return;
    }
    if (s_started) {
        s_force_rebake = true;
        return;
    }
    if (!knob_ui_alloc_frames()) {
        return;
    }
    s_now_base_epoch = now_epoch;
    s_now_base_us = esp_timer_get_time();
    snprintf(s_caption_text, sizeof(s_caption_text), "sweeping 0/%d", sweep_frames_get());
    lvgl_lock();
    if (s_caption) {
        lv_label_set_text(s_caption, s_caption_text);
    }
    hide_all_images();
    lvgl_unlock();
    s_caption_dirty = false;
    s_front = 0;
    s_pub = 0;
    s_applied = 0;
    s_shown = false;
    start_tasks(MODE_SWEEP);
}
