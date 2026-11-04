/* UI simulator: stand-ins for the device externs screens.c uses
 * (clock/settings/wifi/vars/ui). Deterministic canned state so every
 * screen renders in its populated "at rest" state:
 *
 *   clock 12:35, seconds 37
 *   fixed alarm rule 12:35 Mon-Fri (0x3E)   -- the range-arc default
 *   alarm prefix "Mo"                        -- the next fire is > 24 h away
 *   pomodoro interval 45 min (0:45)
 *   idle timeout 60 s, deep-sleep countdown 60 s, dusk anim on
 *   wifi scan complete, 5 canned APs
 *
 * moon_ui_create_image bakes the REAL current-phase moon (the portable
 * core + generated assets, dark_zero = 1 like the device static/wake
 * frame -- the computed backlit night side) into one shared 400x400
 * RGB565 buffer, exactly where the device's moon image sits (33,33 on
 * the 466x466 panel).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock_alarm_scheduler.h"
#include "clock_idle.h"
#include "clock_types.h"
#include "esp_err.h"
#include "lvgl.h"
#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"
#include "moon_ui.h"
#include "screens.h"
#include "settings_store.h"
#include "ui.h"
#include "vars.h"
#include "wifi.h"

/* ---- esp_err (device idf header replaced) ---- */

const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

/* ---- global clock vars (vars.c) ----

   The REAL vars.c COPIES each value into a static buffer (snprintf /
   strncpy); the setter is handed a caller stack buffer that is valid only
   for the call. The stub mirrors that contract (copy, not pointer-store) --
   a pointer-storing stub dangles the moment the caller returns, and the
   shared clock widget's attach refresh (ui_clock_attach_to) sets the alarm
   label from a stack buffer then ticks the widget, so a dangling pointer
   would read as garbage (the tofu boxes). */

static int32_t s_clock_seconds = 37;
static char s_clock_label[6] = "12:35";
static char s_alarm_label[6] = "12:35"; /* the fixed rule's time */

int32_t get_var_clock_seconds() { return s_clock_seconds; }
void set_var_clock_seconds(int32_t value) { s_clock_seconds = value; }
const char *get_var_clock_label() { return s_clock_label; }
void set_var_clock_label(const char *value) {
    snprintf(s_clock_label, sizeof s_clock_label, "%s", value);
    s_clock_label[sizeof s_clock_label - 1] = 0;
}
const char *get_var_alarm_label() { return s_alarm_label; }
void set_var_alarm_label(const char *value) {
    snprintf(s_alarm_label, sizeof s_alarm_label, "%s", value);
    s_alarm_label[sizeof s_alarm_label - 1] = 0;
}

/* The weekday prefix ("" within 24 h of the fire, else the 2-letter
 * day of the fire). Canned: "Mo" (the fixed rule's next fire is
 * Monday, > 24 h out on the sim's Saturday 12:35). */
static char s_alarm_prefix[4] = "Mo";
const char *get_var_alarm_prefix() { return s_alarm_prefix; }
void set_var_alarm_prefix(const char *value) {
    strncpy(s_alarm_prefix, value, sizeof s_alarm_prefix - 1);
    s_alarm_prefix[sizeof s_alarm_prefix - 1] = 0;
}

/* ---- clock (clock.c / clock_idle.c / clock_sntp.c) ---- */

bool clock_is_idle(void) { return false; } /* awake at rest */
void clock_idle_set_timeout_ms(uint64_t ms) { (void)ms; }
/* The snapshots are the synced state (post-NTP): the clock label renders at
   full opacity. The M5.1 unsynced dim (screens.c) is reachable only on the
   P4 before its first sync / manual time entry. */
bool clock_sntp_time_trusted(void) { return true; }

/* The manual time-entry screen (screens.c) reads + writes the wall clock.
   The host sim has no clock: a fixed plausible value to seed the rollers,
   a no-op set, and the trust flag toggle (no-op; the snapshot above
   reports trusted already). */
static clock_time_t s_sim_time = {2026, 11, 1, 12, 35, 7};
esp_err_t clock_sntp_get_system_time(clock_time_t *t) {
    *t = s_sim_time;
    return ESP_OK;
}
esp_err_t clock_set_time(const clock_time_t *t) {
    if (t)
        s_sim_time = *t;
    printf("[stub] set_time %04d-%02d-%02d %02d:%02d:%02d\n", t ? t->year : 0, t ? t->month : 0, t ? t->day : 0,
           t ? t->hour : 0, t ? t->minute : 0, t ? t->second : 0);
    return ESP_OK;
}
void clock_sntp_set_time_trusted(bool trusted) { (void)trusted; }

