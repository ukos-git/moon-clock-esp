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
 * CLOCK_IDLE_TIMEOUT_SEC, range 60-1800. */
uint32_t settings_idle_timeout_sec(void);
void settings_set_idle_timeout_sec(uint32_t sec);

/* Deep-sleep countdown (light sleep -> deep sleep), seconds. Kconfig
 * UI_DEEP_SLEEP_COUNTDOWN_SEC, range 60-1800. */
uint32_t settings_deep_sleep_countdown_sec(void);
void settings_set_deep_sleep_countdown_sec(uint32_t sec);

/* Legacy no-ops (the boot-sweep + dusk animation stack retired 2026-11-13,
 * their Kconfig keys deleted). The store still round-trips their settings.txt
 * lines so an existing card parses; nothing reads the values. */
bool settings_boot_anim_enable(void);
void settings_set_boot_anim_enable(bool on);

bool settings_dusk_anim_enable(void);
void settings_set_dusk_anim_enable(bool on);

/* Timezone selection: the IANA zone name of the clock_tz table
 * (clock_tz.h), persisted as "timezone = Europe/Berlin". The name is the
 * stable key (a zone list edit never shifts the stored choice). Legacy
 * migration on load: a settings.txt that still carries the numeric
 * "timezone_idx" line from before the 2026-11-17 rework maps the old
 * Europe/Berlin entry (idx 53) to the name; the retired generic UTC
 * offsets (old 0..52) fall back to the Kconfig default zone. */
const char *settings_timezone(void);
void settings_set_timezone(const char *name);

/* Alarm sound selection, the index into the alarm_audio sound table
 * (components/ui/alarm_audio.h's ALARM_SOUND_COUNT: 0..3 = two-tone,
 * beep, bell, arpeggio; default 0). Validated to 0..3 here. */
int settings_alarm_sound_id(void);
void settings_set_alarm_sound_id(int id);

/* Display base-orientation trim in degrees, 0..359, clockwise, default 0
 * (the settings hub's rotate entry). ADDED on top of the board's Kconfig
 * UI_ORI_BASE_V10 mount offset: the 90-degree-step orientation pipeline
 * treats the pose held at the 2 s press (or the settled boot pose) as
 * home and displays it rotated by base+trim -- for a clock hung on a
 * wall whose "up" is neither 0 nor 90 degrees. The setter normalises
 * into 0..359 (negative wraps). */
int settings_display_rotation_deg(void);
void settings_set_display_rotation_deg(int deg);

#ifdef __cplusplus
}
#endif
