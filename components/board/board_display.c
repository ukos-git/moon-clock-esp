#include "board_display.h"

/* The knob stores its panel handle here (knob_board_display_init records it
 * via board_panel_set after its own SH8601 bring-up); the clocks read their
 * BSP's handle directly, so s_panel is only consulted on the knob. */
static esp_lcd_panel_handle_t s_panel;

esp_lcd_panel_handle_t board_panel(void) {
#if defined(CONFIG_BOARD_KNOB)
    return s_panel;
#elif defined(CONFIG_BOARD_P4)
    return bsp_display_get_panel_handle();
#else
    return bsp_display_get_panel();
#endif
}

void board_panel_set(esp_lcd_panel_handle_t panel) { s_panel = panel; }

esp_lcd_panel_io_handle_t board_panel_io(void) {
#if defined(CONFIG_BOARD_KNOB) || defined(CONFIG_BOARD_P4)
    /* No QSPI io on the knob (its drain witness is the shared seam's own)
     * or the P4 (MIPI-DSI has no such io): the name only needs to LINK. */
    return NULL;
#else
    return bsp_display_get_panel_io();
#endif
}

esp_io_expander_handle_t board_io_expander_init(void) {
    /* No TCA9554 on the knob (no PCF85063A interrupt line) or the P4
     * (no IO expander at all). */
#if defined(CONFIG_BOARD_KNOB) || defined(CONFIG_BOARD_P4)
    return NULL;
#else
    return bsp_io_expander_init();
#endif
}
