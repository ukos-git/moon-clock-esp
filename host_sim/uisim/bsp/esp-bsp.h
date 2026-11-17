/* Stand-in for the vendored BSP header (like esp_err.h / esp_log.h):
 * clock.h includes the hardware chain for consumer convenience but
 * declares no BSP types; the sim's clock_tz.c link needs the include
 * to merely resolve. */
#pragma once
