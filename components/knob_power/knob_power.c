/**
 * @file knob_power.c
 * @brief K7e idle ladder: active -> light sleep (KNOB_DIM_AFTER_S) -> deep
 *        sleep (KNOB_SLEEP_TIMEOUT_S), EXT0 wake on dial-A GPIO 8.
 *
 * The two rungs load the FAMILY's sleep screens on the shared-UI path
 * (CONFIG_KNOB_UI_ENABLE, the default) -- the clocks' posture
 * (components/ui/actions.c): the dim rung loads SCREEN_ID_LIGHT_SLEEP
 * (clock widget at UI_CLOCK_ALPHA_PCT, LEDC ramp to
 * UI_LIGHT_SLEEP_DIM_PCT) and the deep rung loads SCREEN_ID_DEEP_SLEEP
 * (clock hidden) as the last-lit frame before the backlight off +
 * esp_deep_sleep_start. Each screen swap is ONE atomic flush (the
 * screen_switched seam) at the CURRENT backlight, and the hardware LEDC
 * ramp writes no pixels. The legacy own-screen view (KNOB_UI_ENABLE=n)
 * has no shared screens: its rungs keep the in-place backlight dim + the
 * dim observer callback.
 *
 * The supervisor is a plain registered task on a 500 ms cadence -- it
 * only delays and logs, so a watchdog on it is a feature. Activity
 * comes from two cheap polls (touch is pushed by main's read callback,
 * the dial count is pulled from knob_board) plus the presentation hold
 * owned by knob_ui. Deep sleep is armed with EXT0 GPIO 8 (in the S3 RTCIO
 * range 0..21 -- the runtime accept from esp_sleep_enable_ext0_wakeup
 * is the bench verdict the plan asked for, logged at arm time); the
 * first-detent miss when the B channel leads is documented in the
 * header and on the device page. KNOB_SLEEP_ALLOW=n keeps the device
 * awake and only LOGS the would-be sleep (the plan's sanctioned
 * log-only bench mode).
 */
#include <stdint.h>
#include <time.h>

#include "clock.h"
#include "clock_alarm_scheduler.h"
#include "clock_sntp.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_power.h"
#include "lvgl.h"
#include "moon_ui.h"
#include "screens.h"
#include "settings_store.h"
#include "ui.h"

static const char *TAG = "knob_power";

/* Wake pad: encoder A (GPIO 8), active-LOW detent pulse, 10K external
 * pull-up already on the board. */
#define WAKE_GPIO GPIO_NUM_8

/* #ifndef-seeded (repo invariant: seed the Kconfig defaults, never
 * redefine the macros). */
#ifndef CONFIG_KNOB_DIM_AFTER_S
#define CONFIG_KNOB_DIM_AFTER_S 15
#endif
#ifndef CONFIG_KNOB_SLEEP_TIMEOUT_S
#define CONFIG_KNOB_SLEEP_TIMEOUT_S 60
#endif
#ifndef CONFIG_KNOB_DIM_DUTY
#define CONFIG_KNOB_DIM_DUTY 64 /* P1 dim parity: 25 %, the clocks' dusk */
#endif
#ifndef CONFIG_UI_LIGHT_SLEEP_DIM_PCT
#define CONFIG_UI_LIGHT_SLEEP_DIM_PCT 25 /* the family light-sleep panel level */
#endif
#ifndef CONFIG_UI_CLOCK_ALPHA_PCT
#define CONFIG_UI_CLOCK_ALPHA_PCT 20 /* the family light-sleep clock opacity */
#endif
#ifndef CONFIG_KNOB_ALARM_MAX_SLEEP_GAP_S
#define CONFIG_KNOB_ALARM_MAX_SLEEP_GAP_S 82800
#endif

/* Below this an epoch is not a wall clock (factory default 1970, or a
 * corrupted snapshot): the TIMER arm must not chase it. 2020-09-13. */
#define EPOCH_PLAUSIBLE_MIN 1600000000LL

