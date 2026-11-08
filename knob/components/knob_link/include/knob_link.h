/**
 * @file knob_link.h
 * @brief Wi-Fi STA join for the knob (K2) — connectivity for the moon
 *        display's SNTP (K7d), nothing else.
 *
 * The companion input era (K3/K4: UDP frame TX + mDNS discovery of the
 * clock) was retired 2026-11-08; the knob is a standalone moon display.
 * What survived is the join itself: the credentials file, the retry
 * behaviour and the IP event wiring, unchanged.
 */
#pragma once

#include "esp_err.h"
#include "stdbool.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Join the AP from /spiffs/knob_credentials.txt (K2). Non-fatal:
 *         the handler retries the association in the background. */
esp_err_t knob_link_wifi_init(void);

/** @brief True once associated and holding an IP. */
bool knob_link_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