/* The shared clock widget's attach refresh (screens.c ui_clock_attach_to)
   reads the wall clock directly. */
esp_err_t clock_get_time(clock_time_t *out) {
    if (!out)
        return ESP_FAIL;
    *out = s_sim_time;
    return ESP_OK;
}

/* ---- alarm scheduler (canned rule state) ---- */

static int s_pomo_minutes = 45; /* -1 = none */
static int s_alarm_h = 12, s_alarm_m = 35;
static uint8_t s_alarm_mask = 0x3E; /* Mon-Fri */

uint8_t clock_alarm_rule_types(void) {
    uint8_t t = 0;
    if (s_pomo_minutes > 0)
        t |= 0x01;
    if (s_alarm_mask != 0x00)
        t |= 0x02;
    return t;
}

int clock_alarm_get_interval_rule(void) { return s_pomo_minutes; }

esp_err_t clock_alarm_get_fixed_rule(int *hour, int *minute, uint8_t *dow_mask) {
    *hour = s_alarm_h;
    *minute = s_alarm_m;
    *dow_mask = s_alarm_mask;
    return s_alarm_mask ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t clock_alarm_set_pomodoro_interval(int minutes_total) {
    s_pomo_minutes = minutes_total;
    printf("[stub] set_pomodoro_interval %d\n", minutes_total);
    return ESP_OK;
}

esp_err_t clock_alarm_set_clock_alarm(int hour, int minute, uint8_t dow_mask) {
    s_alarm_h = hour;
    s_alarm_m = minute;
    s_alarm_mask = dow_mask;
    printf("[stub] set_clock_alarm %02d:%02d mask 0x%02X\n", hour, minute, dow_mask);
    return ESP_OK;
}

esp_err_t clock_alarm_deactivate_all(void) {
    s_pomo_minutes = -1;
    s_alarm_mask = 0;
    printf("[stub] deactivate_all\n");
    return ESP_OK;
}

/* The shared clock widget's attach refresh (screens.c) calls the real
   actions.c alarm-display update (scheduler cache -> the label + the
   weekday-prefix vars). The stub replays it from the canned rule: a
   scheduled fire reads its HH:MM + the canned "Mo" weekday prefix (the
   Mon-Fri 12:35 rule, >24 h out on the sim clock), none reads "--:--" +
   an empty prefix. */
void action_update_alarm_display(void *e) {
    (void)e;
    char label[6];
    if (s_alarm_mask) {
        snprintf(label, sizeof(label), "%02d:%02d", s_alarm_h, s_alarm_m);
        set_var_alarm_label(label);
        set_var_alarm_prefix("Mo");
    } else {
        snprintf(label, sizeof(label), "--:--");
        set_var_alarm_label(label);
        set_var_alarm_prefix("");
    }
}

/* ---- runtime settings store (canned values) ---- */

static uint32_t s_idle_sec = 60, s_countdown_sec = 60;
static bool s_dusk_on = true;

uint32_t settings_idle_timeout_sec(void) { return s_idle_sec; }
void settings_set_idle_timeout_sec(uint32_t sec) {
    s_idle_sec = sec;
    printf("[stub] idle timeout %u s\n", sec);
}

uint32_t settings_deep_sleep_countdown_sec(void) { return s_countdown_sec; }
void settings_set_deep_sleep_countdown_sec(uint32_t sec) {
    s_countdown_sec = sec;
    printf("[stub] deep-sleep countdown %u s\n", sec);
}

bool settings_boot_anim_enable(void) { return true; }
void settings_set_boot_anim_enable(bool on) { (void)on; }

bool settings_dusk_anim_enable(void) { return s_dusk_on; }
void settings_set_dusk_anim_enable(bool on) {
    s_dusk_on = on;
    printf("[stub] dusk anim %s\n", on ? "on" : "off");
}

esp_err_t settings_store_save(void) { return ESP_OK; }

/* ---- timezone selection (device: components/clock/clock_tz.c) -------
   The option list mirrors the device: 53 fixed 30-min UTC offsets
   (-12:00..+14:00) + Europe/Berlin (idx 53). The sim holds the stored
   index so the screen seeds + persists it like the device. */

#define STUB_TZ_FIXED 53
static int s_tz_idx = STUB_TZ_FIXED; /* the device default (Europe/Berlin) */

int clock_tz_offset_minutes(int idx) { return (idx < 0 || idx >= STUB_TZ_FIXED) ? -1 : -720 + idx * 30; }

const char *clock_tz_option_name(int idx) {
    static char buf[16];
    if (idx < 0 || idx >= STUB_TZ_FIXED + 1)
        return NULL;
    if (idx == STUB_TZ_FIXED)
        return "Europe/Berlin";
    int off = clock_tz_offset_minutes(idx);
    int am = (off < 0) ? -off : off;
    snprintf(buf, sizeof buf, "UTC%c%02d:%02d", off < 0 ? '-' : '+', am / 60, am % 60);
    return buf;
}

esp_err_t clock_tz_apply(int idx) {
    if (idx < 0 || idx >= STUB_TZ_FIXED + 1)
        return ESP_FAIL;
    s_tz_idx = idx;
    printf("[stub] tz apply %s (idx %d)\n", clock_tz_option_name(idx), idx);
    return ESP_OK;
}

int settings_timezone_idx(void) { return s_tz_idx; }
void settings_set_timezone_idx(int idx) {
    if (idx < 0 || idx >= STUB_TZ_FIXED + 1)
        return;
    s_tz_idx = idx;
}

/* ---- alarm sound selection (device: components/ui/alarm_audio.c +
   settings_store) ----
   The option names mirror the device's ALARM_NAMES; the sim holds the
   stored id so the screen seeds + persists it like the device. The
   preview "plays" a stub line (no audio in the sim). */

#define STUB_ALARM_SOUNDS 4
static const char *STUB_ALARM_NAMES[STUB_ALARM_SOUNDS] = {"Two-Tone", "Beep", "Bell", "Arpeggio"};
static int s_alarm_sound = 0; /* the device default (two-tone) */

int settings_alarm_sound_id(void) { return s_alarm_sound; }
void settings_set_alarm_sound_id(int id) {
    if (id < 0 || id >= STUB_ALARM_SOUNDS)
        return;
    s_alarm_sound = id;
}

int alarm_audio_count(void) { return STUB_ALARM_SOUNDS; }

const char *alarm_audio_name(int id) {
    if (id < 0 || id >= STUB_ALARM_SOUNDS)
        return NULL;
    return STUB_ALARM_NAMES[id];
}

void alarm_audio_select(int id) { settings_set_alarm_sound_id(id); }

int alarm_audio_selected(void) { return s_alarm_sound; }

void alarm_audio_preview(int id) {
    if (id < 0 || id >= STUB_ALARM_SOUNDS)
        return;
    printf("[stub] preview sound %d (%s)\n", id, alarm_audio_name(id));
}

/* ---- wifi scan (canned AP list) ---- */

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth;
} ap_stub_t;

