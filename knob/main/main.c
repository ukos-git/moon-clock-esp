/**
 * @file main.c
 * @brief Firmware for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8: the
 *        third board of the moon-clock family (P0 seam) — panel + CST816S
 *        + EC1 dial, the clock's shared Wi-Fi manager (spiffs credentials,
 *        K7d's SNTP rides it), the clock component's software time base
 *        (SNTP + NVS epoch snapshot + alarm scheduler + settings store),
 *        and the moon view (knob_ui over components/moon via knob_moon).
 */
#include <stdint.h>
#include <stdio.h>

#include "board_display.h"
#include "clock.h"
#if CONFIG_KNOB_UI_ENABLE
#include "actions.h"
#include "clock_cb.h"
#include "knob_battery.h"
#include "knob_battery_ui.h"
#include "moon_ui.h"
#include "ui.h"
#endif
#include "clock_sntp.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_dial.h"
#include "knob_moon.h"
#include "knob_power.h"
#include "knob_time.h"
#include "knob_ui.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "settings_store.h"
#include "wifi.h"

/* #ifndef-seeded (repo invariant: Kconfig 'n'/absent leaves it
 * UNDEFINED; this seeds the Kconfig default, never redefines it). */
#ifndef CONFIG_KNOB_TIME_BOOT_SYNC_MS
#define CONFIG_KNOB_TIME_BOOT_SYNC_MS 20000
#endif
#ifndef CONFIG_KNOB_ALARM_GRACE_S
#define CONFIG_KNOB_ALARM_GRACE_S 300
#endif
#ifndef CONFIG_KNOB_ALARM_LATE_MAX_S
#define CONFIG_KNOB_ALARM_LATE_MAX_S 900
#endif

/* Same plausibility floor as knob_power's TIMER arm (a factory-default
 * 1970 clock must never arbitrate an alarm). */
#define EPOCH_PLAUSIBLE_MIN 1600000000LL

static const char *TAG = "knob_main";

static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
/* LVGL runs in one task; this mutex is the lock for anything that touches
 * it. Must be a MUTEX, not a portMUX: lv_timer_handler renders for tens of
 * ms and a spinlock around it trips the interrupt watchdog. */
static SemaphoreHandle_t s_lvgl_mux;

/* ---- LVGL plumbing ---------------------------------------------------- */

/* The reentrant owner/depth bookkeeping lives in ONE place (knob_ui.c's
 * wrapper pair around the same mutex — knob_ui_set_lvgl_lock hands it
 * the handle). The LVGL task holds the lock across lv_timer_handler and
 * LVGL event handlers re-lock through it (loadScreen -> seam), so the
 * plain take here would self-deadlock on the first tap (bench
 * 2026-11-10): delegate, so same-task takes are depth bumps. */
static void lvgl_lock(void) { knob_ui_lvgl_lock(); }

