#pragma once

/**
 * @file clock_tz.h
 * @brief Runtime timezone selection (curated named zones, no generic offsets)
 *
 * The UI timezone screen offers one roller over the curated named-zone
 * table below (~60 IANA zones covering every typical civil time: one
 * entry per major city/region, ordered west to east by standard offset).
 * The generic "UTC+02"-style fixed-offset options were dropped
 * 2026-11-17: the moon viewport (the observer-view disk tilt,
 * moon_viewport_rot_deg) is computed from the zone's reference city, so
 * a selectable zone must carry a city — a bare offset has no city, no
 * lat/lon and no DST rule, and would silently mis-tilt the moon and
 * mis-read the clock for half the year in DST countries.
 *
 * Each entry carries: the IANA name (persisted in settings.txt), the
 * roller label, the hand-verified POSIX TZ string (newlib has no
 * /usr/share/zoneinfo — host_sim/tz_check.py compares every string
 * against the system zoneinfo over a multi-year window), and the
 * reference-city lat/lon (x10 int16) for the observer view.
 *
 * Applying an option rewrites the POSIX TZ env var (+tzset), which is
 * all the P4's system clock needs (the UTC epoch is untouched, the local
 * digits re-derive). The S3's PCF85063A stores LOCAL digits, so the same
 * moment is re-derived into the new zone's digits and written back
 * before the switch (epoch-consistent). The software/hardware alarm
 * arming is re-armed afterwards (the next fire time depends on the
 * zone). The selection persists via the settings store (timezone = the
 * IANA name); the stored name is applied at SNTP init instead of the
 * Kconfig default.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* One selectable timezone. lat_x10/lon_x10 are the reference city's
 * coordinates in degrees x10 (north/east positive). */
typedef struct {
    const char *name;  /* IANA canonical name — persisted in settings.txt */
    const char *label; /* short roller label (the city) */
    const char *posix; /* verified POSIX TZ string for setenv/TZ */
    int16_t lat_x10;   /* reference-city latitude  x10 (N +) */
    int16_t lon_x10;   /* reference-city longitude x10 (E +) */
} clock_tz_zone_t;

/* Number of selectable zones (the table size, clock_tz.c). */
int clock_tz_count(void);

/* The zone at idx, or NULL for a bad idx. Static storage. */
const clock_tz_zone_t *clock_tz_get(int idx);

/* Index of the zone with this IANA name, or -1. Empty/NULL -> -1. */
int clock_tz_index_of(const char *name);

/* The index of the DEFAULT zone: the zone whose IANA name matches
 * CONFIG_CLOCK_DEFAULT_TIMEZONE (a name outside the table falls back to
 * index 0). */
int clock_tz_default_index(void);

/* The index currently applied (clock_tz_apply / clock_tz_apply_stored
 * set it; starts at the default). */
int clock_tz_active_index(void);

/* The reference city of the ACTIVE zone (for the moon observer view).
 * false = unknown coordinates (a custom POSIX zone configured outside
 * the table) — the caller keeps the north-up viewport then. */
bool clock_tz_observer(float *lat_deg, float *lon_deg);

/**
 * @brief Apply option idx as the active timezone.
 *
 * Epoch-consistent on both boards (see the file comment), then re-arms
 * the alarm arming from the new local time. The caller persists the
 * selection (settings store).
 */
esp_err_t clock_tz_apply(int idx);

/**
 * @brief Apply the zone by IANA name. Unknown names (a custom
 * CONFIG_CLOCK_DEFAULT_TIMEZONE, a stale settings.txt) fall back to the
 * default zone. Empty/NULL applies the default.
 */
esp_err_t clock_tz_apply_name(const char *name);

/**
 * @brief Apply the persisted selection at boot (clock_sntp init): the
 * settings-store zone name (loaded from settings.txt by the caller).
 * setenv+tzset only -- the RTC digits already carry the persisted
 * zone, so no digit rewrite is attempted through an uninitialised
 * old zone.
 */
esp_err_t clock_tz_apply_stored(void);

#ifdef __cplusplus
}
#endif