static volatile int64_t s_last_activity_us; /* written by any task, read by the supervisor */
static volatile bool s_hold;                /* presentation hold (knob_ui) */
static bool s_dimmed;                       /* supervisor-private */
static volatile bool s_sleeping;            /* stable-dark log-only state (KNOB_SLEEP_ALLOW=n) */
static knob_power_dim_cb_t s_dim_cb;        /* in-place dim observer (legacy own-screen path) */
static SemaphoreHandle_t s_lvgl_mux;        /* the UI's LVGL mutex (shared-UI sleep rungs) */

void knob_power_set_dim_cb(knob_power_dim_cb_t cb) { s_dim_cb = cb; }

/* The UI's LVGL mutex (main passes the same handle the knob_ui seam and
 * the boot arm take): the shared-UI rungs take it around the screen load
 * so the screen_switched atomic flush + the object writes are one critical
 * section. The mutex is recursive (IDF), so loadScreen's own re-take in
 * moon_ui_screen_switched is safe from the supervisor task (the boot arm
 * runs the same pattern). */
void knob_power_set_lvgl_lock(SemaphoreHandle_t mux) { s_lvgl_mux = mux; }

static void ui_lvgl_lock(void) {
    if (s_lvgl_mux != NULL) {
        xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
    }
}
static void ui_lvgl_unlock(void) {
    if (s_lvgl_mux != NULL) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

bool knob_power_rule_epochs_around(int64_t now_epoch, int64_t *prev, int64_t *next) {
    int hour = 0, minute = 0;
    uint8_t mask = 0;
    if (clock_alarm_get_fixed_rule(&hour, &minute, &mask) != ESP_OK || mask == 0) {
        return false; /* interval/pomodoro rules only: nothing to arm */
    }
    const time_t nowt = (time_t)now_epoch;
    struct tm base;
    localtime_r(&nowt, &base);
    bool found = false;
    if (next != NULL) {
        *next = 0;
        for (int off = 0; off < 8; off++) {
            struct tm t = base;
            t.tm_hour = hour;
            t.tm_min = minute;
            t.tm_sec = 0;
            t.tm_isdst = -1; /* TZ rules via mktime, like the scheduler */
            t.tm_mday += off;
            const time_t cand = mktime(&t); /* normalizes, fills tm_wday */
            if (cand <= nowt) {
                continue;
            }
            if ((mask & (1u << t.tm_wday)) != 0) {
                *next = (int64_t)cand;
                found = true;
                break;
            }
        }
    }
    if (prev != NULL) {
        *prev = 0;
        for (int off = 0; off < 8; off++) {
            struct tm t = base;
            t.tm_hour = hour;
            t.tm_min = minute;
            t.tm_sec = 0;
            t.tm_isdst = -1;
            t.tm_mday -= off;
            const time_t cand = mktime(&t);
            if (cand > nowt) {
                continue;
            }
            if ((mask & (1u << t.tm_wday)) != 0) {
                *prev = (int64_t)cand;
                found = true;
                break;
            }
        }
    }
    return found;
}

void knob_power_notify_activity(void) {
    s_last_activity_us = esp_timer_get_time();
    s_sleeping = false; /* real activity (touch, dial-up) is the ONLY exit
                         * from the log-only dark state -- the supervisor's
                         * un-dim branch then restores the ladder's full
                         * look on its next pass */
}

void knob_power_set_hold(bool hold) {
    s_hold = hold;
    if (!hold) {
        knob_power_notify_activity(); /* the ladder restarts from now */
    }
}

static int64_t idle_seconds(void) { return (esp_timer_get_time() - s_last_activity_us) / 1000000; }

static void enter_deep_sleep(int64_t idle) {
    const esp_err_t wake_err = esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0);
    if (wake_err != ESP_OK) {
        /* RTCIO verdict NO for GPIO 8 (it is inside the 0..21 RTC range,
         * so this is the "unexpected" branch): never strand the display. */
        ESP_LOGE(TAG, "EXT0 GPIO %d rejected (%s) -- staying awake", (int)WAKE_GPIO, esp_err_to_name(wake_err));
        knob_power_notify_activity();
        return;
    }
#if CONFIG_KNOB_TOUCH_WAKE
    /* A tap wakes like a detent: EXT1 ANY_LOW on the CST816S INT line
     * (GPIO 9, an RTC IO). One status read first clears a pending INT
     * pulse -- a stale low would re-wake the boot instantly (the
     * boot-loop guard; the INT line idles high on a touched-free chip). */
    uint16_t tx = 0, ty = 0;
    bool tp = false;
    (void)knob_board_touch_read(&tx, &ty, &tp);
    const esp_err_t ext1_err = esp_sleep_enable_ext1_wakeup(1ULL << GPIO_NUM_9, ESP_EXT1_WAKEUP_ANY_LOW);
    if (ext1_err != ESP_OK) {
        ESP_LOGW(TAG, "EXT1 GPIO 9 rejected (%s) -- dial-only wake", esp_err_to_name(ext1_err));
    }
#endif
    /* Alarm across real deep sleep (the user's choice): the S3's internal
     * RTC timer wakes at the next fixed-rule time, +/- the RC drift
     * (bench-verified tolerance <= ~0.5 %). The wake decision (fire /
     * hold-to-rule / ignore) is main.c's arbitration; the never-ring-early
     * rule lives there. GPIO-only cases: no fixed rule, implausible
     * epoch, gap beyond KNOB_ALARM_MAX_SLEEP_GAP_S (weekday-mask holes),
     * or a rejected arm. Imminent rules (< 60 s) DEFER the sleep: the
     * board stays awake and the app-level esp_timer fires the alarm. */
#if CONFIG_KNOB_ALARM_TIMER_WAKE
    bool timer_armed = false;
    int64_t now_epoch = 0;
    if (clock_get_utc_epoch(&now_epoch) == ESP_OK && now_epoch >= EPOCH_PLAUSIBLE_MIN) {
        int64_t rule_prev = 0, rule_next = 0;
        if (knob_power_rule_epochs_around(now_epoch, &rule_prev, &rule_next) && rule_next != 0) {
            const int64_t delta = rule_next - now_epoch;
            if (delta < 60) {
                ESP_LOGI(TAG, "alarm rule in %lld s -- staying awake for the app-level timer", (long long)delta);
                knob_power_notify_activity();
                return;
            }
            if (delta > CONFIG_KNOB_ALARM_MAX_SLEEP_GAP_S) {
                ESP_LOGI(TAG, "next rule %lld s out (> MAX_SLEEP_GAP) -- GPIO-only sleep", (long long)delta);
            } else if (esp_sleep_enable_timer_wakeup((uint64_t)delta * 1000000ULL) == ESP_OK) {
                timer_armed = true;
                ESP_LOGI(TAG, "TIMER wake armed: alarm rule in %lld s", (long long)delta);
            } else {
                ESP_LOGW(TAG, "timer wakeup rejected -- GPIO-only sleep");
            }
        }
    } else {
        ESP_LOGI(TAG, "no plausible epoch (or no rule) -- GPIO-only sleep, no phantom timer");
    }
    (void)timer_armed;
#endif
    /* No RTC on this board: persist the last known instant NOW (the
     * clock component's "clock"/"epoch" snapshot, the P4's discipline) so
     * the wake shows the moon at once instead of a dark panel through the
     * Wi-Fi join + NTP window. */
    clock_nvs_save_epoch();
    ESP_LOGI(TAG, "idle %lld s -- EXT0 GPIO %d accepted%s, deep sleep%s", (long long)idle, (int)WAKE_GPIO,
#if CONFIG_KNOB_TOUCH_WAKE
             " (+EXT1 tap wake)"
#else
             ""
#endif
             ,
#if CONFIG_KNOB_SLEEP_ALLOW
             " now"
#else
             " SKIPPED (KNOB_SLEEP_ALLOW=n: log-only)"
#endif
    );
#if CONFIG_KNOB_UI_ENABLE
    /* The family deep-sleep screen as the last-lit frame (the clocks'
     * actions.c posture): the moon-only screen, the shared clock widget
     * hidden. ONE atomic flush under the UI lock (the screen_switched
     * seam), then the backlight off immediately -- the LCD's panel RAM
     * retains the frame across the power-down (the never-black wake
     * advantage). The legacy own-screen view has no screens: its last
     * frame is the dimmed home. */
    ui_lvgl_lock();
    loadScreen(SCREEN_ID_DEEP_SLEEP);
    lv_obj_add_flag(objects.deep_sleep_clock, LV_OBJ_FLAG_HIDDEN);
    ui_tick();
    ui_lvgl_unlock();
    moon_ui_panel_brightness_set(0); /* immediate, mirror-consistent */
#else
    knob_board_backlight_set(0);
#endif
#if CONFIG_KNOB_SLEEP_ALLOW
    esp_deep_sleep_start(); /* does not return */
#else
    /* KNOB_SLEEP_ALLOW=n (the programming-session gate, the user's call):
     * the dark state is STABLE until real activity. The old code faked
     * activity here, which un-dimmed the board to full every countdown --
     * the board spent half of each cycle at full brightness with the
     * seconds arc ticking, so "did it even enter light sleep?" could not
     * be answered by looking at it (user 2026-11-11). Reset the activity
     * clock directly (NOT via notify_activity -- that is the exit path),
     * stay dark; a touch or a brightening dial turn wakes it, the
     * software stand-in for the EXT0/EXT1 wakes of the real sleep. */
    s_last_activity_us = esp_timer_get_time();
    s_sleeping = true;
#endif
}

