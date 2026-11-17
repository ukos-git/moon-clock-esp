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
/* The boot-anim / dusk Kconfig keys were retired 2026-11-13 (the whole
 * animation stack). The two settings fields below stay as LEGACY no-ops:
 * the store still parses/saves their settings.txt lines (a stale card on an
 * existing install must not break parsing) and keeps their getters' shape,
 * but nothing reads them. The #ifndef seeds (the keys are now undefined in
 * sdkconfig.h) only keep those fields compiling. */
#ifndef CONFIG_UI_MOON_ANIM_ENABLE
#define CONFIG_UI_MOON_ANIM_ENABLE 1
#endif
#ifndef CONFIG_UI_MOON_DUSK_ENABLE
#define CONFIG_UI_MOON_DUSK_ENABLE 1
#endif

/* Alarm sound selection: the index into the alarm_audio sound table
 * (components/ui/alarm_audio.h's ALARM_SOUND_COUNT — 0..3, default 0,
 * the two-tone). The store keeps its own copy of the range so the
 * clock component does not depend on ui (ui already requires clock). */
#define SETTINGS_ALARM_SOUND_MAX 4
#define SETTINGS_ALARM_SOUND_DEFAULT 0

/* Display base-orientation trim (the hub's rotate screen): whole degrees
 * CW, stored 0..359, default 0 = exactly the board's Kconfig mount
 * offset. The store owns the range; the ui reads it at orientation init
 * and after every RELEASED drag (components/ui/actions.c). */
#define SETTINGS_DISPLAY_ROT_DEFAULT 0

static uint32_t s_idle_timeout_sec = CONFIG_CLOCK_IDLE_TIMEOUT_SEC;
static uint32_t s_deep_sleep_countdown_sec = CONFIG_UI_DEEP_SLEEP_COUNTDOWN_SEC;
static bool s_boot_anim_enable = (CONFIG_UI_MOON_ANIM_ENABLE != 0);
static bool s_dusk_anim_enable = (CONFIG_UI_MOON_DUSK_ENABLE != 0);
/* The timezone selection: the IANA name of a clock_tz table zone (see
 * clock_tz.h), seeded with the Kconfig default. The old numeric
 * timezone_idx (generic UTC offsets + one Berlin entry) was retired
 * 2026-11-17 together with the offset options -- the moon observer view
 * needs a named zone with a reference city. */
static char s_timezone[40] = CONFIG_CLOCK_DEFAULT_TIMEZONE;
static int s_alarm_sound_id = SETTINGS_ALARM_SOUND_DEFAULT;
static int s_display_rotation_deg = SETTINGS_DISPLAY_ROT_DEFAULT;

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

/* Parse one "key = value" line into a string field (value trimmed of
 * surrounding spaces and trailing newline; truncated at outsz-1). */
static bool parse_kv_str(const char *line, const char *key, char *out, size_t outsz) {
    char k[32];
    const char *eq = strchr(line, '=');
    if (!eq || (size_t)(eq - line) >= sizeof(k))
        return false;
    memcpy(k, line, (size_t)(eq - line));
    k[eq - line] = 0;
    size_t len = strlen(k);
    while (len > 0 && k[len - 1] == ' ')
        k[--len] = 0;
    if (strcmp(k, key) != 0)
        return false;
    const char *v = eq + 1;
    while (*v == ' ')
        v++;
    size_t n = strcspn(v, "\r\n");
    while (n > 0 && v[n - 1] == ' ')
        n--;
    if (n == 0 || n >= outsz)
        return false;
    memcpy(out, v, n);
    out[n] = 0;
    return true;
}

/* Legacy migration for the retired numeric timezone_idx (the pre-2026-11-17
 * option list: 0..52 = generic UTC offsets, 53 = Europe/Berlin). The
 * generic offsets are gone (the moon observer view needs a city); the
 * Berlin entry maps to its name, everything else keeps the default.
 * Returns NULL = keep the current value. */
