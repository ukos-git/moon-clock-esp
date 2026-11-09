/* Knob shim of the BSP header surface (components/clock/include/clock.h
 * includes "bsp/esp-bsp.h" unconditionally; on the two clock boards that
 * resolves to the active Waveshare BSP's header).
 *
 * This is the MINIMAL provider: just the panel geometry the shared clock
 * code's types/logic need. The display + backlight live in knob_board
 * (SH8601 QSPI + LEDC), not here. As the shared UI lands (P1), the bsp_*
 * names it calls (bsp_display_lock/unlock, bsp_display_brightness_set,
 * bsp_i2c_get_handle, ...) are added here and implemented in
 * board_display.c against knob_board — one provider, so the shared TUs
 * compile unchanged. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The knob's round SH8601 panel geometry lives in the sibling shim header
 * (ui_px.h includes "bsp/display.h" directly, the real BSP's layout; the
 * BSP-header consumers must not depend on the knob driver headers). */
#include "bsp/display.h"

#ifdef __cplusplus
}
#endif
