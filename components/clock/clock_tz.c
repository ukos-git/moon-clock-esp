/**
 * @file clock_tz.c
 * @brief Runtime timezone selection (see clock_tz.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_alarm_scheduler.h"
#include "clock_tz.h"

static const char *TAG = "clock_tz";

int clock_tz_offset_minutes(int idx) {
    if (idx < 0 || idx >= CLOCK_TZ_FIXED_STEPS) {
        return -1;
    }
    /* idx 0 = -12:00, 30-minute steps. */
    return -720 + idx * 30;
}

const char *clock_tz_option_name(int idx) {
    static char buf[16];
    if (idx < 0 || idx >= CLOCK_TZ_COUNT) {
        return NULL;
    }
    if (idx == CLOCK_TZ_BERLIN_IDX) {
        return "Europe/Berlin";
    }
    int off = clock_tz_offset_minutes(idx); /* -720 .. +840 */
    int am = (off < 0) ? -off : off;
    snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", off < 0 ? '-' : '+', am / 60, am % 60);
    return buf;
}

const char *clock_tz_string_for_index(int idx) {
    if (idx < 0 || idx >= CLOCK_TZ_COUNT) {
        return NULL;
    }
    if (idx == CLOCK_TZ_BERLIN_IDX) {
        return CONFIG_CLOCK_DEFAULT_TIMEZONE;
    }
    static char buf[24];
    int off = clock_tz_offset_minutes(idx); /* -720 .. +840 */
    int am = (off < 0) ? -off : off;
    /* The POSIX TZ sign is the WEST offset (what to add to local to get
     * UTC) -- flipped relative to the east-of-UTC `off` here: the Kconfig
     * default proves it (CET is UTC+1, spelled "CET-1"). So UTC+02:00 is
     * "UTC-2" and UTC-05:30 is "UTC+5:30". */
    if (am % 60) {
        snprintf(buf, sizeof(buf), "UTC%c%d:%d", off > 0 ? '-' : '+', am / 60, am % 60);
    } else {
        snprintf(buf, sizeof(buf), "UTC%c%d", off > 0 ? '-' : '+', am / 60);
    }
    return buf;
}

esp_err_t clock_tz_apply(int idx) {
    const char *tz = clock_tz_string_for_index(idx);
    if (!tz) {
        ESP_LOGE(TAG, "invalid timezone index %d", idx);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_CLOCK_USE_PCF85063A
    // The PCF holds LOCAL digits: read them, convert to the UTC epoch under
    // the CURRENT (old) zone, switch the zone, re-derive the local digits
    // for the same moment and write them back. mktime must run before the
    // env var changes.
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
    setenv("TZ", tz, 1);
    tzset();
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
    setenv("TZ", tz, 1);
    tzset();
#endif

    ESP_LOGI(TAG, "timezone applied: %s (%s)", clock_tz_option_name(idx), tz);

    // The next fire time (local time-of-day + weekday) depends on the zone:
    // re-arm from the new local time (S3: the PCF ALM; P4: the software
    // one-shot + cache).
    return clock_alarm_scheduler_reload();
}
