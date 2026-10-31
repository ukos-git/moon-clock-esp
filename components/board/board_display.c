#include "board_display.h"

esp_lcd_panel_handle_t board_panel(void) {
#if defined(CONFIG_BOARD_P4)
    return bsp_display_get_panel_handle();
#else
    return bsp_display_get_panel();
#endif
}

esp_io_expander_handle_t board_io_expander_init(void) {
    /* The P4 board has no IO expander (no PCF85063A interrupt line). */
#if defined(CONFIG_BOARD_P4)
    return NULL;
#else
    return bsp_io_expander_init();
#endif
}
