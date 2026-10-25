#include <string.h>

#include "actions.h"
#include "clock_idle.h"
#include "esp_log.h"
#include "fonts.h"
#include "images.h"
#include "moon_ui.h"
#include "screens.h"
#include "styles.h"
#include "ui.h"
#include "vars.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;
uint32_t active_theme_index = 0;

/* Pure-black screen background: the moon image is black outside the disk,
 * so the corners blend into the screen (AMOLED black = off pixels).
 * The moon image must be created FIRST (LVGL 9.4 has no public z-order
 * reorder API; z-order = creation order) so the clock/arc render on top. */
static void moon_screen_prepare(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    moon_ui_create_image(obj);
}

static void event_handler_cb_main_arc_clock_seconds(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    if (event == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *ta = lv_event_get_target(e);
        if (tick_value_change_obj != ta) {
            int32_t value = lv_arc_get_value(ta);
            set_var_clock_seconds(value);
        }
    }
}

static void event_handler_cb_modem_sleep_arc_clock_seconds_modem(lv_event_t *e) {
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
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // arc-clock-seconds
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_main_arc_clock_seconds, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff787878), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            // main_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.main_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 7);
        }
        {
            // screen-indicator-wifi
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_wifi = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_main();
}

void tick_screen_main() {
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock_seconds);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock_seconds;
            lv_arc_set_value(objects.arc_clock_seconds, new_val);
            tick_value_change_obj = NULL;
        }
    }
    tick_user_widget_clock(7);
}

void create_screen_modem_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.modem_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // modem_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.modem_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 12);
        }
        {
            // screen-indicator-modem
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_modem = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, "");
        }
        {
            // arc-clock-seconds-modem
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.arc_clock_seconds_modem = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_arc_set_range(obj, 0, 60);
            lv_arc_set_bg_start_angle(obj, 0);
            lv_arc_set_bg_end_angle(obj, 360);
            lv_arc_set_rotation(obj, 270);
            lv_obj_add_event_cb(obj, event_handler_cb_modem_sleep_arc_clock_seconds_modem, LV_EVENT_ALL, 0);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(obj, LV_STATE_DISABLED);
            add_style_moon_arc(obj);
            lv_obj_set_style_pad_top(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xff787878), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
    }

    tick_screen_modem_sleep();
}

void tick_screen_modem_sleep() {
    tick_user_widget_clock(12);
    {
        int32_t new_val = get_var_clock_seconds();
        int32_t cur_val = lv_arc_get_value(objects.arc_clock_seconds_modem);
        if (new_val != cur_val) {
            tick_value_change_obj = objects.arc_clock_seconds_modem;
            lv_arc_set_value(objects.arc_clock_seconds_modem, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void create_screen_light_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.light_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // light_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.light_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 18);
        }
        {
            // screen-indicator-sleep0
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_sleep0 = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_regular(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_light_sleep();
}

void tick_screen_light_sleep() { tick_user_widget_clock(18); }

void create_screen_deep_sleep() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.deep_sleep = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    moon_screen_prepare(obj);
    {
        lv_obj_t *parent_obj = obj;
        {
            // deep_sleep_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.deep_sleep_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 23);
        }
        {
            // screen-indicator-sleep1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_sleep1 = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_solid(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_deep_sleep();
}

void tick_screen_deep_sleep() { tick_user_widget_clock(23); }

/* The alarm screen dismisses on touch. The idle-wake path (touch while
 * asleep -> action_switch_to_main_screen) only fires while the device is
 * in idle mode; while awake, taps just reset the idle timer, so without
 * this handler a tapping user never leaves the screen (and never lets it
 * sleep into a dismissable state). Asleep, the wake path handles it and
 * this returns early so the switch only happens once. */
static void event_handler_cb_alarm_root(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSED)
        return;
    if (clock_is_idle())
        return;
    ESP_LOGI("ui_screens", "Alarm dismissed by touch (device awake)");
    action_switch_to_main_screen(e);
}

void create_screen_alarm() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.alarm = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 466, 466);
    lv_obj_add_event_cb(obj, event_handler_cb_alarm_root, LV_EVENT_PRESSED, 0);
    {
        lv_obj_t *parent_obj = obj;
        {
            // alarm_clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.alarm_clock = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            create_user_widget_clock(obj, 28);
        }
        {
            // screen-indicator-alarm
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.screen_indicator_alarm = obj;
            lv_obj_set_pos(obj, 185, 320);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            add_style_label_icon_regular(obj);
            lv_label_set_text(obj, "");
        }
    }

    tick_screen_alarm();
}

void tick_screen_alarm() { tick_user_widget_clock(28); }

void create_user_widget_clock(lv_obj_t *parent_obj, int startWidgetIndex) {
    (void)startWidgetIndex;
    lv_obj_t *obj = parent_obj;
    {
        lv_obj_t *parent_obj = obj;
        {
            // clock
            lv_obj_t *obj = lv_obj_create(parent_obj);
            ((lv_obj_t **)&objects)[startWidgetIndex + 0] = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 466, 466);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_END, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_flex_track_place(obj, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // label-clock-time
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 1] = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_label_bold(obj);
                    lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // label-clock-alarm
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    ((lv_obj_t **)&objects)[startWidgetIndex + 2] = obj;
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    add_style_label_regular(obj);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
}

void tick_user_widget_clock(int startWidgetIndex) {
    (void)startWidgetIndex;
    {
        const char *new_val = get_var_clock_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 1]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 1];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 1], new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = get_var_alarm_label();
        const char *cur_val = lv_label_get_text(((lv_obj_t **)&objects)[startWidgetIndex + 2]);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = ((lv_obj_t **)&objects)[startWidgetIndex + 2];
            lv_label_set_text(((lv_obj_t **)&objects)[startWidgetIndex + 2], new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void change_color_theme(uint32_t theme_index) {
    active_theme_index = theme_index;

    lv_style_set_arc_color(get_style_moon_arc_INDICATOR_DEFAULT(), lv_color_hex(theme_colors[theme_index][0]));

    lv_style_set_bg_color(get_style_moon_arc_KNOB_DEFAULT(), lv_color_hex(theme_colors[theme_index][0]));

    lv_obj_invalidate(objects.main);
    lv_obj_invalidate(objects.modem_sleep);
    lv_obj_invalidate(objects.light_sleep);
    lv_obj_invalidate(objects.deep_sleep);
    lv_obj_invalidate(objects.alarm);
}

uint32_t theme_colors[1][1] = {
    {0xff787878},
};

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main, tick_screen_modem_sleep, tick_screen_light_sleep, tick_screen_deep_sleep, tick_screen_alarm,
};
void tick_screen(int screen_index) { tick_screen_funcs[screen_index](); }
void tick_screen_by_id(enum ScreensEnum screenId) { tick_screen_funcs[screenId - 1](); }

void create_screens() {
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                              true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    create_screen_main();
    create_screen_modem_sleep();
    create_screen_light_sleep();
    create_screen_deep_sleep();
    create_screen_alarm();
}
