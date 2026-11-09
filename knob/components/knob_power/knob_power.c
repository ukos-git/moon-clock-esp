/**
 * @file knob_power.c
 * @brief K7e idle ladder: active -> dim (KNOB_DIM_AFTER_S) -> deep sleep
 *        (KNOB_SLEEP_TIMEOUT_S), EXT0 wake on dial-A GPIO 8.
 *
 * The supervisor is a plain registered task on a 500 ms cadence -- it
 * only delays and logs, so a watchdog on it is a feature. Activity
 * comes from two cheap polls (touch is pushed by main's read callback,
 * the dial count is pulled from knob_board) plus the sweep hold owned
 * by knob_ui. Deep sleep is armed with EXT0 GPIO 8 (in the S3 RTCIO
 * range 0..21 -- the runtime accept from esp_sleep_enable_ext0_wakeup
 * is the bench verdict the plan asked for, logged at arm time); the
 * first-detent miss when the B channel leads is documented in the
 * header and on the device page. KNOB_SLEEP_ALLOW=n keeps the device
 * awake and only LOGS the would-be sleep (the plan's sanctioned
 * log-only bench mode).
 */
#include <stdint.h>

#include "clock_sntp.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_power.h"

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
#define CONFIG_KNOB_DIM_DUTY 40
#endif

static volatile int64_t s_last_activity_us; /* written by any task, read by the supervisor */
static volatile bool s_hold;                /* sweep presenting (knob_ui) */
static int32_t s_last_dial_count;           /* supervisor-private */
static bool s_dimmed;                       /* supervisor-private */

void knob_power_notify_activity(void) { s_last_activity_us = esp_timer_get_time(); }

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
    knob_board_backlight_set(0);
#if CONFIG_KNOB_SLEEP_ALLOW
    esp_deep_sleep_start(); /* does not return */
#else
    knob_power_notify_activity(); /* stay up; the ladder re-arms from now */
#endif
}

static void supervisor_task(void *arg) {
    (void)arg;
    s_last_dial_count = knob_board_encoder_count();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_hold) {
            continue; /* presenting: the ladder freezes */
        }

        /* Dial activity is pulled (the encoder shim exposes a count, not
         * a callback); touch is pushed via knob_power_notify_activity(). */
        const int32_t count = knob_board_encoder_count();
        if (count != s_last_dial_count) {
            s_last_dial_count = count;
            knob_power_notify_activity();
        }

        const int64_t idle = idle_seconds();
        if (s_dimmed && idle < CONFIG_KNOB_DIM_AFTER_S) {
            s_dimmed = false;
            knob_board_backlight_set(255);
            ESP_LOGI(TAG, "activity -- backlight back to full");
            continue;
        }
        if (!s_dimmed && CONFIG_KNOB_DIM_AFTER_S > 0 && idle >= CONFIG_KNOB_DIM_AFTER_S) {
            s_dimmed = true;
            knob_board_backlight_set(CONFIG_KNOB_DIM_DUTY);
            ESP_LOGI(TAG, "idle %lld s -- backlight dimmed to %d", (long long)idle, (int)CONFIG_KNOB_DIM_DUTY);
        }
        if (CONFIG_KNOB_SLEEP_TIMEOUT_S > 0 && idle >= CONFIG_KNOB_SLEEP_TIMEOUT_S) {
            enter_deep_sleep(idle);
        }
    }
}

void knob_power_init(void) {
    knob_power_notify_activity();
    xTaskCreate(supervisor_task, "knob_power", 3072, NULL, 4, NULL);
#if CONFIG_KNOB_SLEEP_ALLOW
    const char *sleep_mode = "";
#else
    const char *sleep_mode = " [log-only]";
#endif
    ESP_LOGI(TAG, "power supervisor up (dim %d s, sleep %d s%s, EXT0 dial-A wake)", (int)CONFIG_KNOB_DIM_AFTER_S,
             (int)CONFIG_KNOB_SLEEP_TIMEOUT_S, sleep_mode);
}
