/**
 * @file clock_tz.c
 * @brief Runtime timezone selection (see clock_tz.h).
 *
 * The table below is the selectable list: 67 curated named IANA zones
 * (one entry per typical major city/region), ordered west to east by
 * standard offset. Each POSIX TZ string is hand-verified against the
 * system zoneinfo over 2026-01-01..2031-01-01 by host_sim/tz_check.py
 * (run it after touching any rule; zones listed in that script's
 * APPROXIMATE set are documented rule approximations and report their
 * worst-case difference instead of failing).
 *
 * The lat/lon are the reference CITY's coordinates (degrees x10): the
 * moon's observer-view viewport (moon_viewport_rot_deg) tilts the disk
 * as an observer at that city sees it. This is why the generic fixed
 * UTC-offset options (the old "UTC+02" list) are gone -- an offset has
 * no city to project from.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_alarm_scheduler.h"
#include "clock_tz.h"
#include "settings_store.h"

static const char *TAG = "clock_tz";

/* Kconfig-seeded default zone name (#ifndef fallback for the
 * Kconfig-leaves-the-macro-undefined gotcha). CONFIG_CLOCK_DEFAULT_
 * TIMEZONE names a table zone by its IANA name; a value outside the
 * table is applied as a raw POSIX TZ string (north-up viewport, no
 * observer city). */
#ifndef CONFIG_CLOCK_DEFAULT_TIMEZONE
#define CONFIG_CLOCK_DEFAULT_TIMEZONE "Europe/Berlin"
#endif

/* The curated selectable zones (see clock_tz.h). Field order: IANA name,
 * roller label, POSIX TZ string, city lat x10, city lon x10.
 *
 * DST rule notes (all verified 2026-11-17 by host_sim/tz_check.py):
 *   US/Canada (NY..Chicago, Denver, Anchorage, Toronto, Halifax):
 *   2nd Sun Mar 02:00 -> 1st Sun Nov 02:00. Vancouver has been on
 *   year-round UTC-7 (permanent DST) per the current tzdata. Mexico
 *   City has had no DST since 2022. Phoenix never. Brazil (Sao Paulo)
 *   abolished DST 2019; Argentina never. Caracas has been a permanent
 *   UTC-4 (it left UTC-4:30 again per the current tzdata).
 *   EU (London/Lisbon/Madrid/Paris/Berlin/Rome/Warsaw/Athens/Helsinki/
 *   Bucharest/Kyiv): last Sun Mar 01:00 UTC / last Sun Oct 01:00 UTC
 *   (spelled in each zone's local standard time). Moscow permanent MSK
 *   since 2014; Istanbul permanent +03 since 2016; Iceland/Reykjavik
 *   never; Lagos/Nairobi/Johannesburg/Algiers permanent (WAT/EAT/SAST/
 *   CET); Cairo re-introduced DST 2023 (rule below, tz_check-verified).
 *   Iran permanent +3:30 since 2022. Israel + DST: the M-form is the
 *   tz_check APPROXIMATE entry (holidays shift the real rule by up to a
 *   week). Gulf/South/Southeast/North Asia and Australia: no DST except
 *   Sydney/Adelaide (AEDT/ACDT: 1st Sun Oct 02:00 -> 1st Sun Apr 03:00
 *   AEST/ACST local) and Auckland (last Sun Sep 02:00 -> 1st Sun Apr
 *   03:00 NZST local). Fiji has no DST per the current tzdata.
 * Re-run host_sim/tz_check.py after ANY rule touch (and after a tzdata
 * bump on the build host -- a civil-calendar change shows up there).
 */
