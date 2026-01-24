#include "styles.h"
#include "fonts.h"
#include "images.h"

#include "screens.h"
#include "ui.h"

//
// Style: label_regular
//

void init_style_label_regular_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_montserrat_regular_48_numbers);
};

lv_style_t *get_style_label_regular_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_label_regular_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_label_regular(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_label_regular_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_label_regular(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_label_regular_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: label_bold
//

void init_style_label_bold_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_montserrat_bold_96_numbers);
};

lv_style_t *get_style_label_bold_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_label_bold_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_label_bold(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_label_bold_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_label_bold(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_label_bold_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
// Style: moon_arc
//

void init_style_moon_arc_INDICATOR_DEFAULT(lv_style_t *style) {
    lv_style_set_arc_color(style, lv_color_hex(theme_colors[active_theme_index][0]));
};

lv_style_t *get_style_moon_arc_INDICATOR_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_moon_arc_INDICATOR_DEFAULT(style);
    }
    return style;
};

void init_style_moon_arc_KNOB_DEFAULT(lv_style_t *style) {
    lv_style_set_bg_color(style, lv_color_hex(theme_colors[active_theme_index][0]));
};

lv_style_t *get_style_moon_arc_KNOB_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_moon_arc_KNOB_DEFAULT(style);
    }
    return style;
};

void add_style_moon_arc(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_moon_arc_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_moon_arc_KNOB_DEFAULT(), LV_PART_KNOB | LV_STATE_DEFAULT);
};

void remove_style_moon_arc(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_moon_arc_INDICATOR_DEFAULT(), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_moon_arc_KNOB_DEFAULT(), LV_PART_KNOB | LV_STATE_DEFAULT);
};

//
// Style: label_icon
//

void init_style_label_icon_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_font(style, &ui_font_fontawesome7_regular_96_icons);
};

lv_style_t *get_style_label_icon_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_malloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_label_icon_MAIN_DEFAULT(style);
    }
    return style;
};

void add_style_label_icon(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_label_icon_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

void remove_style_label_icon(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_label_icon_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_label_regular,
        add_style_label_bold,
        add_style_moon_arc,
        add_style_label_icon,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_label_regular,
        remove_style_label_bold,
        remove_style_moon_arc,
        remove_style_label_icon,
    };
    remove_style_funcs[styleIndex](obj);
}
