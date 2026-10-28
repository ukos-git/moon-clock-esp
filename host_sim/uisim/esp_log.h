/* Host stand-in for esp_log.h (UI simulator only): the log macros
 * print to stderr so the scripted actions are visible in the run. */
#ifndef UISIM_ESP_LOG_H
#define UISIM_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) printf("I %-18s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W %-18s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("E %-18s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGV(tag, fmt, ...)

#endif /* UISIM_ESP_LOG_H */
