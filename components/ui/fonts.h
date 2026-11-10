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
/* Knob tier only (fonts_knob/, 37 px baked under the 48 baseline name,
 * same ASCII 32-126 as the 32_ssid): the Wi-Fi roller's large entries. */
extern const lv_font_t ui_font_montserrat_regular_48_ssid;

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/
