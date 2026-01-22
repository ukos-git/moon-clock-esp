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
    ESP_LOGI(TAG, "set_var_clock_seconds called with value: %d", value);
    clock_seconds = value;
}
