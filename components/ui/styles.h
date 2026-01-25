#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: label_regular
lv_style_t *get_style_label_regular_MAIN_DEFAULT();
void add_style_label_regular(lv_obj_t *obj);
void remove_style_label_regular(lv_obj_t *obj);

// Style: label_bold
lv_style_t *get_style_label_bold_MAIN_DEFAULT();
void add_style_label_bold(lv_obj_t *obj);
void remove_style_label_bold(lv_obj_t *obj);

// Style: moon_arc
lv_style_t *get_style_moon_arc_INDICATOR_DEFAULT();
lv_style_t *get_style_moon_arc_KNOB_DEFAULT();
void add_style_moon_arc(lv_obj_t *obj);
void remove_style_moon_arc(lv_obj_t *obj);

// Style: label_icon_regular
lv_style_t *get_style_label_icon_regular_MAIN_DEFAULT();
void add_style_label_icon_regular(lv_obj_t *obj);
void remove_style_label_icon_regular(lv_obj_t *obj);

// Style: label_icon_solid
lv_style_t *get_style_label_icon_solid_MAIN_DEFAULT();
void add_style_label_icon_solid(lv_obj_t *obj);
void remove_style_label_icon_solid(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/
