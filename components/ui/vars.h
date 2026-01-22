#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables { FLOW_GLOBAL_VARIABLE_CLOCK_SECONDS = 0, FLOW_GLOBAL_VARIABLE_CLOCK_LABEL = 1 };

// Native global variables

extern int32_t get_var_clock_seconds();
extern void set_var_clock_seconds(int32_t value);
extern const char *get_var_clock_label();
extern void set_var_clock_label(const char *value);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/
