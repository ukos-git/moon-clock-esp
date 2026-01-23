#include <string.h>

#include "esp_log.h"

#include "vars.h"

char clock_label[6] = {0};
int32_t clock_seconds;

static const char *TAG = "ui_variables";

const char *get_var_clock_label() { return clock_label; }

void set_var_clock_label(const char *value) {
    ESP_LOGI(TAG, "set_var_clock_label called with value: %s", value);
    strncpy(clock_label, value, sizeof(clock_label) / sizeof(char));
    clock_label[sizeof(clock_label) / sizeof(char) - 1] = 0;
}

int32_t get_var_clock_seconds() { return clock_seconds; }

void set_var_clock_seconds(int32_t value) {
    // Apply modulo 60 to keep seconds in 0-59 range
    int32_t clamped_value = value % 60;
    ESP_LOGD(TAG, "set_var_clock_seconds called with value: %d, clamped: %d", value, clamped_value);
    clock_seconds = clamped_value;
}
