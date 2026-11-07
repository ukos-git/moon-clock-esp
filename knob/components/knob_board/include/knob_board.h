/**
 * @file knob_board.h
 * @brief Board shim for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8
 *        (the ESP32-S3R8 app side — the U4WDH keeps its factory firmware).
 *
 * Pin map: vendor schematic + per-function ESP-IDF demo, cross-checked
 * 2026-11-07 (docs/devices/knob.rst carries the table with sources).
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "stdbool.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- display (1.8" round LCD, QSPI x4, 360x360) ---------------------- */
#define KNOB_LCD_H_RES 360
#define KNOB_LCD_V_RES 360
/* Flush strips live in internal DMA RAM. The S3 SPI DMA caps one transfer
 * at 32 KiB (the clock's lesson), so keep H*ROWS*2 well under it:
 * 360*30*2 = 21600 B per buffer. */
#define KNOB_LCD_ROWS_PER_BUF 30

/**
 * @brief Bring up the QSPI bus, the SH8601-class panel and its vendor init.
 *
 * @param done_cb  esp_lcd color-trans-done callback (wires LVGL flush-ready)
 * @param user_ctx opaque pointer handed back to done_cb
 */
esp_err_t knob_board_display_init(esp_lcd_panel_io_color_trans_done_cb_t done_cb, void *user_ctx,
                                  esp_lcd_panel_io_handle_t *io_out, esp_lcd_panel_handle_t *panel_out);

/** @brief Start the GPIO-47 backlight PWM (vendor: LEDC, 5 kHz, 8-bit). */
esp_err_t knob_board_backlight_init(void);

/** @brief Set backlight duty 0..255 (duty=255 is the vendor's "on"). */
esp_err_t knob_board_backlight_set(uint8_t duty);

/* ---- touch (CST816S @ I2C 0x15, SDA 11 / SCL 12, INT 9, RST 10) ------ */

/** @brief Init I2C, reset the CST816S and put it in normal mode. */
esp_err_t knob_board_touch_init(void);

/**
 * @brief Poll one touch sample (vendor read shape: 7 bytes from reg 0x00).
 * @param pressed true when a contact is reported
 * @param x,y panel coordinates clamped to the panel
 */
esp_err_t knob_board_touch_read(uint16_t *x, uint16_t *y, bool *pressed);

/* ---- dial (EC1: A = GPIO 8, B = GPIO 7, 10K pull-ups) ----------------- */

/** @brief Start the 2 ms polling task (the vendor's debouncer style). */
esp_err_t knob_board_encoder_init(void);

/** @brief Signed detent count since boot/reset. */
int32_t knob_board_encoder_count(void);

/** @brief Zero the detent count. */
void knob_board_encoder_reset(void);

#ifdef __cplusplus
}
#endif