static void supervisor_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_hold) {
            continue; /* presenting: the ladder freezes */
        }

        /* Activity is PUSHED, never pulled from here: touch via
         * knob_power_notify_activity() (main.c's read_cb) and dial turns
         * via knob_dial.c — which reports per screen, since on HOME the
         * dial is the brightness control and deliberately is NOT
         * activity (the ladder dims/sleeps through dial fiddling). */

        /* P2: the SLEEP screens own the ladder (the settings.txt
         * round-trip the P2 acceptance asks for). Both rungs read the
         * store every pass — the shared getters, live after a roller
         * commit on the settings screens: dim after idle_timeout_sec
         * (the light-sleep roller), deep sleep one countdown after THAT
         * (the deep-sleep roller), both measured from the last
         * activity — the clock's additive posture (idle -> light-sleep
         * dim -> countdown -> deep), the store's 0 falls back to the
         * Kconfig seeds. The two kill switches (the sleep screens'
         * power-off toggles) read the same way: idle off -> the dim rung
         * is skipped (dim_s = 0, so deep sleep fires at `countdown`
         * after the last activity); deep off -> the ladder dims and
         * stays dim, never esp_deep_sleep (real activity wakes it). */
        const uint32_t idle_to = settings_idle_timeout_sec();
        const uint32_t cnt_down = settings_deep_sleep_countdown_sec();
        const bool idle_on = settings_idle_timeout_enable();
        const bool deep_on = settings_deep_sleep_countdown_enable();
        const int64_t dim_s = idle_on ? (idle_to ? (int64_t)idle_to : CONFIG_KNOB_DIM_AFTER_S) : 0;
        const int64_t countdown = cnt_down ? (int64_t)cnt_down : CONFIG_KNOB_SLEEP_TIMEOUT_S;
        const int64_t sleep_s = dim_s + countdown;

        const int64_t idle = idle_seconds();
        /* Un-dim on activity -- or when the dim rung was just switched
         * OFF (dim_s = 0): a board already dimmed at the toggle must
         * return to full on the next pass, or it would stay dim forever
         * (idle < 0 never holds). */
        if (s_dimmed && !s_sleeping && (dim_s == 0 || idle < dim_s)) {
            s_dimmed = false;
#if CONFIG_KNOB_UI_ENABLE
            /* Back to the family home (both-moon plain flush -- the same
             * disk, so loadScreen skips the fade). The backlight restore
             * rides the seam's brightness set so its LEDC mirror (s_duty)
             * stays consistent with what the panel actually shows. */
            ui_lvgl_lock();
            loadScreen(SCREEN_ID_MODEM_SLEEP);
            ui_tick();
            ui_lvgl_unlock();
            moon_ui_panel_brightness_set(100);
#else
            knob_board_backlight_set(255);
            if (s_dim_cb != NULL) {
                s_dim_cb(false); /* legacy in-place undim (home clock full) */
            }
#endif
            ESP_LOGI(TAG, "backlight back to full (%s)", dim_s == 0 ? "dim rung off" : "activity");
            continue;
        }
        if (!s_dimmed && dim_s > 0 && idle >= dim_s) {
            s_dimmed = true;
#if CONFIG_KNOB_UI_ENABLE
            /* The family light-sleep screen (the clocks' posture,
             * components/ui/actions.c): the dusk screen REPLACES the
             * home (both-moon plain flush -- loadScreen skips the fade
             * for the shared disk), the shared clock widget drops to
             * UI_CLOCK_ALPHA_PCT, then the panel LEDC RAMPs to the
             * family level. The ramp writes no pixels (the sanctioned
             * brightness exception), so the dimmed frame is the one
             * atomic flush that just landed at full level. The
             * light-sleep screen carries no seconds arc, so nothing
             * ticks through the dim. */
            ui_lvgl_lock();
            loadScreen(SCREEN_ID_LIGHT_SLEEP);
            lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_MAX * CONFIG_UI_CLOCK_ALPHA_PCT / 100, 0);
            ui_tick();
            ui_lvgl_unlock();
            moon_ui_panel_dim_ramp(CONFIG_UI_LIGHT_SLEEP_DIM_PCT);
            ESP_LOGI(TAG, "idle %lld s -- light-sleep screen up, LEDC -> %d%%", (long long)idle,
                     (int)CONFIG_UI_LIGHT_SLEEP_DIM_PCT);
