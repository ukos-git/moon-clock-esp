#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_montserrat_bold_96_numbers;
extern const lv_font_t ui_font_montserrat_regular_48_numbers;
extern const lv_font_t ui_font_fontawesome7_regular_96_icons;
extern const lv_font_t ui_font_fontawesome7_solid_96_icons;
extern const lv_font_t ui_font_montserrat_regular_32_ssid;
extern const lv_font_t ui_font_montserrat_regular_32_range;
/* The Wi-Fi roller's large entries (roller MAIN/SELECTED + the
 * placeholder label, screens.c:1684-1685/:1732 — family-wide, all three
 * tiers bake this face). The knob tier joined last: 37 px under the 48
 * baseline name since 2026-11-10, because the 32 px rows read too small
 * on the 1.8" (docs/devices/knob.rst). */
extern const lv_font_t ui_font_montserrat_regular_48_ssid;

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/
