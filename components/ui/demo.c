/* ---- demo mode (UI_DEMO_MODE): the full scripted tour ----------------
 *
 * A camera-ready self-driving tour of every screen and function, so the
 * bench can record the S3 and the P4 side by side and compare them
 * frame for frame (docs/verification/demo-mode). Kconfig:
 * UI_DEMO_MODE (+ UI_DEMO_FULL_TOUR, UI_DEMO_SCREEN_SEC,
 * UI_DEMO_DUSK_WAIT_S). Off by default; the stubs at the bottom keep
 * the hooks (action_demo_active, ui_demo_init) compiling in production.
 *
 * What one cycle walks (all taps go through the REAL touch path -- a
 * second LVGL POINTER indev fed by this file, so hit-testing, the
 * sticky-sector wheel reveal and every CLICKED handler execute for
 * real; the real touch stays live for the human):
 *
 *   MOONS      the five moon screens on a rotation, MM:SS odometer
 *   HUB_WALK   home tap -> hub; every ring icon -> sub-screen dwell ->
 *              exit; the alarm screen's pomodoro + real-alarm entries
 *   WIFI       scan, first AP row -> wheel keyboard
 *   WHEEL      letters, all four registers, centre backspaces, DISCARD
 *              x2 (clear, then exit) -- the COMMIT key is NEVER tapped
 *              (it would write wifi_credentials.txt and start a connect)
 *   BACK       wifi exit -> hub exit -> home
 *   DUSK       light sleep + the pole spin to the dark side, wake
 *   ROLL       the synthetic tilt script through the real roll pipeline
 *   SYNC       one bounded on-demand WiFi + NTP sync
 *   ALARM      LAST in the cycle: the real ring script (fire, stop,
 *              re-fire = codec re-open, dismiss); scheduled alarms stay
 *              suppressed. Last on purpose: the 2026-11-08 take showed
 *              the S3 QSPI completion-loss race firing reliably ~29 s
 *              into this phase (panel watchdog -> controlled reboot);
 *              running it at the cycle end keeps dusk/roll/sync
 *              comparable on every cycle even when that recovery runs.
 *
 * Suppressed while the tour runs (actions.c guards): the deep-sleep
 * transition, scheduled alarm fires, the hourly sync and the sync
 * hand-off. The idle monitor is switched off at ARM and the timer is
 * held every second, so the tour never falls asleep mid-take.
 *
 * Every phase entry logs `phase=<name> cycle=<n> screen=<id>` (tag
 * DEMO) -- the recording harness aligns the video to the logs with it.
 *
 * The tap points: a widget's AABB centre is rotation-safe (the screen
 * roots rotate in exact 90 deg steps about the panel centre, so the
 * AABB centre stays the rotated centre). The wheel's points are local
 * to the wheel screen (screens.c exposes them) and are mapped into
 * display coords through the screen root's own transform matrix.
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "lvgl.h"

#include "actions.h"
#include "alarm_audio.h"
#include "clock.h"
#include "clock_idle.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"
#include "wifi.h"

#ifdef CONFIG_UI_DEMO_MODE

static const char *TAG = "DEMO";

/* Kconfig n leaves macros UNDEFINED (hard invariant): seed the runtime
 * reads, never the macros. */
#ifndef CONFIG_UI_DEMO_SCREEN_SEC
#define CONFIG_UI_DEMO_SCREEN_SEC 15
#endif
#ifndef CONFIG_UI_DEMO_DUSK_WAIT_S
#define CONFIG_UI_DEMO_DUSK_WAIT_S 180
#endif

#define DEMO_MS_PER_S 1000u
#define TAP_PRESS_MS 90u  /* < 400 ms long-press gate -> SHORT_CLICKED */
#define TAP_TOTAL_MS 150u /* press 90 + release 60 (indev reads every 30 ms) */
#define TAP_SETTLE_MS                                                                                                  \
    900u /* after a release: let the CLICKED handler land                                                              \
          * AND the fade-to-black switch complete                                                                      \
          * (2 x UI_SCREEN_SWITCH_FADE + one hidden                                                                    \
          * flush ~= 600 ms) -- verifying earlier read                                                                 \
          * the PREVIOUS screen and fired a spurious                                                                   \
          * loadScreen fallback per step (observed on                                                                  \
          * the P4 hub walk, 2026-11-08 take). */

