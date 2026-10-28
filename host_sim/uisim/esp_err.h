/* Host stand-in for esp_err.h (UI simulator only -- the firmware
 * build uses the real ESP-IDF header). */
#ifndef UISIM_ESP_ERR_H
#define UISIM_ESP_ERR_H

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_NOT_FOUND -3

#ifdef __cplusplus
extern "C" {
#endif

const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif

#endif /* UISIM_ESP_ERR_H */
