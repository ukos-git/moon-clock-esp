/**
 * @file board_display.c
 * @brief Knob board shim: the shared components' board-name calls,
 *        answered knob-side (see the header comment). P0 needs only the
 *        panel-handle store; the bsp_* display/brightness surface joins
 *        it with P1's shared screens.
 */
#include "board_display.h"

static esp_lcd_panel_handle_t s_panel;

esp_lcd_panel_handle_t board_panel(void) { return s_panel; }

void board_panel_set(esp_lcd_panel_handle_t panel) { s_panel = panel; }

esp_io_expander_handle_t board_io_expander_init(void) { return NULL; }
