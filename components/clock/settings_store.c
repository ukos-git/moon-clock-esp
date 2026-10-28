/**
 * @file settings_store.c
 * @brief Runtime settings persistence (/spiffs/settings.txt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "settings_store.h"

static const char *TAG = "settings_store";
#define SETTINGS_PATH "/spiffs/settings.txt"

/* The two transition timeouts are user-facing in MINUTES (the settings
 * screens show a 1..30 minute picker). Stored internally in seconds:
 * 1 min .. 30 min = 60 .. 1800 s, default 1 min (60 s). */
#define SETTINGS_TIMEOUT_MIN_SEC 60
#define SETTINGS_TIMEOUT_MAX_SEC 1800

/* Kconfig-seeded defaults (#ifndef fallbacks for the Kconfig-`n`-leaves-
 * the-macro-undefined gotcha). */
#ifndef CONFIG_CLOCK_IDLE_TIMEOUT_SEC
#define CONFIG_CLOCK_IDLE_TIMEOUT_SEC 60
#endif
#ifndef CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC
#define CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC 60
#endif
#ifndef CONFIG_UI_MOON_ANIM_ENABLE
#define CONFIG_UI_MOON_ANIM_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_DUSK_ENABLE
#define CONFIG_UI_MOON_DUSK_ENABLE 1
#endif

static uint32_t s_idle_timeout_sec = CONFIG_CLOCK_IDLE_TIMEOUT_SEC;
static uint32_t s_deep_sleep_countdown_sec = CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC;
static bool s_boot_anim_enable = (CONFIG_UI_MOON_ANIM_ENABLE != 0);
static bool s_dusk_anim_enable = (CONFIG_UI_MOON_DUSK_ENABLE != 0);

/* Parse one "key = value" line into a numeric field; returns true when
 * the line is recognized (the value is validated by the caller). */
static bool parse_kv(const char *line, const char *key, long *out) {
    char k[32];
    const char *eq = strchr(line, '=');
    if (!eq || (size_t)(eq - line) >= sizeof(k))
        return false;
    memcpy(k, line, (size_t)(eq - line));
    k[eq - line] = 0;
    /* Trim trailing spaces in the key, leading spaces in the value. */
    size_t len = strlen(k);
    while (len > 0 && k[len - 1] == ' ')
        k[--len] = 0;
    if (strcmp(k, key) != 0)
        return false;
    char *end;
    long v = strtol(eq + 1, &end, 10);
    *out = v;
    return true;
}

esp_err_t settings_store_load(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG,
                 "no %s (or missing SPIFFS entry); using Kconfig defaults "
                 "(idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d)",
                 SETTINGS_PATH, (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
                 s_dusk_anim_enable);
        return ESP_ERR_NOT_FOUND;
    }
    char line[128];
    long v;
    int lines = 0;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (parse_kv(line, "idle_timeout_sec", &v) && v >= SETTINGS_TIMEOUT_MIN_SEC && v <= SETTINGS_TIMEOUT_MAX_SEC)
            s_idle_timeout_sec = (uint32_t)v;
        else if (parse_kv(line, "deep_sleep_countdown_sec", &v) && v >= SETTINGS_TIMEOUT_MIN_SEC &&
                 v <= SETTINGS_TIMEOUT_MAX_SEC)
            s_deep_sleep_countdown_sec = (uint32_t)v;
        else if (parse_kv(line, "boot_anim_enable", &v))
            s_boot_anim_enable = (v != 0);
        else if (parse_kv(line, "dusk_anim_enable", &v))
            s_dusk_anim_enable = (v != 0);
        else if (lines < 8)
            ESP_LOGD(TAG, "unrecognized settings line: %s", line);
    }
    fclose(f);
    ESP_LOGI(TAG, "settings loaded (idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d)",
             (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
             s_dusk_anim_enable);
    return ESP_OK;
}

esp_err_t settings_store_save(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s for writing", SETTINGS_PATH);
        return ESP_FAIL;
    }
    int n = fprintf(f,
                    "# Runtime settings (owned by the settings screens; do not\n"
                    "# edit by hand -- a full flash resets this file).\n"
                    "idle_timeout_sec = %u\n"
                    "deep_sleep_countdown_sec = %u\n"
                    "boot_anim_enable = %d\n"
                    "dusk_anim_enable = %d\n",
                    (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
                    s_dusk_anim_enable);
    fclose(f);
    if (n < 0) {
        ESP_LOGE(TAG, "settings write failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "settings saved (idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d)",
             (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
             s_dusk_anim_enable);
    return ESP_OK;
}

uint32_t settings_idle_timeout_sec(void) { return s_idle_timeout_sec; }

void settings_set_idle_timeout_sec(uint32_t sec) {
    if (sec < SETTINGS_TIMEOUT_MIN_SEC)
        sec = SETTINGS_TIMEOUT_MIN_SEC;
    if (sec > SETTINGS_TIMEOUT_MAX_SEC)
        sec = SETTINGS_TIMEOUT_MAX_SEC;
    s_idle_timeout_sec = sec;
}

uint32_t settings_deep_sleep_countdown_sec(void) { return s_deep_sleep_countdown_sec; }

void settings_set_deep_sleep_countdown_sec(uint32_t sec) {
    if (sec < SETTINGS_TIMEOUT_MIN_SEC)
        sec = SETTINGS_TIMEOUT_MIN_SEC;
    if (sec > SETTINGS_TIMEOUT_MAX_SEC)
        sec = SETTINGS_TIMEOUT_MAX_SEC;
    s_deep_sleep_countdown_sec = sec;
}

bool settings_boot_anim_enable(void) { return s_boot_anim_enable; }

void settings_set_boot_anim_enable(bool on) { s_boot_anim_enable = on; }

bool settings_dusk_anim_enable(void) { return s_dusk_anim_enable; }

void settings_set_dusk_anim_enable(bool on) { s_dusk_anim_enable = on; }