static void lvgl_unlock(void) { knob_ui_lvgl_unlock(); }

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    /* #31 probe: a dead CST816S (all err), a silent panel (all ok, no
     * press ever) and a working one are three different log shapes. */
    static uint32_t s_ok, s_err, s_press_logged;
    static int64_t s_hb_us;
    esp_err_t rd = knob_board_touch_read(&x, &y, &pressed);
    if (rd == ESP_OK) {
        s_ok++;
    } else {
        s_err++;
    }
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_hb_us > 5000000) {
        s_hb_us = now_us;
        ESP_LOGI(TAG, "touch heartbeat ok=%u err=%u last=(%u,%u) pressed=%d", (unsigned)s_ok, (unsigned)s_err, x, y,
                 (int)pressed);
    }
    if (rd == ESP_OK && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        knob_power_notify_activity(); /* K7e: touch is activity */
        if (s_press_logged < 20) {
            s_press_logged++;
            ESP_LOGI(TAG, "touch press (%u,%u) [%u/20]", x, y, (unsigned)s_press_logged);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* The dial is an INPUT since P2 (knob_dial.c: cursor step + ladder
 * activity; screens.c's knob branch owns the per-screen semantics). The
 * #31 probe line shape survives inside that task. */

static void panel_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    ESP_LOGI(TAG, "flush START (%d,%d)-(%d,%d)", (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
#endif
    /* Gasp witness: a LOST GPSPI completion parks draw_bitmap on
     * portMAX_DELAY forever (the stock driver has no panel_wdg like the
     * clocks) — FLUSH without a later FLUSH_DONE is exactly that death. */
    knob_ui_gasp(KNOB_GASP_FLUSH);
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    /* flush_ready comes from the QSPI trans-done callback below. */
}

static bool trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)io;
    (void)edata;
    (void)user_ctx;
#if CONFIG_KNOB_DIAG_FRAME_TRACE
    /* EARLY, not plain: this runs in the GPSPI ISR; the newlib stdout lock
     * aborts() when taken from ISR context (bench 2026-11-12: the plain
     * ESP_LOGI here boot-looped the knob at the first atomic flush). */
    ESP_EARLY_LOGI(TAG, "flush DONE");
#endif
    knob_ui_gasp(KNOB_GASP_FLUSH_DONE); /* byte store — safe in ISR context */
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false; /* no higher-priority task woken */
}

static void tick_timer_cb(void *arg) {
    (void)arg;
    lv_tick_inc(2);
}

static void lvgl_task(void *arg) {
    (void)arg;
    while (1) {
        lvgl_lock();
        uint32_t wait_ms = lv_timer_handler();
        lvgl_unlock();
        /* Floor 10 ms = ONE tick at the knob's CONFIG_FREERTOS_HZ=100 (the
         * clock's ff922bb lesson: a sub-tick floor floors to 0 ticks and the
         * task spins the core). */
        vTaskDelay(pdMS_TO_TICKS(wait_ms < 10 ? 10 : (wait_ms > 20 ? 20 : wait_ms)));
    }
}

/* ---- K7d: boot arm after time truth ----------------------------------- */

/**
 * The knob has no RTC: the display arms ONLY on a trusted instant
 * (never a guessed phase — the clock's never-a-phantom-alarm
 * discipline). Waits for the background join (K2 retries in place),
 * starts SNTP once an IP is held, takes the fix within
 * KNOB_TIME_BOOT_SYNC_MS: every arm shows the STATIC current phase
 * (the clock's wake policy -- the knob's own cold-boot sweep retired
 * 2026-11-14), no fix at all leaves "no time" on glass while the task
 * keeps polling cheaply (a late join + uplink still brings the moon
 * up). Bench
 * builds freeze KNOB_MOON_TEST_EPOCH instead and never start SNTP —
 * what keeps the K7b/K7c acceptance epochs reproducible.
 */
static void boot_arm_task(void *arg) {
    /* K7e: the wake path must be readable from the log. ESP-IDF's
     * esp_sleep_wakeup_cause_t reserves ESP_SLEEP_WAKEUP_ALL at 1, so
     * the S3 values are EXT0 = 2 (dial detent on GPIO 8) and EXT1 = 3
     * (tap pulling the CST816S INT low) -- the bench captured 3 on a
     * tap 2026-11-09; the old legend off-by-one'd both causes. */
    ESP_LOGI(TAG, "wakeup cause %d (0=power-on, 2=EXT0 dial, 3=EXT1 tap, 4=TIMER alarm)",
             (int)esp_sleep_get_wakeup_cause());
    /* The power ladder must not run while the first frame is still
     * ahead of the user: app_main armed it at t=0, the ladder released
     * only when moon_task exists -- so a slow join + NTP window let the
     * 15 s dim and the 60 s deep sleep fire over a glass that had never
     * shown anything. Hold it here until a presentation owns the hold. */
    knob_power_set_hold(true);
    if (arg == NULL) {
        ESP_LOGW(TAG, "moon frame not shown — caption stays honest on glass");
        knob_power_set_hold(false); /* nothing will ever present: ladder free */
        vTaskDelete(NULL);
    }
    int64_t now = 0;
    bool have_time = false;
    bool armed = false;
#if CONFIG_KNOB_TIME_USE_TEST_EPOCH
    now = (int64_t)CONFIG_KNOB_MOON_TEST_EPOCH;
    have_time = true;
    ESP_LOGW(TAG, "bench build: frozen test epoch, SNTP not started");
#else
    /* The clock component's snapshot discipline (the P4's path, now the
     * knob's too): restore the "clock"/"epoch" NVS backup; on a board
     * that never booted this firmware, the pre-seam "knob_time"
     * namespace still holds the instant (one-time adopt + promote).
     * Either restore is UNSAFE-to-trust by design (the trust flag stays
     * down until an SNTP fix), so the `wall` bool — not the plausibility
     * of the raw system clock — is what may arm a phase: a factory
     * default clock never shows a silently-wrong moon. */
    bool wall = clock_nvs_restore_epoch();
    if (!wall) {
        wall = knob_time_adopt_legacy_snapshot();
        if (wall) {
            (void)clock_nvs_save_epoch(); /* promote into the clock namespace */
        }
    }
    for (int waited = 0; waited < 15000 && !wifi_manager_is_connected(); waited += 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (wifi_manager_is_connected()) {
        have_time = (clock_sync_time_from_ntp(CONFIG_KNOB_TIME_BOOT_SYNC_MS) == ESP_OK);
    } else {
        ESP_LOGW(TAG, "Wi-Fi never came up — no NTP attempt");
    }
    if (!have_time) {
        /* No RTC: the restored wall clock (last plausible instant, saved
         * on every SNTP fix and before every deep sleep) arms the static
         * moon immediately instead of a dark panel through the join +
         * NTP window. Honest log: it is the LAST KNOWN instant, and the
         * late fix below re-bakes through the show_moon force-rebake. */
        if ((wall || clock_sntp_time_trusted()) && clock_get_utc_epoch(&now) == ESP_OK) {
            ESP_LOGW(TAG, "no NTP fix in window — arming static phase on NVS snapshot %lld (last known)",
                     (long long)now);
            armed = knob_ui_show_moon(now);
        } else {
            ESP_LOGW(TAG, "no NTP fix and no snapshot — 'no time' stays on glass");
        }
        for (;;) {
            if (wifi_manager_is_connected()) {
                if (clock_sntp_sync(2000) == ESP_OK) {
                    break;
                }
            } else {
                /* The shared manager's join retry is bounded; carry the
                 * old knob_link background-rejoin: kick an attempt each
                 * round until the radio takes. */
                (void)esp_wifi_connect();
            }
            if (!armed) {
                /* Nothing will present until a fix lands: the ladder
                 * stays free so an untouched board still sleeps (the
                 * dial/tap wake re-runs this arm). */
                knob_power_set_hold(false);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        now = 0;
        (void)clock_get_utc_epoch(&now);
        ESP_LOGI(TAG, "late NTP fix — static current phase armed");
        (void)knob_ui_show_moon(now); /* s_started -> force-rebake path */
        /* fall through: a TIMER wake whose fix landed LATE still deserves
         * the arbitration below (and the static re-arm takes the real
         * now instead of 0). */
    }
#endif
#if CONFIG_KNOB_ALARM_TIMER_WAKE && CONFIG_KNOB_UI_ENABLE
    /* The boot-fix path never reads `now` (only the snapshot/late-fix
     * branches did) -- the rule math runs against the wall clock, which
     * is fresh right after a successful boot fix. */
    if (now < EPOCH_PLAUSIBLE_MIN) {
        (void)clock_get_utc_epoch(&now);
    }
    /* Alarm across real deep sleep (the user's choice, P1): a TIMER wake
     * is a CANDIDATE rule fire, decided here against the fixed rule
     * around the now-epoch. The NVS snapshot is untrusted by design, but
     * the drift over one sleep is bounded by the RC tolerance the plan
     * accepted -- GRACE absorbs it, and a hold re-reads the clock every
     * second so a landing SNTP fix re-anchors the wait. NEVER ring
     * early; implausible/absent time falls through to a normal wake. */
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && now >= EPOCH_PLAUSIBLE_MIN) {
        int64_t rule_prev = 0, rule_next = 0;
        if (knob_power_rule_epochs_around(now, &rule_prev, &rule_next)) {
            const int64_t late = (rule_prev != 0) ? (now - rule_prev) : INT64_MAX;
            const int64_t early = (rule_next != 0) ? (rule_next - now) : INT64_MAX;
            bool alarm_fire = false;
            if (late <= CONFIG_KNOB_ALARM_GRACE_S) {
                ESP_LOGI(TAG, "TIMER wake %lld s after the rule -- alarm visual now", (long long)late);
                alarm_fire = true;
            } else if (early <= CONFIG_KNOB_ALARM_LATE_MAX_S) {
                ESP_LOGI(TAG, "TIMER wake %lld s EARLY -- holding dark at 1 Hz (never rings early)", (long long)early);
                int64_t remaining = early;
                bool lost_clock = false;
                while (remaining > 0) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    int64_t cur = 0, p2 = 0, n2 = 0;
                    if (clock_get_utc_epoch(&cur) != ESP_OK || !knob_power_rule_epochs_around(cur, &p2, &n2) ||
                        n2 == 0) {
                        lost_clock = true; /* cannot judge the rule any more */
                        break;
                    }
                    remaining = n2 - cur; /* a landing SNTP fix re-anchors */
                }
                if (lost_clock) {
                    /* Never fire an EARLY alarm blind on a clock we lost
                     * mid-hold -- a stale fire wakes like the dial. */
                    ESP_LOGW(TAG, "lost the clock mid-hold -- stale fire, normal wake");
                } else {
                    ESP_LOGI(TAG, "rule time reached after early TIMER wake -- alarm visual");
                    alarm_fire = true;
                }
            } else {
                ESP_LOGW(TAG, "TIMER wake %.0lld s off the rule -- stale fire, normal wake",
                         (late != INT64_MAX) ? (long long)late : -(long long)early);
            }
            if (alarm_fire) {
                /* Off the LVGL task: the function touches the tick timer
                 * and loads the alarm screen, so take the lock around
                 * it (the seam's own refresh nests recursively). */
                knob_ui_lvgl_lock();
                action_alarm_triggered(NULL); /* silent alarm visual (codec-less board) */
                knob_ui_lvgl_unlock();
                /* The alarm screen carries a moon image that nobody has
                 * baked yet (this path never reached the static arm at
                 * the tail): start the producer on the current phase --
                 * the clock's alarm wake shows the moon under the alarm,
                 * and the producer owns the ladder hold from here. The
                 * clock's replay_animation seam was retired with the
                 * dusk family 2026-11-13; the knob arms its own producer
                 * directly (fresh epoch: this branch can outlive the
                 * boot-time `now` by the whole hold). */
                int64_t moon_now = 0;
                if (clock_get_utc_epoch(&moon_now) == ESP_OK && moon_now != 0) {
                    (void)knob_ui_show_moon(moon_now);
                }
                vTaskDelete(NULL);
            }
        }
    }
#endif
    /* The clock's wake policy, family-wide since 2026-11-14: EVERY arm
     * -- cold boot or any wake -- shows the STATIC current phase (the
     * knob's own cold-boot synodic sweep retired with the clock sweep
     * family). */
    (void)knob_ui_show_moon(now);
    vTaskDelete(NULL);
}

/* ---- entry point ------------------------------------------------------- */

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    knob_ui_gasp_boot_report(); /* where was the board when the last crash hit? */

    s_lvgl_mux = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(knob_board_backlight_init());
    ESP_ERROR_CHECK(knob_board_backlight_set(255)); /* vendor "on" */

    ESP_ERROR_CHECK(knob_board_display_init(trans_done_cb, NULL, NULL, &s_panel));
    board_panel_set(s_panel); /* the shared components' board shim reads it */

    lv_init();
    s_disp = lv_display_create(KNOB_LCD_H_RES, KNOB_LCD_V_RES);
    /* SH8601 QSPI RAM takes big-endian RGB565; LVGL renders little-endian,
     * so ask for the pre-swapped buffer or every pixel lands byte-swapped
     * (the K4 purple-wash symptom — invisible on K1's black/white screen).
     * Must be set before the buffers are allocated. */
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    const size_t buf_bytes = KNOB_LCD_H_RES * KNOB_LCD_ROWS_PER_BUF * sizeof(uint16_t);
    uint8_t *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(buf1 == NULL || buf2 == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, panel_flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    ESP_ERROR_CHECK(knob_board_touch_init());
    ESP_ERROR_CHECK(knob_board_encoder_init());

    /* Family seam (P0): the clock's shared Wi-Fi manager — mounts SPIFFS,
     * reads /spiffs/wifi_credentials.txt and joins the home AP.
     * Non-fatal: the moon renders offline; the manager's join retry is
     * bounded, and boot_arm_task carries the background rejoin until a
     * connection lands. K7d's SNTP rides the connection. */
    esp_err_t wifi_err = wifi_manager_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi join incomplete (%s) — boot arm keeps rejoining", esp_err_to_name(wifi_err));
    }

    /* settings.txt BEFORE clock_init (the clock's order): clock_sntp_init
     * reads the timezone selection from the store. */
    const esp_err_t settings_err = settings_store_load();
    if (settings_err != ESP_OK && settings_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "settings.txt load (%s) — keeping Kconfig defaults", esp_err_to_name(settings_err));
    }

    /* K7e/P2 idle ladder: dim after idle_timeout_sec (the light-sleep
     * roller, seeded by KNOB_DIM_AFTER_S), deep sleep (EXT0 dial-A
     * GPIO8) one deep-sleep countdown after that (KNOB_SLEEP_TIMEOUT_S
     * seed). AFTER settings_store_load so the banner quotes the real
     * store pair; boots active, the boot presentation holds the ladder
     * from knob_ui, every dial detent / tap is activity. */
    knob_power_init();

    /* Software time base (CLOCK_USE_PCF85063A=n, the P4's posture):
     * esp_timer countdown/alarm arming, SNTP + NVS epoch snapshot
     * (clock_sntp.c), alarm scheduler loaded from /spiffs/alarm.txt.
     * NTP is boot_arm_task's job (enable_ntp_sync=false) so the display
     * core never waits on the radio window. */
    const clock_config_t clock_cfg = {.enable_ntp_sync = false};
    if (clock_init(&clock_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "clock_init failed — time stays untrusted");
    }

    /* K7: eager asset load (albedo -> PSRAM, relief gradient cache) runs
     * on the boot core BEFORE the LVGL task exists — the proven
     * moon_esp_assets.c posture: one build, no concurrent-build race. */
    const bool assets_ready = knob_moon_init();

    knob_ui_set_lvgl_lock(s_lvgl_mux);
#if CONFIG_KNOB_UI_ENABLE
    /* P1 home parity: the SHARED home screen (moon image attached
     * through the render seam + split-flap HH:MM + alarm row + seconds
     * arc). moon_ui_init before ui_init (the clocks' order: buffers and
     * the descriptor exist before create_screens asks for images). */
    moon_ui_init();
    ui_init();
    /* The family per-second chain (the clocks' registrations, minus the
     * idle pair -- knob_power owns the knob's ladder): minute labels,
     * alarm display + expiry -> silent alarm visual. */
    clock_register_time_update_callback((clock_update_cb_t)action_update_clock_display);
    clock_register_alarm_update_callback((clock_update_cb_t)action_update_alarm_display);
    clock_register_alarm_trigger_callback((clock_update_cb_t)action_alarm_triggered);
    /* The 10 ms UI-tick + 1 s seconds-increment lv_timers: on the clocks
     * these arrive with action_wake_screen, which the knob never runs
     * (knob_power owns the ladder) -- without them the shared home's
     * seconds arc sat frozen between minute ticks (user 2026-11-10).
     * Creation-phase call (lvgl_task does not exist yet), like ui_init. */
    action_ensure_ui_timers();
    /* P1 dim parity: the ladder's dim rung drops the clock widget to
     * UI_CLOCK_ALPHA_PCT alongside the 25 % LEDC backlight (the clocks'
     * dusk posture; knob_power.c calls it from the supervisor task). */
    knob_power_set_dim_cb(knob_render_set_home_dim);
    /* P2 dial input: tap-to-commit listeners on the settings roots +
     * dial task (encoder deltas -> cursor steps, every turn is
     * activity). Runs in the single-threaded UI-creation phase, like
     * ui_init itself. */
    knob_dial_start();
    /* P4 battery gauge: ADC1 ch0 (GPIO1 sense behind the divider,
     * placeholder LiPo calibration) + the "NN %" label at the foot of
     * the shared home, refreshed once a minute. Inert with
     * KNOB_BATTERY_GAUGE=n or a failed ADC init. */
    knob_battery_init();
    knob_battery_ui_attach();
#else
    knob_ui_create();
    if (assets_ready) {
        /* K7d: the honest pre-frame state while boot_arm_task chases an
         * NTP fix ("moon unavailable" stays if the buffers failed). */
        knob_ui_show_no_time();
    }
#endif

    const esp_timer_create_args_t tick_args = {
        .callback = tick_timer_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    xTaskCreate(lvgl_task, "knob_lvgl", 8192, NULL, 10, NULL);
#if CONFIG_KNOB_UI_ENABLE
    /* The family clock tick (clock_cb.c): the software-synthesized RTC
     * interrupts -- minute tick (labels + day probe), alarm expiry
     * (silent alarm visual through action_alarm_triggered), countdown.
     * RTC_POLL_INTERVAL_MS on the clocks is 1300. */
    lv_timer_create(clock_rtc_interrupt_handler_cb, 1300, NULL);
#endif
    /* K7c+K7d: the display arms on a TRUSTED instant only. The wait +
     * the arm live on this task (registered, it only delays), so
     * app_main returns at once and neither the ~20 s classify scan nor
     * the NTP window ever touches the boot core. */
    xTaskCreate(boot_arm_task, "knob_boot", 4096, (void *)(intptr_t)(assets_ready ? 1 : 0), 6, NULL);
    ESP_LOGI(TAG, "knob K1 up (heap internal %u DMA-suitable bytes)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}
