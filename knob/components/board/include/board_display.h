/* Knob board shim of the clock's board shim (components/board/include/
 * board_display.h): same include surface, knob-backed implementation.
 * The knob has no IO expander (board_io_expander_init -> NULL) and no
 * vendor BSP; the panel handle comes from knob_board_display_init via
 * board_panel_set (the knob's main wires it after display bring-up). */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_lcd_types.h"

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel geometry (mirrors the real shim's BOARD_LCD_W/H). */
#define BOARD_LCD_W (BSP_LCD_H_RES)
#define BOARD_LCD_H (BSP_LCD_V_RES)

/* The knob's SH8601 QSPI panel handle. NULL until the knob display
 * init has run; shared code only consults it after bring-up. */
esp_lcd_panel_handle_t board_panel(void);

/* Knob-side setter: knob_board_display_init records its panel here
 * (the clock's shim reads its BSP's handle instead). */
void board_panel_set(esp_lcd_panel_handle_t panel);

/* No TCA9554 on this board: always NULL (the shared clock's PCF path —
 * the only consumer — is compiled out with CLOCK_USE_PCF85063A=n). */
esp_io_expander_handle_t board_io_expander_init(void);

#ifdef __cplusplus
}
#endif