/* ---------------- virtual pointer (the second POINTER indev) ---------
 *
 * A separate LVGL indev, not a wrapper of the BSP one: the real CST9217
 * /GT911 indev (the FIRST ever created -- the alarm/orientation touch
 * registrations bound to it before ui_demo_init runs) keeps working for
 * the human. lv_indev_active() during a vt dispatch is the vt indev, so
 * the wheel handlers' raw-point reads see the injected press. */
static lv_indev_t *s_vt;
static lv_point_t s_vt_pt;
static bool s_vt_pressed;
static uint32_t s_tap_ms; /* tap countdown; 0 = no tap in flight */

static void vt_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = s_vt_pt.x;
    data->point.y = s_vt_pt.y;
    data->state = s_vt_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void vt_tap(const lv_point_t *pt) {
    s_vt_pt = *pt;
    s_vt_pressed = true;
    s_tap_ms = TAP_TOTAL_MS;
}

static void vt_step_10ms(void) {
    if (!s_tap_ms)
        return;
    s_tap_ms -= 10;
    if (s_vt_pressed && s_tap_ms <= TAP_TOTAL_MS - TAP_PRESS_MS)
        s_vt_pressed = false;
}

static bool vt_idle(void) { return s_tap_ms == 0; }

/* Tap the centre of a widget in display coords. Rotation-safe: the
 * screen roots rotate by multiples of 90 deg about the panel centre,
 * so the AABB centre of any widget equals its rotated centre. */
static void tap_obj(const lv_obj_t *obj) {
    if (!obj)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_point_t p = {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)((a.y1 + a.y2) / 2)};
    vt_tap(&p);
}

/* Tap a wheel point (local wheel-screen space from screens.c) through
 * the wheel screen root's own transform into display coords. */
