#include <string.h>

#include "actions.h"
#include "fonts.h"
#include "images.h"
#include "screens.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

static void event_handler_cb_main_arc_clock(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_arc_get_value(ta);
            set_var_clock_seconds(value);
        }
    }
}

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    {
        lv_obj_t *parent_obj = obj;
        {
            // label-clock
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.label_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_montserrat_bold_96_numbers, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "");
        }
        {
            // arc-clock
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_main_arc_clock, LV_EVENT_ALL, 0);
            lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    tick_screen_main();
}

void tick_screen_main() {
    {
        const char *new_val = get_var_clock_label();
        const char *cur_val = lv_label_get_text(objects.label_clock);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.label_clock;
            lv_label_set_text(objects.label_clock, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock;
            lv_arc_set_value(objects.arc_clock, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) { tick_screen_funcs[screen_index](); }
void tick_screen_by_id(enum ScreensEnum screenId) { tick_screen_funcs[screenId - 1](); }

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                              true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    create_screen_main();
}