static const ap_stub_t s_aps[] = {
    {"MOONHOME-5G", -38, 3},  {"MOONHOME-2G", -44, 3},  {"FIBERLINK-88", -52, 3},
    {"CAFE_HOTSPOT", -61, 0}, {"OFFICE-GUEST", -73, 3},
};

/* The sim shows the home screen as connected (white indicator). */
bool wifi_manager_is_connected(void) { return true; }

void wifi_scan_begin(void) { printf("[stub] wifi_scan_begin\n"); }
bool wifi_scan_in_progress(void) { return false; }
int wifi_scan_get_results(wifi_ap_info_t *out, int max) {
    int n = (int)(sizeof s_aps / sizeof s_aps[0]);
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++) {
        snprintf(out[i].ssid, sizeof out[i].ssid, "%s", s_aps[i].ssid);
        out[i].rssi = s_aps[i].rssi;
        out[i].auth = s_aps[i].auth;
    }
    return n;
}

/* The wheel keyboard's save path: the stub keeps the password out of
 * the log (lengths only) and plays the device's screen switch. */
void loadScreen(enum ScreensEnum screenId);
void ui_tick(void);

esp_err_t wifi_credentials_write(const char *ssid, const char *password) {
    printf("[stub] credentials write (ssid %zu chars, pw %zu chars)\n", strlen(ssid), strlen(password));
    return ESP_OK;
}

void action_start_wifi_sync(void) {
    printf("[stub] wifi sync started (transient WiFi screen)\n");
    loadScreen(SCREEN_ID_WIFI);
    ui_tick();
}

/* ---- screen switching / tick (ui.c) ---- */

static int s_cur_screen = -1;

