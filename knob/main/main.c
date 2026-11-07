/**
 * @file main.c
 * @brief Companion-knob firmware for the Waveshare ESP32-S3-Knob-Touch-
 *        LCD-1.8 (task #16): panel + CST816S + EC1 dial, Wi-Fi STA from
 *        the baked spiffs file, and the KEYBOARD/STEPPER mirror modes
 *        (knob_ui) that drive the clock over UDP (knob_link).
 */
#include <stdio.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "knob_board.h"
#include "knob_link.h"
#include "knob_ui.h"
#include "lvgl.h"
#include "nvs_flash.h"

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
    if (knob_board_touch_read(&x, &y, &pressed) == ESP_OK && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
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

    /* K2: join the clock's AP from the baked spiffs file. Non-fatal — the
     * dial keeps working offline; the link retries in the background. */
    esp_err_t wifi_err = knob_link_wifi_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi join incomplete (%s) — retrying in background", esp_err_to_name(wifi_err));
    }
    /* K4: TX socket + mDNS discoverer (background task; the UI status
     * line reports "wifi ... / clock? / clock <ip>"). */
    if (knob_link_start() != ESP_OK) {
        ESP_LOGW(TAG, "companion TX not started — dial frames disabled");
    }

    knob_ui_create();

    const esp_timer_create_args_t tick_args = {
        .callback = tick_timer_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));

    xTaskCreate(lvgl_task, "knob_lvgl", 8192, NULL, 10, NULL);
    ESP_LOGI(TAG, "knob K1 up (heap internal %u DMA-suitable bytes)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}