static const clock_tz_zone_t s_zones[] = {
    /* UTC-11 */
    { "Pacific/Pago_Pago", "Pago Pago", "UTC11", -143, -1707 },
    /* UTC-10 */
    { "Pacific/Honolulu", "Honolulu", "UTC10", 213, -1579 },
    /* UTC-9 */
    { "America/Anchorage", "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0", 612, -1499 },
    /* UTC-8 */
    { "America/Vancouver", "Vancouver", "UTC7", 493, -1231 },
    { "America/Los_Angeles", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0", 340, -1182 },
    /* UTC-7 */
    { "America/Denver", "Denver", "MST7MDT,M3.2.0,M11.1.0", 397, -1050 },
    { "America/Phoenix", "Phoenix", "MST7", 334, -1121 },
    /* UTC-6 */
    { "America/Chicago", "Chicago", "CST6CDT,M3.2.0,M11.1.0", 419, -876 },
    { "America/Mexico_City", "Mexico City", "CST6", 194, -991 },
    /* UTC-5 */
    { "America/New_York", "New York", "EST5EDT,M3.2.0,M11.1.0", 407, -740 },
    { "America/Toronto", "Toronto", "EST5EDT,M3.2.0,M11.1.0", 437, -794 },
    { "America/Bogota", "Bogota", "UTC5", 46, -741 },
    { "America/Lima", "Lima", "UTC5", -120, -770 },
    /* UTC-4 */
    { "America/Caracas", "Caracas", "UTC4", 105, -669 },
    /* UTC-4 (Santiago: Chile DST, 1st Sun Sep 00:00 -> 1st Sat Apr
     * 24:00 local) */
    { "America/Santiago", "Santiago", "UTC4CHST,M9.1.0/0,M4.1.6/24", -346, -707 },
    { "America/Halifax", "Halifax", "AST4ADT,M3.2.0,M11.1.0", 446, -636 },
    /* UTC-3 */
    { "America/Argentina/Buenos_Aires", "Buenos Aires", "UTC3", -346, -584 },
    { "America/Sao_Paulo", "Sao Paulo", "UTC3", -235, -466 },
    /* UTC+0 */
    { "Atlantic/Reykjavik", "Reykjavik", "UTC0", 641, -219 },
    { "Europe/London", "London", "GMT0BST,M3.5.0/1,M10.5.0/2", 515, -1 },
    { "Europe/Lisbon", "Lisbon", "WET0WEST,M3.5.0/1,M10.5.0/2", 387, -91 },
    /* UTC+1 (EU rule: last Sun Mar 02:00 local -> last Sun Oct 03:00
     * local, spelled per zone) */
    { "Europe/Madrid", "Madrid", "CET-1CEST,M3.5.0/2,M10.5.0/3", 404, -37 },
    { "Europe/Paris", "Paris", "CET-1CEST,M3.5.0/2,M10.5.0/3", 489, 23 },
    { "Europe/Berlin", "Berlin", "CET-1CEST,M3.5.0/2,M10.5.0/3", 525, 134 },
    { "Europe/Rome", "Rome", "CET-1CEST,M3.5.0/2,M10.5.0/3", 419, 125 },
    { "Europe/Warsaw", "Warsaw", "CET-1CEST,M3.5.0/2,M10.5.0/3", 522, 210 },
    { "Africa/Algiers", "Algiers", "CET-1", 368, 31 },
    { "Africa/Lagos", "Lagos", "WAT-1", 65, 34 },
    { "Africa/Johannesburg", "Johannesburg", "SAST-2", -262, 280 },
    /* UTC+2 */
    { "Europe/Athens", "Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4", 380, 237 },
    { "Europe/Helsinki", "Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4", 602, 249 },
    { "Europe/Bucharest", "Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4", 444, 261 },
    { "Europe/Kyiv", "Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4", 505, 305 },
    { "Africa/Cairo", "Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24", 300, 312 },
    { "Asia/Jerusalem", "Jerusalem", "IST-2IDT,M3.2.5/2,M10.1.0/2", 318, 352 },
    /* UTC+3 */
    { "Europe/Istanbul", "Istanbul", "UTC-3", 410, 290 },
    { "Europe/Moscow", "Moscow", "MSK-3", 558, 376 },
    { "Africa/Nairobi", "Nairobi", "EAT-3", -13, 368 },
    /* UTC+3:30 */
    { "Asia/Tehran", "Tehran", "UTC-3:30", 357, 514 },
    /* UTC+4 */
    { "Asia/Dubai", "Dubai", "GST-4", 252, 553 },
    /* UTC+4:30 */
    { "Asia/Kabul", "Kabul", "UTC-4:30", 346, 692 },
    /* UTC+5 */
    { "Asia/Karachi", "Karachi", "PKT-5", 249, 670 },
    { "Asia/Almaty", "Almaty", "UTC-5", 432, 769 },
    /* UTC+5:30 / +5:45 */
    { "Asia/Kolkata", "Kolkata", "IST-5:30", 286, 772 },
    { "Asia/Kathmandu", "Kathmandu", "NPT-5:45", 277, 853 },
    /* UTC+6 / +6:30 */
    { "Asia/Dhaka", "Dhaka", "BST-6", 238, 904 },
    { "Asia/Yangon", "Yangon", "UTC-6:30", 169, 962 },
    /* UTC+7 */
    { "Asia/Bangkok", "Bangkok", "ICT-7", 138, 1005 },
    { "Asia/Jakarta", "Jakarta", "WIB-7", -62, 1069 },
    /* UTC+8 */
    { "Asia/Singapore", "Singapore", "SGT-8", 14, 1038 },
    { "Asia/Kuala_Lumpur", "Kuala Lumpur", "MYT-8", 31, 1017 },
    { "Asia/Hong_Kong", "Hong Kong", "HKT-8", 223, 1142 },
    { "Asia/Shanghai", "Shanghai", "CST-8", 312, 1215 },
    { "Asia/Taipei", "Taipei", "CST-8", 250, 1216 },
    { "Asia/Manila", "Manila", "PHT-8", 146, 1210 },
    { "Australia/Perth", "Perth", "AWST-8", -320, 1159 },
    /* UTC+9 */
    { "Asia/Seoul", "Seoul", "KST-9", 376, 1270 },
    { "Asia/Tokyo", "Tokyo", "JST-9", 357, 1397 },
    /* UTC+9:30 */
    { "Australia/Darwin", "Darwin", "ACST-9:30", -125, 1308 },
    { "Australia/Adelaide", "Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3", -349, 1386 },
    /* UTC+10 */
    { "Australia/Brisbane", "Brisbane", "AEST-10", -275, 1530 },
    { "Australia/Sydney", "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", -339, 1512 },
    { "Pacific/Guam", "Guam", "ChST-10", 134, 1448 },
    /* UTC+12 */
    { "Pacific/Auckland", "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3", -368, 1748 },
    { "Pacific/Fiji", "Fiji", "UTC-12", -181, 1784 },
    /* UTC+13 */
    { "Pacific/Tongatapu", "Nukualofa", "UTC-13", -211, -1752 },
    /* UTC+14 */
    { "Pacific/Kiritimati", "Kiritimati", "UTC-14", 19, -1574 },
};

#define CLOCK_TZ_NZONES ((int)(sizeof(s_zones) / sizeof(s_zones[0])))

static int s_active_idx = -1; /* -1: custom raw POSIX zone (no city) */

int clock_tz_count(void) { return CLOCK_TZ_NZONES; }

const clock_tz_zone_t *clock_tz_get(int idx) {
    if (idx < 0 || idx >= CLOCK_TZ_NZONES) {
        return NULL;
    }
    return &s_zones[idx];
}

int clock_tz_index_of(const char *name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < CLOCK_TZ_NZONES; i++) {
        if (strcmp(s_zones[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int clock_tz_default_index(void) {
    int idx = clock_tz_index_of(CONFIG_CLOCK_DEFAULT_TIMEZONE);
    if (idx >= 0) {
        return idx;
    }
    /* A custom POSIX string (or a typo) configured: the table's Berlin
     * entry is the fallback default, exactly the pre-2026 behavior. */
    return clock_tz_index_of("Europe/Berlin");
}

int clock_tz_active_index(void) { return s_active_idx; }

bool clock_tz_observer(float *lat_deg, float *lon_deg) {
    const clock_tz_zone_t *z = clock_tz_get(s_active_idx);
    if (!z || !lat_deg || !lon_deg) {
        return false;
    }
    *lat_deg = (float)z->lat_x10 / 10.0f;
    *lon_deg = (float)z->lon_x10 / 10.0f;
    return true;
}

/* The setenv+tzset core, shared by the two apply paths. */
static void tz_setenv(const char *posix) {
    setenv("TZ", posix, 1);
    tzset();
}

esp_err_t clock_tz_apply(int idx) {
    const clock_tz_zone_t *z = clock_tz_get(idx);
    if (!z) {
        ESP_LOGE(TAG, "invalid timezone index %d", idx);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_CLOCK_USE_PCF85063A
    // The PCF holds LOCAL digits: read them, convert to the UTC epoch
    // under the CURRENT (old) zone, switch the zone, re-derive the local
    // digits for the same moment and write them back. mktime must run
    // before the env var changes.
    clock_time_t t;
    esp_err_t ret = clock_get_time(&t);
    if (ret != ESP_OK) {
        return ret;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = t.year - 1900;
    tm.tm_mon = t.month - 1;
    tm.tm_mday = t.day;
    tm.tm_hour = t.hour;
    tm.tm_min = t.minute;
    tm.tm_sec = t.second;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    if (epoch == (time_t)-1) {
        ESP_LOGE(TAG, "epoch conversion failed for the current RTC time");
        return ESP_ERR_INVALID_STATE;
    }
    tz_setenv(z->posix);
    struct tm nt;
    localtime_r(&epoch, &nt);
    clock_time_t nt2 = {nt.tm_year + 1900, nt.tm_mon + 1, nt.tm_mday, nt.tm_hour, nt.tm_min, nt.tm_sec};
    ret = clock_set_time(&nt2);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "rewriting the RTC digits for the new zone failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    // The P4's time base is the system clock: the epoch is untouched, the
    // local digits re-derive from it through the new zone.
    tz_setenv(z->posix);
#endif

    s_active_idx = idx;
    ESP_LOGI(TAG, "timezone applied: %s (%s)", z->name, z->posix);

    // The next fire time (local time-of-day + weekday) depends on the
    // zone: re-arm from the new local time (S3: the PCF ALM; P4: the
    // software one-shot + cache).
    return clock_alarm_scheduler_reload();
}

esp_err_t clock_tz_apply_name(const char *name) {
    int idx = clock_tz_index_of(name);
    if (idx < 0) {
        if (!name || !name[0]) {
            return clock_tz_apply(clock_tz_default_index());
        }
        /* A raw POSIX string configured outside the table (custom
         * CONFIG_CLOCK_DEFAULT_TIMEZONE): apply it as-is; the viewport
         * stays north-up (clock_tz_observer reports no city). */
        tz_setenv(name);
        s_active_idx = -1;
        ESP_LOGI(TAG, "timezone applied: custom POSIX '%s'", name);
        return clock_alarm_scheduler_reload();
    }
    return clock_tz_apply(idx);
}

esp_err_t clock_tz_apply_stored(void) {
    /* Boot-safe variant of apply (clock_sntp_init, during clock_init,
     * after settings_store_load): setenv+tzset ONLY -- the RTC digits
     * already live in the persisted zone's digits (the last apply left
     * them there), so a PCF digit rewrite through an uninitialised old
     * zone would corrupt the wall clock. */
    const char *name = settings_timezone();
    int idx = clock_tz_index_of(name);
    if (idx < 0) {
        name = CONFIG_CLOCK_DEFAULT_TIMEZONE;
        idx = clock_tz_index_of(name);
    }
    if (idx < 0) {
        tz_setenv(name);
        s_active_idx = -1;
        ESP_LOGI(TAG, "timezone from store: custom POSIX '%s' (no observer city)", name);
        return ESP_OK;
    }
    tz_setenv(s_zones[idx].posix);
    s_active_idx = idx;
    ESP_LOGI(TAG, "timezone from store: %s (%s)", s_zones[idx].name, s_zones[idx].posix);
    return ESP_OK;
}
