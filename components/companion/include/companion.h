/**
 * @file companion.h
 * @brief Clock-side companion input: mDNS advertisement + UDP listener
 *        for the knob (task #16). The listener task only parses and
 *        queues — frames are drained from ui_tick_timer_cb (the LVGL
 *        task), the same "upgrade from the UI tick" pattern as the wake
 *        fix, so nothing network-side ever touches LVGL directly.
 */
#pragma once

#include "companion_proto.h"
#include "esp_err.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the listener + mDNS (call after ui_init). A no-op returning
 *        ESP_OK when CONFIG_COMPANION_ENABLE is not set.
 */
esp_err_t companion_start(void);

/**
 * @brief Pop one accepted frame (call from the LVGL task only).
 * @return true and *out filled when a frame was waiting.
 */
bool companion_poll(companion_frame_t *out);

/**
 * @brief True when CONFIG_COMPANION_KEEP_WIFI: the airplane and light-sleep
 *        hand-offs must NOT deinit the radio (the knob reaches the listener
 *        over LAN at any time). Deep sleep always deinits regardless.
 */
bool companion_wifi_keep_connected(void);

#ifdef __cplusplus
}
#endif
