/* Knob shim of the BSP's display header (the P1 shared UI lands:
 * components/ui/ui_px.h includes "bsp/display.h" for BSP_LCD_H_RES unless
 * the host sim defines PANEL_SIZE). The real BSPs arrive at the same
 * geometry through the Waveshare BSP package; here it is spelled from the
 * knob's SH8601 panel. */
#pragma once

#define BSP_LCD_H_RES 360
#define BSP_LCD_V_RES 360