#else
            knob_board_backlight_set(CONFIG_KNOB_DIM_DUTY);
            if (s_dim_cb != NULL) {
                s_dim_cb(true); /* legacy in-place dim + the dim observer */
            }
            ESP_LOGI(TAG, "idle %lld s -- backlight dimmed to %d", (long long)idle, (int)CONFIG_KNOB_DIM_DUTY);
#endif
        }
        if (deep_on && !s_sleeping && countdown > 0 && idle >= sleep_s) {
            enter_deep_sleep(idle);
        }
    }
}

void knob_power_init(void) {
    knob_power_notify_activity();
    /* 8 K (was 3 K): the shared-UI rungs load the family sleep screens
     * from here -- loadScreen drives ONE atomic whole-panel lv_refr_now
     * (LVGL sw render + QSPI flush), the same work the 8 K lvgl_task
     * does. The ladder otherwise only delays and logs. */
    xTaskCreate(supervisor_task, "knob_power", 8192, NULL, 4, NULL);
#if CONFIG_KNOB_SLEEP_ALLOW
    const char *sleep_mode = "";
#else
    const char *sleep_mode = " [log-only]";
#endif
    /* P2: the rungs follow the settings store (the sleep screens'
     * rollers), live per pass; the banner quotes the store pair the
     * supervisor starts on (falling back to the Kconfig seeds when a
     * value is unset — app_main must call this AFTER
     * settings_store_load so the pair is real). */
    const uint32_t idle_to = settings_idle_timeout_sec();
    const uint32_t cnt_down = settings_deep_sleep_countdown_sec();
    ESP_LOGI(TAG, "power supervisor up (dim %u s%s, sleep +%u s%s [seeds %d/%d s]%s, EXT0 dial-A wake)",
             (unsigned)(idle_to ? idle_to : (uint32_t)CONFIG_KNOB_DIM_AFTER_S),
             settings_idle_timeout_enable() ? "" : " (rung OFF)",
             (unsigned)(cnt_down ? cnt_down : (uint32_t)CONFIG_KNOB_SLEEP_TIMEOUT_S),
             settings_deep_sleep_countdown_enable() ? "" : " (rung OFF)", (int)CONFIG_KNOB_DIM_AFTER_S,
             (int)CONFIG_KNOB_SLEEP_TIMEOUT_S, sleep_mode);
}
