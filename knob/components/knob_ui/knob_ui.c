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
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_moon.h"
#include "knob_power.h"
#include "knob_time.h"
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

static lv_obj_t *s_moon_img;
static lv_obj_t *s_caption;
static lv_image_dsc_t s_moon_dsc;
static uint16_t *s_buf[2]; /* PSRAM, each MOON_SIZE*MOON_SIZE RGB565 */
static bool s_nomem;       /* the double-buffer PSRAM alloc failed */
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

/* Time source (K7d): the knob has no RTC, so "now" is knob_time (SNTP
 * truth) whenever it holds it -- reading the system clock directly means
 * the hourly POLL re-sync's correction takes effect without re-arming.
 * Without a trusted time ui_now_epoch() falls back to the boot epoch
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

static void lvgl_lock(void) {
    if (s_lvgl_mux) {
        xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    }
}

static void lvgl_unlock(void) {
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
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
    const int64_t sntp = knob_time_now_epoch(); /* 0 until an SNTP fix */
    if (sntp != 0) {
        return sntp;
    }
    return s_now_base_epoch + (esp_timer_get_time() - s_now_base_us) / 1000000;
}

static uint32_t day_key(int64_t epoch) {
    time_t t = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    return (uint32_t)(tm_utc.tm_year + 1900) * 512u + (uint32_t)(tm_utc.tm_mon + 1) * 32u + (uint32_t)tm_utc.tm_mday;
}

static void utc_caption(char *dst, size_t dst_sz, int64_t epoch) {
    time_t t = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    snprintf(dst, dst_sz, "%04d-%02d-%02d %02d:%02d UTC", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min);
}

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
    utc_caption(cap, sizeof(cap), now);
    if (!bake_publish(now, 0, true, NULL, cap)) {
        ESP_LOGE(TAG, "landing bake failed (bake_px=crisp)");
        caption_unavailable();
        return;
    }
    s_baked_day = day_key(now);
    s_baked_sig = knob_moon_classify_signature(now);
}

/* The first frame of the static (wake / sweep-disabled) boot, on the
 * PRODUCER (unregistered, pinned to CPU1): the display_begin classify
 * scan alone is ~20 s of core moon_event work without yields -- the
 * clock keeps it off the boot core for the same reason. Caption + disk
 * appear when the frame applies; "moon unavailable" stays if it fails. */
static void static_once_body(void) {
    const int64_t now = ui_now_epoch();
    knob_moon_display_begin(now);
    char cap[40];
    utc_caption(cap, sizeof(cap), now);
    if (!bake_publish(now, 0, true, NULL, cap)) {
        ESP_LOGE(TAG, "static bake failed");
        caption_unavailable();
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
    }
    land_current_phase();
    s_sweeping = false;
    s_abort = false;
    ESP_LOGI(TAG, "sweep done: %d/%d frames in %lld ms", f, frames, (long long)((esp_timer_get_time() - t0) / 1000));
}

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
            utc_caption(cap, sizeof(cap), now);
            if (!bake_publish(now, 0, true, NULL, cap)) {
                ESP_LOGE(TAG, "day-tick bake failed");
                caption_unavailable();
                continue;
            }
            s_baked_day = day_key(now);
            s_baked_sig = sig;
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
                lv_obj_set_hidden(s_moon_img, false);
            }
            lv_obj_invalidate(s_moon_img);
            if (s_caption_dirty) {
                s_caption_dirty = false;
                lv_label_set_text(s_caption, s_caption_text);
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
     * xTaskCreatePinnedToCoreWithCaps -- unregistered from TWDT (the
     * core classify/new-moon scans run ~30 s without yields; CPU1's
     * idle check is off in sdkconfig.defaults for the same reason),
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

void knob_ui_create(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_moon_img = lv_image_create(scr);
    lv_obj_set_pos(s_moon_img, 0, 0);
    lv_obj_set_clickable(s_moon_img, false);
    /* Hidden until the first frame lands (the sweep arms it hidden; the
     * static path unhides after its boot-core bake): keeps the "is a
     * frame up?" question answerable from the object tree, and the
     * never-black rule is carried by the caption instead. */
    lv_obj_set_hidden(s_moon_img, true);

    s_caption = lv_label_create(scr);
    lv_label_set_text(s_caption, "moon unavailable");
    lv_obj_align(s_caption, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_clickable(s_caption, false);

    /* Screen press = sweep abort (the screen is clickable by default). */
    lv_obj_add_event_cb(scr, scr_press_cb, LV_EVENT_PRESSED, NULL);

    const size_t buf_sz = (size_t)MOON_SIZE * MOON_SIZE * sizeof(uint16_t);
    s_buf[0] = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    s_buf[1] = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_buf[0] || !s_buf[1]) {
        free(s_buf[0]);
        free(s_buf[1]);
        s_buf[0] = s_buf[1] = NULL;
        s_nomem = true;
        ESP_LOGE(TAG, "PSRAM double buffer (%d KB x2) failed", (int)(buf_sz / 1024));
        return;
    }
    s_moon_dsc.header.cf = LV_COLOR_FORMAT_RGB565; /* LVGL swaps into the canvas */
    s_moon_dsc.header.w = MOON_SIZE;
    s_moon_dsc.header.h = MOON_SIZE;
    s_moon_dsc.header.stride = MOON_SIZE * sizeof(uint16_t);
    s_moon_dsc.data = (const uint8_t *)s_buf[0];
    s_moon_dsc.data_size = (uint32_t)buf_sz;
    lv_image_set_src(s_moon_img, &s_moon_dsc);

    ESP_LOGI(TAG, "knob UI up (moon view, %d px disk r %.0f, sweep %s, PSRAM free %d KB)", MOON_SIZE, (double)MOON_R,
             knob_ui_sweep_enabled() ? "armed" : "off", (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void knob_ui_show_no_time(void) {
    if (s_started || s_nomem) {
        return; /* a frame path owns the caption, or it names a real failure */
    }
    lvgl_lock();
    lv_label_set_text(s_caption, "no time");
    lv_obj_set_hidden(s_moon_img, true);
    lvgl_unlock();
}

bool knob_ui_show_moon(int64_t utc_epoch) {
    s_now_base_epoch = utc_epoch;
    s_now_base_us = esp_timer_get_time();
    if (s_nomem) {
        return false; /* the double buffer never came up (create logged it) */
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
    utc_caption(s_caption_text, sizeof(s_caption_text), utc_epoch);
    lv_label_set_text(s_caption, s_caption_text);
    s_front = 0;
    s_pub = 0;
    s_applied = 0;
    s_shown = false;
    lv_obj_set_hidden(s_moon_img, true);
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
    if (s_nomem) {
        return;
    }
    s_now_base_epoch = now_epoch;
    s_now_base_us = esp_timer_get_time();
    snprintf(s_caption_text, sizeof(s_caption_text), "sweeping 0/%d", sweep_frames_get());
    lv_label_set_text(s_caption, s_caption_text);
    s_caption_dirty = false;
    s_front = 0;
    s_pub = 0;
    s_applied = 0;
    s_shown = false;
    lv_obj_set_hidden(s_moon_img, true);
    start_tasks(MODE_SWEEP);
}
