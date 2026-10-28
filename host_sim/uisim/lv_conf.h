/* Host UI simulator LVGL config: mirrors the device's feature set
 * (from sdkconfig CONFIG_LV_*) with the only deliberate deviation:
 * LV_USE_SNAPSHOT on (the device build keeps it off -- the snapshot
 * code is compiled out there, so this cannot affect the firmware).
 *
 * Everything left undefined falls back to LVGL's internal defaults
 * (lv_conf_internal.h), which match the device's Kconfig defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H 1

/* Display: the panel is RGB565, 466x466. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_DPI_DEF 130

/* Software renderer (the device has no GPU). */
#define LV_USE_DRAW_SW 1

/* stdlib: the device's sdkconfig is LV_USE_STDLIB_BUILTIN (string/sprintf)
 * + clib malloc. LVGL 9.6 names: LV_STDLIB_* values are defined by
 * lv_conf_internal.h before the user conf is included. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

/* Layout + widgets the settings screens use. */
#define LV_USE_FLEX 1
#define LV_USE_ARC 1
#define LV_USE_ROLLER 1
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1

/* Theme + fonts: the default theme is applied by create_screens();
 * the default font is montserrat 14 (the device's LV_FONT_DEFAULT). */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_FONT_MONTSERRAT_14 1
#define LV_USE_FONT_PLACEHOLDER 1

/* Offscreen rendering: snapshot every screen to a PNG. */
#define LV_USE_SNAPSHOT 1

/* Quiet: no LVGL log, no perf monitor, no observers. */
#define LV_USE_LOG 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_OS 0

#endif /* LV_CONF_H */