static bool tap_wheel_point(lv_point_t local) {
    lv_obj_transform_point(objects.settings_wifi_password, &local, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    vt_tap(&local);
    return true;
}

/* ---------------- phase engine --------------------------------------- */

enum phase {
    PH_ARM,       /* wait out the boot sweep, then take over */
    PH_MOONS,     /* the five moon screens on a rotation */
    PH_HUB_WALK,  /* scripted tap list: home -> hub -> every icon -> exits */
    PH_WIFI,      /* hub wifi icon -> settings wifi -> scan -> AP row */
    PH_WHEEL,     /* the keyboard drive (discard only, never commit) */
    PH_BACK,      /* wifi exit -> hub exit -> home */
    PH_ALARM,     /* the real ring script */
    PH_HOME,      /* drain the post-dismiss sync, land on home */
    PH_DUSK,      /* light sleep + dusk spin */
    PH_DUSK_DARK, /* hold the dark side, then wake */
    PH_ROLL,      /* the synthetic tilt script */
    PH_SYNC,      /* one bounded on-demand sync */
    PH_N
};

static const char *const s_phase_names[PH_N] = {"arm",   "moons", "hub_walk", "wifi", "wheel", "back",
                                                "alarm", "home",  "dusk",     "dark", "roll",  "sync"};

static enum phase s_phase = PH_ARM;
static bool s_active = false; /* from ARM to the end of the first cycle, then always */
static uint32_t s_cycle = 0;
static uint32_t s_phase_t0; /* lv_tick at the phase entry */
static uint32_t s_sub;      /* substep inside the phase */
static uint32_t s_sub_t0;   /* lv_tick at the substep entry */
static uint32_t s_sec_t0;   /* the per-second odometer beat */

static uint32_t s_settle_t0; /* TAP_SETTLE_MS stamp after a tap landed */

bool action_demo_active(void) { return s_active; }

static uint32_t now_ms(void) { return lv_tick_get(); }

static bool due(uint32_t t0_ms, uint32_t after_ms) { return (uint32_t)(now_ms() - t0_ms) >= after_ms; }

static void phase_enter(enum phase p) {
    s_phase = p;
    s_sub = 0;
    s_phase_t0 = now_ms();
    s_sub_t0 = s_phase_t0;
    ESP_LOGI(TAG, "phase=%s cycle=%u screen=%d", s_phase_names[p], (unsigned)s_cycle, (int)ui_current_screen());
}

static void phase_timeout(const char *what, uint32_t budget_ms) {
    ESP_LOGW(TAG, "phase=%s cycle=%u: %s timed out after %u ms -- moving on", s_phase_names[s_phase], (unsigned)s_cycle,
             what, (unsigned)budget_ms);
}

/* Best-effort screen landing: the taps drive the real handlers, so the
 * screen should already be right; a mismatch (a dropped tap, a failed
 * hit-test) falls back to the direct load so the tour never wedges. */
static void ensure_screen(enum ScreensEnum want) {
    if (ui_current_screen() != want) {
        ESP_LOGW(TAG, "expected screen %d, on %d -- loadScreen fallback", (int)want, (int)ui_current_screen());
        loadScreen(want);
        ui_tick();
        settings_screen_refresh_for(want);
    }
}

/* Wait until the tap in flight has landed and its CLICKED handler has
 * had TAP_SETTLE_MS; true when settled. (Call after issuing the tap.) */
static bool tap_settled(void) {
    if (!vt_idle()) {
        s_settle_t0 = 0; /* (re)arm the settle stamp when the tap completes */
        return false;
    }
    if (!s_settle_t0) {
        s_settle_t0 = now_ms();
        return false;
    }
    return due(s_settle_t0, TAP_SETTLE_MS);
}

/* ---------------- the hub walk (generic tap-step list) ---------------- */

struct wstep {
    const lv_obj_t *obj; /* tap target (NULL = verify/dwell only) */
    enum ScreensEnum want;
    uint32_t dwell_ms;
};

#define WALK_MAX 24
static struct wstep s_walk[WALK_MAX];
static int s_walk_n, s_walk_i;

static const lv_obj_t *sub_exit_of(enum ScreensEnum screen) {
    lv_obj_t *root = NULL;
    switch (screen) {
    case SCREEN_ID_SETTINGS_ALARM:
        root = objects.settings_alarm;
        break;
    case SCREEN_ID_SETTINGS_POMODORO:
        root = objects.settings_pomodoro;
        break;
    case SCREEN_ID_SETTINGS_ALARM_TIME:
        root = objects.settings_alarm_time;
        break;
    case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
        root = objects.settings_light_sleep;
        break;
    case SCREEN_ID_SETTINGS_DEEP_SLEEP:
        root = objects.settings_deep_sleep;
        break;
    case SCREEN_ID_SETTINGS_WIFI:
        root = objects.settings_wifi;
        break;
    case SCREEN_ID_SETTINGS_TIME:
        root = objects.settings_time;
        break;
    case SCREEN_ID_SETTINGS_TZ:
        root = objects.settings_tz;
        break;
    case SCREEN_ID_SETTINGS_ALARM_SOUND:
        root = objects.settings_alarm_sound;
        break;
    default:
        return NULL;
    }
    return screens_sub_exit_get(root);
}

static void walk_push(const lv_obj_t *obj, enum ScreensEnum want, uint32_t dwell_ms) {
    if (s_walk_n < WALK_MAX) {
        s_walk[s_walk_n++] = (struct wstep){obj, want, dwell_ms};
    }
}

static void hub_walk_build(void) {
    s_walk_n = 0;
    const lv_obj_t *icons[8];
    enum ScreensEnum targets[8];
    int n = screens_hub_icons(icons, targets, 8);
    const uint32_t sec = (uint32_t)CONFIG_UI_DEMO_SCREEN_SEC * DEMO_MS_PER_S;
    for (int i = 0; i < n; i++) {
        walk_push(icons[i], targets[i], sec);
        if (targets[i] == SCREEN_ID_SETTINGS_ALARM) {
            /* The alarm screen's two entries (they open their own
             * sub-screens; the deactivate-all master is NOT tapped --
             * that would really disarm the user's alarms). */
            walk_push(objects.settings_alarm_pomodoro, SCREEN_ID_SETTINGS_POMODORO, sec);
            walk_push(sub_exit_of(SCREEN_ID_SETTINGS_POMODORO), SCREEN_ID_SETTINGS_ALARM, 0);
            walk_push(objects.settings_alarm_clock, SCREEN_ID_SETTINGS_ALARM_TIME, sec);
            walk_push(sub_exit_of(SCREEN_ID_SETTINGS_ALARM_TIME), SCREEN_ID_SETTINGS_ALARM, 0);
        }
        walk_push(sub_exit_of(targets[i]), SCREEN_ID_SETTINGS_HUB, 0);
    }
    s_walk_i = 0;
    ESP_LOGI(TAG, "hub walk: %d tap steps", s_walk_n);
}

/* ---------------- the wheel keyboard drive ---------------------------- */

/* kind: 0 = dwell only, 1 = mode key (arg = register), 2 = letter,
 * 3 = centre (backspace), 4 = discard. Never commit. */
struct wtap {
    int kind;
    int arg;
    uint32_t dwell_ms;
};

#define WTAP_MAX 24
static struct wtap s_wtap[WTAP_MAX];
static int s_wtap_n, s_wtap_i;

static void wtap_push(int kind, int arg, uint32_t dwell_ms) {
    if (s_wtap_n < WTAP_MAX) {
        s_wtap[s_wtap_n++] = (struct wtap){kind, arg, dwell_ms};
    }
}

static void wheel_drive_build(void) {
    s_wtap_n = 0;
    /* Lowercase register (the open state): type "demo". */
    wtap_push(2, 'd', 250);
    wtap_push(2, 'e', 250);
    wtap_push(2, 'm', 250);
    wtap_push(2, 'o', 250);
    /* Register tour: ABC + A, 123 + 1, back to abc. */
    wtap_push(1, 1, 250);
    wtap_push(2, 'A', 250);
    wtap_push(1, 2, 250);
    wtap_push(2, '1', 250);
    wtap_push(1, 0, 250);
    /* Centre backspace x2 ("demoA1" -> "demo"). */
    wtap_push(3, 0, 250);
    wtap_push(3, 0, 250);
    /* DISCARD clears; the second DISCARD (empty entry) exits to the
     * WiFi list -- the keyboard's only in-ring exit besides the centre
     * long press (a 90 ms tap is a SHORT_CLICKED, not a long press). */
    wtap_push(4, 0, 400);
    wtap_push(4, 0, 400);
    s_wtap_i = 0;
}

static void wheel_tap_issue(const struct wtap *w) {
    lv_point_t p;
    bool ok = false;
    switch (w->kind) {
    case 1:
        ok = settings_wheel_demo_mode_point(w->arg, &p) && tap_wheel_point(p);
        break;
    case 2:
        ok = settings_wheel_demo_letter_point((char)w->arg, &p) && tap_wheel_point(p);
        break;
    case 3:
        ok = settings_wheel_demo_center_point(&p) && tap_wheel_point(p);
        break;
    case 4:
        ok = settings_wheel_demo_discard_point(&p) && tap_wheel_point(p);
        break;
    default:
        ok = true; /* dwell-only step */
        break;
    }
    if (!ok)
        ESP_LOGW(TAG, "wheel: tap kind %d arg %d not available (register state?)", w->kind, w->arg);
}

/* ---------------- the per-second odometer ----------------------------- */

static const enum ScreensEnum s_moon_order[] = {SCREEN_ID_MODEM_SLEEP, SCREEN_ID_WIFI, SCREEN_ID_LIGHT_SLEEP,
                                                SCREEN_ID_DEEP_SLEEP, SCREEN_ID_ALARM};
static int s_moon_i;

static void odometer_second(void) {
    clock_time_t now;
    if (clock_get_time(&now) == ESP_OK)
        action_clock_widget_label(now.hour, now.minute, now.second);
    tick_user_widget_clock(17);
    /* The demo is activity: hold the idle timer (belt -- the monitor is
     * disabled at ARM; braces for anything that re-enables it). */
    clock_idle_reset_timer();
}

/* Drain the on-demand sync a wake/dismiss started: the UI tick leaves
 * s_wifi_sync_done unclaimed under the demo, so claim it here; then
 * land on the airplane screen (the sync loaded the WiFi screen). */
static bool home_drain_sync(uint32_t budget_ms, const char *what) {
    bool ok;
    if (action_sync_demo_done(&ok)) {
        ESP_LOGI(TAG, "%s: on-demand sync %s", what, ok ? "complete" : "failed");
    } else if (ui_current_screen() != SCREEN_ID_WIFI) {
        /* Nothing to wait for: the wake went straight to the airplane
         * screen (already connected + fresh RTC), or no sync started. */
    } else if (due(s_sub_t0, budget_ms)) {
        phase_timeout(what, budget_ms);
    } else {
        return false;
    }
    ensure_screen(SCREEN_ID_MODEM_SLEEP);
    clock_idle_set_enabled(false);
    return true;
}

/* ---------------- phase steppers (every 10 ms) ------------------------ */

static void step_arm(void) {
    /* No boot-animation wait: the home bakes its static phase at boot
     * (the synodic boot sweep retired 2026-11-13), so the tour arms on
     * schedule. */
    clock_idle_set_enabled(false);
    s_active = true;
    ESP_LOGI(TAG, "demo mode armed: full tour (screen dwell %d s, dusk wait %d s)", CONFIG_UI_DEMO_SCREEN_SEC,
             CONFIG_UI_DEMO_DUSK_WAIT_S);
    s_moon_i = 0;
    phase_enter(PH_MOONS);
}

static void step_moons(void) {
    const uint32_t sec = (uint32_t)CONFIG_UI_DEMO_SCREEN_SEC * DEMO_MS_PER_S;
    if (!due(s_sub_t0, sec))
        return;
    if (s_sub == 1) { /* the final home dwell is over -> the hub walk */
        phase_enter(PH_HUB_WALK);
        return;
    }
    s_moon_i = (s_moon_i + 1) % (int)(sizeof(s_moon_order) / sizeof(s_moon_order[0]));
    loadScreen(s_moon_order[s_moon_i]);
    ui_tick();
    s_sub_t0 = now_ms();
    if (s_moon_i == 0) { /* back at the home screen after the full rotation */
        s_sub = 1;       /* one more dwell, then the hub walk */
    } else {
        ESP_LOGI(TAG, "phase=moons cycle=%u screen=%d (next in %d s)", (unsigned)s_cycle, (int)ui_current_screen(),
                 CONFIG_UI_DEMO_SCREEN_SEC);
    }
}

static void step_hub_walk(void) {
    if (s_sub == 0) { /* phase entry: build the list, tap home (centre) */
        hub_walk_build();
        tap_obj(objects.modem_sleep); /* the centre is rotation-invariant */
        s_settle_t0 = 0;
        s_sub = 1;
        s_sub_t0 = now_ms();
        return;
    }
    if (s_sub == 1) { /* home tap landed -> hub */
        if (!tap_settled())
            return;
        ensure_screen(SCREEN_ID_SETTINGS_HUB);
        s_sub = 2;
        s_sub_t0 = now_ms();
        return;
    }
    if (s_sub == 2) { /* dwell on the hub so the ring reads on camera */
        if (!due(s_sub_t0, (uint32_t)CONFIG_UI_DEMO_SCREEN_SEC * DEMO_MS_PER_S / 2))
            return;
        s_sub = 3;
        s_settle_t0 = 0;
        return;
    }
    /* s_sub == 3: the walk list */
    if (s_walk_i >= s_walk_n) {
        phase_enter(PH_WIFI);
        return;
    }
    const struct wstep *w = &s_walk[s_walk_i];
    switch (s_sub) {
    case 3: /* issue the tap (a NULL target = verify only) */
        if (w->obj) {
            tap_obj(w->obj);
            s_settle_t0 = 0;
        } else {
            s_settle_t0 = now_ms();
        }
        s_sub = 4;
        break;
    case 4: /* settle + verify */
        if (!tap_settled())
            break;
        ensure_screen(w->want);
        s_sub = 5;
        s_sub_t0 = now_ms();
        break;
    case 5: /* dwell on the sub-screen */
        if (w->dwell_ms && !due(s_sub_t0, w->dwell_ms))
            break;
        s_walk_i++;
        s_sub = 3;
        break;
    default:
        s_sub = 3;
        break;
    }
    if (due(s_phase_t0, (s_walk_n + 4) * ((uint32_t)CONFIG_UI_DEMO_SCREEN_SEC + 20) * DEMO_MS_PER_S)) {
        phase_timeout("hub walk", (s_walk_n + 4) * ((uint32_t)CONFIG_UI_DEMO_SCREEN_SEC + 20) * DEMO_MS_PER_S);
        ensure_screen(SCREEN_ID_SETTINGS_HUB);
        phase_enter(PH_WIFI);
    }
}

/* The wifi icon pointer (the walk's icon array is local; re-find it by
 * target for the WIFI phase entry). */
static const lv_obj_t *wifi_icon_find(void) {
    const lv_obj_t *icons[8];
    enum ScreensEnum targets[8];
    int n = screens_hub_icons(icons, targets, 8);
    for (int i = 0; i < n; i++)
        if (targets[i] == SCREEN_ID_SETTINGS_WIFI)
            return icons[i];
    return NULL;
}

static void step_wifi(void) {
    const uint32_t scan_budget_ms = 12000;
    switch (s_sub) {
    case 0: /* on the hub: tap the wifi icon */
        tap_obj(wifi_icon_find());
        s_settle_t0 = 0;
        s_sub = 1;
        break;
    case 1:
        if (!tap_settled())
            break;
        ensure_screen(SCREEN_ID_SETTINGS_WIFI);
        s_sub = 2;
        s_sub_t0 = now_ms();
        break;
    case 2: /* let the scan finish (the screen refresh armed it) */
    {
        if (wifi_scan_in_progress()) {
            if (due(s_sub_t0, scan_budget_ms)) {
                phase_timeout("wifi scan", scan_budget_ms);
                s_sub = 3;
            }
            break;
        }
        wifi_ap_info_t aps[4];
        int n = wifi_scan_get_results(aps, 4);
        if (n <= 0 && due(s_sub_t0, scan_budget_ms))
            phase_timeout("wifi scan", scan_budget_ms);
        s_sub = 3;
        s_sub_t0 = now_ms();
        break;
    }
    case 3: /* tap the first AP row (or open the wheel prefilled) */
    {
        wifi_ap_info_t aps[4];
        int n = wifi_scan_get_results(aps, 4);
        if (n > 0) {
            ESP_LOGI(TAG, "wifi: %d APs, tapping row 0 (%s)", n, aps[0].ssid);
            tap_obj(screens_wifi_row_get(0));
        } else {
            ESP_LOGW(TAG, "wifi: no APs -- opening the wheel prefilled (DEMO-AP)");
            settings_wheel_demo_open("DEMO-AP");
        }
        s_settle_t0 = 0;
        s_sub = 4;
        break;
    }
    case 4:
        if (!tap_settled())
            break;
        if (ui_current_screen() != SCREEN_ID_SETTINGS_WIFI_PASSWORD) {
            /* The row tap was rejected (the scan list changed under the
             * tap) -- the prefilled fallback still shows the keyboard. */
            settings_wheel_demo_open("DEMO-AP");
        }
        phase_enter(PH_WHEEL);
        break;
    default:
        phase_enter(PH_WHEEL);
        break;
    }
    if (due(s_phase_t0, 40000)) {
        phase_timeout("wifi", 40000);
        settings_wheel_demo_open("DEMO-AP");
        phase_enter(PH_WHEEL);
    }
}

static void step_wheel(void) {
    static uint32_t dwell_t0;
    if (s_sub == 0) { /* phase entry */
        wheel_drive_build();
        s_sub = 1;
        s_settle_t0 = 0;
        return;
    }
    if (s_wtap_i >= s_wtap_n) {
        /* The ladder ends with DISCARD (clears) + DISCARD (exits empty),
         * so the entry must be empty on arrival at the WiFi list. */
        if (settings_wheel_demo_pw_len() != 0)
            ESP_LOGW(TAG, "wheel: discard ladder left %d chars in the entry", settings_wheel_demo_pw_len());
        ensure_screen(SCREEN_ID_SETTINGS_WIFI); /* the last discard exits */
        phase_enter(PH_BACK);
        return;
    }
    const struct wtap *w = &s_wtap[s_wtap_i];
    switch (s_sub) {
    case 1:
        wheel_tap_issue(w);
        s_settle_t0 = 0;
        s_sub = 2;
        break;
    case 2:
        if (!tap_settled())
            break;
        s_sub = 3;
        dwell_t0 = now_ms();
        break;
    case 3:
        if (!due(dwell_t0, w->dwell_ms))
            break;
        s_wtap_i++;
        s_sub = 1;
        break;
    default:
        s_sub = 1;
        break;
    }
    if (due(s_phase_t0, 60000)) {
        phase_timeout("wheel", 60000);
        loadScreen(SCREEN_ID_SETTINGS_WIFI);
        ui_tick();
        settings_screen_refresh_for(SCREEN_ID_SETTINGS_WIFI);
        phase_enter(PH_BACK);
    }
}

static void step_back(void) {
    switch (s_sub) {
    case 0: /* the wifi sub-screen exit -> hub (verified after settling) */
        tap_obj(sub_exit_of(SCREEN_ID_SETTINGS_WIFI));
        s_settle_t0 = 0;
        s_sub = 1;
        s_sub_t0 = now_ms();
        break;
    case 1:
        if (!due(s_sub_t0, TAP_SETTLE_MS))
            break;
        ensure_screen(SCREEN_ID_SETTINGS_HUB);
        tap_obj(objects.settings_hub_exit);
        s_settle_t0 = 0;
        s_sub = 2;
        s_sub_t0 = now_ms();
        break;
    case 2:
        if (!due(s_sub_t0, TAP_SETTLE_MS))
            break;
        ensure_screen(SCREEN_ID_MODEM_SLEEP);
        phase_enter(PH_DUSK);
        break;
    default:
        phase_enter(PH_DUSK);
        break;
    }
    if (due(s_phase_t0, 20000)) {
        phase_timeout("back", 20000);
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        phase_enter(PH_DUSK);
    }
}

/* The real alarm-ring script (mirrors the TEST_ALARM_RING_SEC hook):
 * fire, stop after the volume ramp, re-fire (codec RE-OPEN), dismiss.
 * The scheduled-alarm suppression is opened only for this phase. */
static void step_alarm(void) {
    switch (s_sub) {
    case 0:
        action_demo_allow_alarm(true);
        action_alarm_triggered(NULL);
        s_sub = 1;
        s_sub_t0 = now_ms();
        break;
    case 1:
        if (!due(s_sub_t0, 9000))
            break;
        alarm_audio_ring_stop();
        s_sub = 2;
        s_sub_t0 = now_ms();
        break;
    case 2:
        if (!due(s_sub_t0, 4000))
            break;
        action_alarm_triggered(NULL); /* re-fire: codec re-open */
        s_sub = 3;
        s_sub_t0 = now_ms();
        break;
    case 3:
        if (!due(s_sub_t0, 6000))
            break;
        action_wake_screen(NULL); /* the real dismiss (stop + wake flow) */
        action_demo_allow_alarm(false);
        s_sub = 4;
        s_sub_t0 = now_ms();
        break;
    case 4: /* the dismiss ran the on-demand wake sync -- drain it, end
             * of cycle (the alarm runs LAST: see step_sync) */
        if (home_drain_sync(45000, "alarm dismiss")) {
            s_cycle++;
            s_moon_i = 0;
            phase_enter(PH_MOONS);
        }
        break;
    default:
        action_demo_allow_alarm(false);
        s_cycle++;
        s_moon_i = 0;
        phase_enter(PH_MOONS);
        break;
    }
}

static void step_dusk(void) {
    const uint32_t budget_ms = (uint32_t)CONFIG_UI_DEMO_DUSK_WAIT_S * DEMO_MS_PER_S;
    switch (s_sub) {
    case 0: /* the real light-sleep entry: pause + dusk spin + dim */
        action_switch_to_light_sleep_screen(NULL);
        s_sub = 1;
        s_sub_t0 = now_ms();
        break;
    case 1:
        if (moon_ui_dusk_complete()) {
            s_sub = 2;
            s_sub_t0 = now_ms();
        } else if (due(s_sub_t0, budget_ms)) {
            phase_timeout("dusk spin", budget_ms);
            s_sub = 2;
            s_sub_t0 = now_ms();
        }
        break;
    default:
        break;
    }
    if (s_sub == 2 && due(s_sub_t0, 10000)) /* linger 10 s on the dark side */
        phase_enter(PH_DUSK_DARK);
    if (due(s_phase_t0, budget_ms + 30000)) {
        phase_timeout("dusk", budget_ms + 30000);
        phase_enter(PH_DUSK_DARK);
    }
}

static void step_dusk_dark(void) {
    /* One step: the real wake (spin resumes, dusk flag resets, the
     * wake flow runs the on-demand sync -- drained by PH_HOME). */
    action_wake_screen(NULL);
    phase_enter(PH_HOME);
}

static void step_home(void) {
    if (home_drain_sync(45000, "dusk wake"))
        phase_enter(PH_ROLL);
}

static void step_roll(void) {
    if (s_sub == 0) {
        action_roll_test_begin();
        s_sub = 1;
        s_sub_t0 = now_ms();
        return;
    }
    if (!action_roll_test_busy()) {
        phase_enter(PH_SYNC);
        return;
    }
    if (due(s_sub_t0, 60000))
        phase_timeout("roll script", 60000);
    if (due(s_phase_t0, 70000))
        phase_enter(PH_SYNC);
}

static void step_sync(void) {
    switch (s_sub) {
    case 0:
        ensure_screen(SCREEN_ID_MODEM_SLEEP);
        action_start_wifi_sync(); /* loads the WiFi screen + the task */
        s_sub = 1;
        s_sub_t0 = now_ms();
        break;
    case 1:
        if (home_drain_sync(45000, "tour sync"))
            phase_enter(PH_ALARM); /* last phase: see step_alarm's tail */
        break;
    default:
        phase_enter(PH_ALARM);
        break;
    }
}

/* ---------------- the 10 ms engine tick (LVGL task) ------------------- */

static void demo_tick_cb(lv_timer_t *timer) {
    (void)timer;
    vt_step_10ms();
    if (due(s_sec_t0, DEMO_MS_PER_S)) {
        s_sec_t0 = now_ms();
        if (s_active)
            odometer_second();
    }
    switch (s_phase) {
    case PH_ARM:
        step_arm();
        break;
    case PH_MOONS:
        step_moons();
        break;
    case PH_HUB_WALK:
        step_hub_walk();
        break;
    case PH_WIFI:
        step_wifi();
        break;
    case PH_WHEEL:
        step_wheel();
        break;
    case PH_BACK:
        step_back();
        break;
    case PH_ALARM:
        step_alarm();
        break;
    case PH_HOME:
        step_home();
        break;
    case PH_DUSK:
        step_dusk();
        break;
    case PH_DUSK_DARK:
        step_dusk_dark();
        break;
    case PH_ROLL:
        step_roll();
        break;
    case PH_SYNC:
        step_sync();
        break;
    default:
        phase_enter(PH_MOONS);
        break;
    }
}

void ui_demo_init(void) {
    s_vt = lv_indev_create();
    if (!s_vt) {
        ESP_LOGE(TAG, "virtual pointer creation failed -- the tour runs screen-scripted only");
    } else {
        lv_indev_set_type(s_vt, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_vt, vt_read_cb);
    }
    s_sec_t0 = now_ms();
    lv_timer_create(demo_tick_cb, 10, NULL);
    ESP_LOGI(TAG, "demo mode: tour created (arming after the boot sweep)");
}

#else /* !CONFIG_UI_DEMO_MODE -- production stubs (the hooks stay compiling) */

bool action_demo_active(void) { return false; }

void ui_demo_init(void) {}

#endif /* CONFIG_UI_DEMO_MODE */
