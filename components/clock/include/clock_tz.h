#pragma once

/**
 * @file clock_tz.h
 * @brief Runtime timezone selection (UTC offset + the Europe/Berlin default)
 *
 * The UI timezone screen offers one roller over every 30-minute UTC offset
 * (UTC-12:00 .. UTC+14:00) plus Europe/Berlin (the Kconfig default, with
 * DST) as the final entry. Applying an option rewrites the POSIX TZ env var
 * (+tzset), which is all the P4's system clock needs (the UTC epoch is
 * untouched, the local digits re-derive). The S3's PCF85063A stores LOCAL
 * digits, so the same moment is re-derived into the new zone's digits and
 * written back before the switch (epoch-consistent). The software/hardware
 * alarm arming is re-armed afterwards (the next fire time depends on the
 * zone). The selection persists via the settings store (timezone_idx); the
 * stored index is applied at SNTP init instead of the Kconfig default.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* The option list: indices 0..52 = the fixed UTC offsets in 30-minute
 * steps from -12:00 (idx 0) to +14:00 (idx 52), index 53 = Europe/Berlin
 * (the Kconfig default, DST-aware). */
#define CLOCK_TZ_FIXED_STEPS 53
#define CLOCK_TZ_BERLIN_IDX (CLOCK_TZ_FIXED_STEPS)
#define CLOCK_TZ_COUNT (CLOCK_TZ_FIXED_STEPS + 1)

/* The fixed offset of option idx (0..52), in minutes EAST of UTC. */
int clock_tz_offset_minutes(int idx);

/* The human name of option idx ("UTC-12:00" .. "UTC+14:00",
 * "Europe/Berlin" for the default). Returns NULL for a bad idx. The
 * string is static storage (valid until the next call). */
const char *clock_tz_option_name(int idx);

/* The POSIX TZ string for option idx (fixed offsets use the sign-flipped
 * "UTC-2" / "UTC+5:30" form; Berlin uses CONFIG_CLOCK_DEFAULT_TIMEZONE).
 * Returns NULL for a bad idx. Static storage. */
const char *clock_tz_string_for_index(int idx);

/**
 * @brief Apply option idx as the active timezone.
 *
 * Epoch-consistent on both boards (see the file comment), then re-arms the
 * alarm arming from the new local time. The caller persists the selection
 * (settings store).
 */
esp_err_t clock_tz_apply(int idx);

#ifdef __cplusplus
}
#endif
