#pragma once

/**
 * @file settings_store.h
 * @brief Runtime settings persistence (/spiffs/settings.txt)
 *
 * The Kconfig values are the defaults; the settings screens (light-sleep,
 * deep-sleep) can change them at runtime and persist them across boots.
 * The file is RUNTIME-OWNED: it must not exist in spiffs/ (it would be
 * baked into storage.bin and clobbered on every full flash; a flash
 * resets the settings to the Kconfig defaults, which is the accepted
 * flow).
 *
 * File format: one "key = value" per line, '#' comments, blank lines and
 * unknown keys ignored (the same tolerance as the alarm.txt parsing).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Load the settings from /spiffs/settings.txt
 *
 * Missing file / parse failure: the Kconfig-seeded defaults are kept and
 * ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_DATA is returned. Call once at
 * boot, after the SPIFFS mount (wifi_manager_init) and before the
 * runtime consumers (clock_idle_set_timeout_ms, the countdown sites).
 */
esp_err_t settings_store_load(void);

/**
 * @brief Persist the current settings to /spiffs/settings.txt
 */
esp_err_t settings_store_save(void);

/* Idle timeout (main operation -> light sleep), seconds. Kconfig
 * CLOCK_IDLE_TIMEOUT_SEC, range 10-300. */
uint32_t settings_idle_timeout_sec(void);
void settings_set_idle_timeout_sec(uint32_t sec);

/* Deep-sleep countdown (light sleep -> deep sleep), seconds. Kconfig
 * UI_DEEP_SLEEP_COUNTDOWN_SEC, range 10-300. */
uint32_t settings_deep_sleep_countdown_sec(void);
void settings_set_deep_sleep_countdown_sec(uint32_t sec);

/* Boot sweep animation on/off. Kconfig UI_MOON_ANIM_ENABLE. */
bool settings_boot_anim_enable(void);
void settings_set_boot_anim_enable(bool on);

/* Light-sleep "dusk" animation on/off. Kconfig UI_MOON_DUSK_ENABLE. */
bool settings_dusk_anim_enable(void);
void settings_set_dusk_anim_enable(bool on);

/* Timezone selection, the index into the clock_tz option list (clock_tz.h:
 * 0..52 = the fixed UTC offsets, 53 = Europe/Berlin = the default). */
int settings_timezone_idx(void);
void settings_set_timezone_idx(int idx);

#ifdef __cplusplus
}
#endif