void loadScreen(enum ScreensEnum screenId) {
    s_cur_screen = screenId - 1;
    lv_obj_t *screen = ((lv_obj_t **)&objects)[s_cur_screen];
    lv_scr_load(screen);
    /* The device's loadScreen (ui.c) reparents the ONE shared clock widget
       onto the new moon screen and refreshes the clock vars from the RTC
       before the atomic flush (ui_clock_attach_to); the stub mirrors it so
       the snapshots exercise the shared-widget path. */
    ui_clock_attach_to(screenId);
}

enum ScreensEnum ui_current_screen(void) { return (enum ScreensEnum)(s_cur_screen + 1); }

/* Drive one screen tick (the device's ui_tick dispatches the same
 * tick_screen_funcs table; here only the loaded screen is ticked). */
void ui_tick(void) {
    if (s_cur_screen >= 0)
        tick_screen_by_id((enum ScreensEnum)(s_cur_screen + 1));
}

/* ---- moon image (moon_ui.c): bake the real current phase once ---- */

/* The moon geometry, mirroring the device's board-conditional block in
 * moon_ui.c: 400/33/200 on the S3 (PANEL_SIZE 466), 650/35/325 on the P4
 * (PANEL_SIZE 720). The disk radius is MOON_PX/2 -- the asset's native
 * MOON_DISK_RADIUS_PX (200) on the S3 and 325 = 650/2 on the P4. */
#ifndef PANEL_SIZE
#define PANEL_SIZE 466
#endif
#if PANEL_SIZE == 720
#define MOON_PX 650
#else
#define MOON_PX 400
#endif
#define MOON_IMG_POS ((PANEL_SIZE - MOON_PX) / 2)
#define MOON_R ((float)MOON_PX / 2.0f)

static uint8_t s_gray[MOON_PX * MOON_PX];
static uint16_t s_rgb[MOON_PX * MOON_PX];
static lv_image_dsc_t s_dsc;

lv_obj_t *moon_ui_create_image(lv_obj_t *parent) {
    if (getenv("UISIM_BLACK_MOON")) {
        /* black background -- the wobble A/B needs the digit ink isolated
         * from the lunar surface (temp toggle, driven by env) */
        s_dsc = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                            .cf = LV_COLOR_FORMAT_RGB565,
                                            .w = MOON_PX,
                                            .h = MOON_PX,
                                            .stride = MOON_PX * sizeof(uint16_t)},
                                 .data_size = sizeof s_rgb,
                                 .data = (uint8_t *)s_rgb}; /* zeroed = black */
        printf("[stub] black moon (wobble A/B)\n");
    } else if (!s_dsc.data) {
        static const int64_t utc_epoch = 1798500000; /* 2026-10-28 12:00:00 UTC */
        moon_geom_t g;
        moon_geometry(utc_epoch, &g);
        moon_bake(&g, moon_albedo(), moon_relief(), s_gray, MOON_PX, MOON_R, MOON_PX / 2.0f, MOON_PX / 2.0f, MOON_ALB_W,
                  MOON_ALB_H, 1, 0.0f);
        for (int i = 0; i < MOON_PX * MOON_PX; i++)
            s_rgb[i] = moon_gray565(s_gray[i]);
        s_dsc = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC,
                       .cf = LV_COLOR_FORMAT_RGB565,
                       .w = MOON_PX,
                       .h = MOON_PX,
                       .stride = MOON_PX * sizeof(uint16_t)},
            .data_size = sizeof s_rgb,
            .data = (uint8_t *)s_rgb,
        };
        printf("[stub] moon baked: illum %.2f phase %.1f deg\n", g.illum, g.phase_angle_deg);
    }
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_pos(img, MOON_IMG_POS, MOON_IMG_POS);
    lv_obj_set_size(img, MOON_PX, MOON_PX);
    lv_image_set_src(img, &s_dsc);
    return img;
}

void moon_ui_refresh(void) {}
void moon_ui_init(void) {}
void moon_ui_bake_now(int64_t utc_epoch) { (void)utc_epoch; }
void moon_ui_day_tick(void) {} /* the manual time-entry commit re-bakes (device) */
void moon_ui_start(void) {}
bool moon_ui_sweeping(void) { return false; }
void moon_ui_fade_out(void) {}
bool moon_ui_screen_is_moon(enum ScreensEnum id) {
    return id == SCREEN_ID_MODEM_SLEEP || id == SCREEN_ID_WIFI || id == SCREEN_ID_LIGHT_SLEEP ||
           id == SCREEN_ID_DEEP_SLEEP;
}
void moon_ui_set_panel_step(int step) { (void)step; }
void moon_ui_rebuild_ui_mask(void) {}
