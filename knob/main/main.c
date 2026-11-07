/**
 * @file main.c
 * @brief K1 skeleton test for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 *        companion firmware: panel + CST816S + EC1 dial on one screen.
 *
 * Serial-verification target (task #19): boot log shows panel/touch/dial
 * init, the screen shows "KNOB K1", a live detent count and the last touch
 * point; tapping anywhere zeroes the count. K2 replaces the chrome with
 * the Wi-Fi join; K4 replaces it with the KEYBOARD/STEPPER modes.
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
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "knob_main";

static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
static lv_obj_t *s_dial_label;
static lv_obj_t *s_touch_label;
/* LVGL runs in one task; this mutex is the lock for anything that touches
 * it. Must be a MUTEX, not a portMUX: lv_timer_handler renders for tens of
 * ms and a spinlock around it trips the interrupt watchdog. */
static SemaphoreHandle_t s_lvgl_mux;

/* Last touch sample, written by the indev read cb (LVGL task context). */
static uint16_t s_touch_x;
static uint16_t s_touch_y;
static bool s_touch_down;

/* ---- LVGL plumbing ---------------------------------------------------- */

static void lvgl_lock(void) { xSemaphoreTake(s_lvgl_mux, portMAX_DELAY); }

static void lvgl_unlock(void) { xSemaphoreGive(s_lvgl_mux); }

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    if (knob_board_touch_read(&x, &y, &pressed) == ESP_OK && pressed) {
        s_touch_x = x;
        s_touch_y = y;
        s_touch_down = true;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        s_touch_down = false;
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

/* ---- K1 test screen ---------------------------------------------------- */

static void tap_reset_cb(lv_event_t *e) {
    (void)e;
    knob_board_encoder_reset();
}

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    char buf[96];
    snprintf(buf, sizeof(buf), "dial %ld", (long)knob_board_encoder_count());
    lv_label_set_text(s_dial_label, buf);
    snprintf(buf, sizeof(buf), "touch %u,%u %s", s_touch_x, s_touch_y, s_touch_down ? "DOWN" : "up");
    lv_label_set_text(s_touch_label, buf);
}

static void ui_create(void) {
    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *tap = lv_obj_create(scr);
    lv_obj_set_size(tap, KNOB_LCD_H_RES, KNOB_LCD_V_RES);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_pad_all(tap, 0, 0);
    lv_obj_set_scrollable(tap, false);
    lv_obj_add_event_cb(tap, tap_reset_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KNOB K1");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_clickable(title, false);

    s_dial_label = lv_label_create(scr);
    lv_label_set_text(s_dial_label, "dial 0");
    lv_obj_set_style_text_font(s_dial_label, &lv_font_montserrat_48, 0);
    lv_obj_center(s_dial_label);
    lv_obj_set_clickable(s_dial_label, false);

    s_touch_label = lv_label_create(scr);
    lv_label_set_text(s_touch_label, "touch -,- up");
    lv_obj_align(s_touch_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_set_clickable(s_touch_label, false);

    lv_timer_create(ui_timer_cb, 100, NULL);
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

    ui_create();

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
