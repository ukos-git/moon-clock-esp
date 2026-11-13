/* Board shim: the stable app-side surface over the two BSPs (the vendored
 * Waveshare ESP32-S3-Touch-AMOLED-1.75 BSP and the registry Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-XC BSP).
 *
 * Both BSPs ship a `bsp/esp-bsp.h` that resolves to the ACTIVE board
 * header, and they export an identical API except for two names:
 *   - bsp_display_get_panel() (S3) vs bsp_display_get_panel_handle() (P4)
 *   - the IO expander: bsp_io_expander_init() exists only on the S3
 *     (TCA9554; the P4 board has no expander).
 *
 * This header re-exports the active board header and normalizes those two
 * via board_panel() / board_io_expander_init(). Everything else —
 * bsp_display_start(), bsp_display_lock/unlock(), bsp_display_backlight_
 * on/off(), bsp_display_brightness_set(), bsp_i2c_get_handle(),
 * BSP_LCD_H_RES/V_RES — is identical on both BSPs and the app calls it
 * through each board's own BSP headers unchanged. */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_lcd_types.h"

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel geometry (both BSPs define BSP_LCD_H_RES/V_RES). */
#define BOARD_LCD_W (BSP_LCD_H_RES)
#define BOARD_LCD_H (BSP_LCD_V_RES)

/* The active BSP's panel handle (S3 bsp_display_get_panel, P4
 * bsp_display_get_panel_handle). Valid after bsp_display_start. */
esp_lcd_panel_handle_t board_panel(void);

/* The S3's QSPI panel io handle (bsp_display_get_panel_io) -- the
 * single spi_device every panel write rides. The P4's MIPI-DSI path
 * has no such io: returns NULL there (its drain witness is
 * S3-only). */
esp_lcd_panel_io_handle_t board_panel_io(void);

/* The S3 IO expander handle (TCA9554; the PCF85063A interrupt line).
 * The P4 board has no expander: returns NULL. */
esp_io_expander_handle_t board_io_expander_init(void);

#ifdef __cplusplus
}
#endif
