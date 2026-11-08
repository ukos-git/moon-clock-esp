/**
 * @file main.c
 * @brief Moon-display firmware for the Waveshare ESP32-S3-Knob-Touch-
 *        LCD-1.8 (task #24): panel + CST816S + EC1 dial, Wi-Fi STA from
 *        the baked spiffs file (connectivity for K7d's SNTP), and the
 *        standalone moon view (knob_ui over the ported core, knob_moon).
 */
#include <stdio.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_link.h"
#include "knob_moon.h"
#include "knob_power.h"
#include "knob_time.h"
#include "knob_ui.h"
#include "lvgl.h"
#include "nvs_flash.h"

/* #ifndef-seeded (repo invariant: Kconfig 'n'/absent leaves it
 * UNDEFINED; this seeds the Kconfig default, never redefines it). */
#ifndef CONFIG_KNOB_TIME_BOOT_SYNC_MS
#define CONFIG_KNOB_TIME_BOOT_SYNC_MS 20000
#endif

static const char *TAG = "knob_main";

static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
/* LVGL runs in one task; this mutex is the lock for anything that touches
 * it. Must be a MUTEX, not a portMUX: lv_timer_handler renders for tens of
 * ms and a spinlock around it trips the interrupt watchdog. */
static SemaphoreHandle_t s_lvgl_mux;

/* ---- LVGL plumbing ---------------------------------------------------- */

static void lvgl_lock(void) { xSemaphoreTake(s_lvgl_mux, portMAX_DELAY); }

static void lvgl_unlock(void) { xSemaphoreGive(s_lvgl_mux); }

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

/* #31 probe: the dial is a jobless input until the (deferred) settings
 * editor — log the pulse-pair deltas so the bench separates "never
 * sampled" from "sampled, nothing wired to it". */
static void dial_watch_task(void *arg) {
    (void)arg;
    int32_t last = knob_board_encoder_count();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int32_t c = knob_board_encoder_count();
        if (c != last) {
            ESP_LOGI(TAG, "dial %d -> %d", (int)last, (int)c);
            last = c;
        }
    }
}

static void panel_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    /* flush_ready comes from the QSPI trans-done callback below. */
}

static bool trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)io;
    (void)edata;
    (void)user_ctx;
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
        vTaskDelay(pdMS_TO_TICKS(wait_ms < 5 ? 5 : (wait_ms > 20 ? 20 : wait_ms)));
    }
}

/* ---- K7d: boot arm after time truth ----------------------------------- */

/**
 * The knob has no RTC: the display arms ONLY on a trusted instant
 * (never a guessed phase — the clock's never-a-phantom-alarm
 * discipline). Waits for the background join (K2 retries in place),
 * starts SNTP once an IP is held, takes the fix within
 * KNOB_TIME_BOOT_SYNC_MS: fix in window + cold boot plays the synodic
 * sweep (K7c's policy), a late fix arms the STATIC current phase, no
 * fix at all leaves "no time" on glass while the task keeps polling
 * cheaply (a late join + uplink still brings the moon up). Bench
 * builds freeze KNOB_MOON_TEST_EPOCH instead and never start SNTP —
 * what keeps the K7b/K7c acceptance epochs reproducible.
 */
static void boot_arm_task(void *arg) {
    /* K7e: the wake path must be readable from the log (0 = power-on,
     * 1 = EXT0 dial detent after the K7e deep sleep). */
    ESP_LOGI(TAG, "wakeup cause %d (0=power-on, 1=EXT0 dial)", (int)esp_sleep_get_wakeup_cause());
    if (arg == NULL) {
        ESP_LOGW(TAG, "moon frame not shown — caption stays honest on glass");
        vTaskDelete(NULL);
    }
    int64_t now = 0;
    bool have_time = false;
#if CONFIG_KNOB_TIME_USE_TEST_EPOCH
    now = (int64_t)CONFIG_KNOB_MOON_TEST_EPOCH;
    have_time = true;
    ESP_LOGW(TAG, "bench build: frozen test epoch, SNTP not started");
#else
    for (int waited = 0; waited < 15000 && !knob_link_wifi_is_connected(); waited += 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (knob_link_wifi_is_connected()) {
        knob_time_start_sync();
        have_time = knob_time_wait_synced(CONFIG_KNOB_TIME_BOOT_SYNC_MS);
    } else {
        ESP_LOGW(TAG, "Wi-Fi never came up — no NTP attempt");
    }
    if (!have_time) {
        ESP_LOGW(TAG, "no NTP fix in window — 'no time' stays on glass");
        for (;;) {
            if (knob_link_wifi_is_connected()) {
                knob_time_start_sync(); /* idempotent: takes when the join lands */
            }
            if (knob_time_wait_synced(1000)) {
                break;
            }
        }
        now = knob_time_now_epoch();
        ESP_LOGI(TAG, "late NTP fix — static current phase armed");
        (void)knob_ui_show_moon(now);
        vTaskDelete(NULL);
    }
#endif
    const bool cold = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED;
    if (cold && knob_ui_sweep_enabled()) {
        knob_ui_start_sweep(now);
    } else {
        (void)knob_ui_show_moon(now);
    }
    vTaskDelete(NULL);
}

/* ---- entry point ------------------------------------------------------- */

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    s_lvgl_mux = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(knob_board_backlight_init());
    ESP_ERROR_CHECK(knob_board_backlight_set(255)); /* vendor "on" */

    ESP_ERROR_CHECK(knob_board_display_init(trans_done_cb, NULL, NULL, &s_panel));

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

    /* K7e idle ladder: dim after KNOB_DIM_AFTER_S of touch/dial silence,
     * deep sleep (EXT0 dial-A GPIO8) after KNOB_SLEEP_TIMEOUT_S. Boots
     * active; the sweep holds the ladder from knob_ui. */
    knob_power_init();

    /* K2: join the home AP from the baked spiffs file. Non-fatal — the
     * moon renders offline; the join retries in the background and K7d's
     * SNTP rides it. */
    esp_err_t wifi_err = knob_link_wifi_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi join incomplete (%s) — retrying in background", esp_err_to_name(wifi_err));
    }

    /* K7: eager asset load (albedo -> PSRAM, relief gradient cache) runs
     * on the boot core BEFORE the LVGL task exists — the proven
     * moon_esp_assets.c posture: one build, no concurrent-build race. */
    const bool assets_ready = knob_moon_init();

    knob_ui_set_lvgl_lock(s_lvgl_mux);
    knob_ui_create();
    if (assets_ready) {
        /* K7d: the honest pre-frame state while boot_arm_task chases an
         * NTP fix ("moon unavailable" stays if the buffers failed). */
        knob_ui_show_no_time();
    }

    const esp_timer_create_args_t tick_args = {
        .callback = tick_timer_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    xTaskCreate(lvgl_task, "knob_lvgl", 8192, NULL, 10, NULL);
    xTaskCreate(dial_watch_task, "dial_watch", 3072, NULL, 3, NULL);
    /* K7c+K7d: the display arms on a TRUSTED instant only. The wait +
     * the sweep-vs-static decision live on this task (registered, it
     * only delays), so app_main returns at once and neither the ~20 s
     * classify scan nor the NTP window ever touches the boot core. */
    xTaskCreate(boot_arm_task, "knob_boot", 4096, (void *)(intptr_t)(assets_ready ? 1 : 0), 6, NULL);
    ESP_LOGI(TAG, "knob K1 up (heap internal %u DMA-suitable bytes)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}
