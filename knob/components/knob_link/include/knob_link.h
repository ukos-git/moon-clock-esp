/**
 * @file knob_link.h
 * @brief Wi-Fi join + companion frame TX for the knob (task #16).
 *
 * K2: STA join from the baked spiffs credentials file. K4: UDP sender +
 * mDNS discovery of the clock's listener (service _moonclock._udp.local),
 * broadcast fallback while the address is unknown. Wire contract shared
 * with the clock: components/companion/include/companion_proto.h.
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

/**
 * @brief Create the TX socket, mdns_init() and start the background clock
 *        discoverer (call after knob_link_wifi_init). Safe to call once.
 */
esp_err_t knob_link_start(void);

/** @brief True once the clock was discovered (unicast targets live). */
bool knob_link_clock_ready(void);

/** @brief Dotted-quad of the discovered clock, or "broadcast". */
const char *knob_link_clock_ip_str(void);

/**
 * @brief Build and send one companion frame (seq counter maintained).
 * @param type  companion_frame_type_t value
 * @param field companion_field_t value (or the KEY_MODE register / the
 *              KEY_HOT|KEY_ADD slot — the delta byte carries the payload)
 * @param delta signed step (STEP) or payload byte
 */
esp_err_t knob_link_send(uint8_t type, uint8_t field, int8_t delta);

/** @brief Convenience: send HELLO (logged by the clock, not queued). */
esp_err_t knob_link_hello(void);

#ifdef __cplusplus
}
#endif
