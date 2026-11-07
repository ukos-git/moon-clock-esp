/**
 * @file knob_link.h
 * @brief Knob <-> clock link: Wi-Fi STA join (K2), later UDP frames +
 *        mDNS clock discovery (K4) — see docs plan task #16.
 */
#pragma once

#include "esp_err.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount /spiffs, read knob_credentials.txt (line 1 SSID, line 2
 *        password, '#' comments skipped — clock wifi.c shape) and join as
 *        STA. Keeps retrying in the background; returns once connected or
 *        after the first pass of attempts (never blocks the app forever).
 */
esp_err_t knob_link_wifi_init(void);

/** @brief True while the STA has an IP. */
bool knob_link_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