static const char *legacy_timezone_idx(long v) {
    if (v == 53)
        return "Europe/Berlin";
    return NULL;
}

esp_err_t settings_store_load(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG,
                 "no %s (or missing SPIFFS entry); using Kconfig defaults "
                 "(idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d, timezone=%s, alarm_sound_id=%d, "
                 "display_rotation_deg=%d)",
                 SETTINGS_PATH, (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
                 s_dusk_anim_enable, s_timezone, s_alarm_sound_id, s_display_rotation_deg);
        return ESP_ERR_NOT_FOUND;
    }
    char line[128];
    long v;
    long legacy_tz_idx = -1; /* legacy numeric timezone_idx, if the file has one */
    bool tz_named = false;   /* the name-based timezone= line was seen */
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
        else if (parse_kv_str(line, "timezone", s_timezone, sizeof(s_timezone)))
            tz_named = true;
        else if (parse_kv(line, "timezone_idx", &v) && v >= 0 && v <= 53)
            legacy_tz_idx = v; /* applied after the loop (never beats a timezone= line) */
        else if (parse_kv(line, "alarm_sound_id", &v) && v >= 0 && v < SETTINGS_ALARM_SOUND_MAX)
            s_alarm_sound_id = (int)v;
        else if (parse_kv(line, "display_rotation_deg", &v) && v >= 0 && v <= 359)
            s_display_rotation_deg = (int)v;
        else if (lines < 10)
            ESP_LOGD(TAG, "unrecognized settings line: %s", line);
    }
    fclose(f);
    if (!tz_named && legacy_tz_idx >= 0) {
        const char *mig = legacy_timezone_idx(legacy_tz_idx);
        if (mig) {
            strncpy(s_timezone, mig, sizeof(s_timezone) - 1);
            s_timezone[sizeof(s_timezone) - 1] = 0;
            ESP_LOGI(TAG, "migrated legacy timezone_idx=%ld -> %s", legacy_tz_idx, s_timezone);
        }
    }
    ESP_LOGI(TAG,
             "settings loaded (idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d, timezone=%s, "
             "alarm_sound_id=%d, display_rotation_deg=%d)",
             (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable, s_dusk_anim_enable,
             s_timezone, s_alarm_sound_id, s_display_rotation_deg);
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
                    "dusk_anim_enable = %d\n"
                    "timezone = %s\n"
                    "alarm_sound_id = %d\n"
                    "display_rotation_deg = %d\n",
                    (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable,
                    s_dusk_anim_enable, s_timezone, s_alarm_sound_id, s_display_rotation_deg);
    fclose(f);
    if (n < 0) {
        ESP_LOGE(TAG, "settings write failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "settings saved (idle=%u s, countdown=%u s, boot_anim=%d, dusk_anim=%d, timezone=%s, "
             "alarm_sound_id=%d, display_rotation_deg=%d)",
             (unsigned)s_idle_timeout_sec, (unsigned)s_deep_sleep_countdown_sec, s_boot_anim_enable, s_dusk_anim_enable,
             s_timezone, s_alarm_sound_id, s_display_rotation_deg);
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

const char *settings_timezone(void) { return s_timezone; }

void settings_set_timezone(const char *name) {
    if (!name || !name[0] || strlen(name) >= sizeof(s_timezone))
        return;
    strcpy(s_timezone, name);
}

int settings_alarm_sound_id(void) { return s_alarm_sound_id; }

void settings_set_alarm_sound_id(int id) {
    if (id < 0 || id >= SETTINGS_ALARM_SOUND_MAX)
        return;
    s_alarm_sound_id = id;
}

int settings_display_rotation_deg(void) { return s_display_rotation_deg; }

void settings_set_display_rotation_deg(int deg) {
    deg %= 360; /* normalise any int into 0..359 (negative wraps) */
    if (deg < 0)
        deg += 360;
    s_display_rotation_deg = deg;
}
