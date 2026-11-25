/* UI simulator main: renders the REAL screens.c (device UI code) on the
 * host with the vendored LVGL 9.6 (SW renderer, RGB565, 466x466, the
 * device's lv_conf feature set + LV_USE_SNAPSHOT) and writes one PNG per
 * screen. The moon screens get the REAL current-phase moon baked by the
 * portable core (stubs.c).
 *
 * Build:  host_sim/uisim/build.sh
 * Output: host_sim/uisim/out/<screen>.png
 *
 * Usage: uisim [screen ...] -- without args, snapshot all 12 screens.
 * Screen names: modem_sleep wifi light_sleep deep_sleep alarm
 *               settings_hub settings_alarm settings_pomodoro
 *               settings_alarm_time settings_light_sleep
 *               settings_deep_sleep settings_wifi
 *
 * Special modes:
 *   --rolltest  verify the HH:MM digit ROLL (odometer): loads home, changes
 *               the minute 12:35 -> 12:36 through the real var + tick path,
 *               and writes out/roll_{before,up_mid,up_after,down_mid,
 *               down_after}.png -- settle, then 12:35 -> 12:36 (the "5"
 *               rolls UP into the "6"), then 12:36 -> 12:35 (the "6" rolls
 *               DOWN into the "5"): the direction-aware strip scroll, both
 *               clipped to the one-line window.
 *   --clockprobe  print the HH:MM row object geometry (cells, pads, label
 *                 bboxes + text_align) to stdout -- verifies the pair-cell
 *                 clip/anchoring fix without eyeballing the PNG.
 *   --rottest / --reveal <q> [screen] / --wheel: the other verification
 *               hooks (see their blocks below).
 *   --tour  the docs promo film (S3 466 only): the full 17-screen round
 *           trip through the REAL UI, scripted by the same indev as
 *           --wheel (real widgets tapped at their AABB centres, real
 *           rollers dragged, the wheel keyboard typed with the
 *           never-tap rules), every frame dumped from the display front
 *           buffer as out/tour_%04d.png at the film's 15 fps. Encoded
 *           by docs/gen_media.py into docs/img/promo.mp4.
 *   --dial [screen]  (knob panel only) capture the dial edit cursor:
 *               screens_dial_init + one clockwise detent on the given
 *               screen (default settings_hub), written to
 *               out/<screen>_dial.png -- the state after a user's first
 *               dial turn on that screen.
 */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "screens.h"
#include "stb_image_write.h"
#include "ui.h"
#include "ui_px.h"
#include "vars.h"

/* The panel size (the S3's 466x466 by default; the P4's 720x720 with
 * `build.sh run --panel 720`). The screens themselves carry their own
 * literals until the M4 upscale, so at 720 the moon (driven by
 * PANEL_SIZE in stubs.c) is the thing under test here. */
#ifndef PANEL_SIZE
#define PANEL_SIZE 466
#endif
#define W PANEL_SIZE
#define H PANEL_SIZE

static uint8_t s_fb[W * H * 2]; /* RGB565 full-panel front buffer */
static uint32_t s_now_ms;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}
static uint32_t tick_cb(void) { return s_now_ms; }

static void pump(lv_display_t *disp, uint32_t ms) {
    uint32_t end = s_now_ms + ms;
    while (s_now_ms < end) {
        s_now_ms += 10;
        lv_timer_handler();
    }
    lv_refr_now(disp);
}

/* The device's concentric-reveal rotation (moon_ui.c, copied verbatim
 * so the probe exercises the exact transform). */
static void reveal_transpose(uint16_t *p, int n) {
    for (int i = 1; i < n; i++)
        for (int j = 0; j < i; j++) {
            const uint16_t t = p[(size_t)i * n + j];
            p[(size_t)i * n + j] = p[(size_t)j * n + i];
            p[(size_t)j * n + i] = t;
        }
}

static void reveal_reverse_rows(uint16_t *p, int n) {
    for (int y = 0; y < n; y++) {
        uint16_t *row = p + (size_t)y * n;
        for (int a = 0, b = n - 1; a < b; a++, b--) {
            const uint16_t t = row[a];
            row[a] = row[b];
            row[b] = t;
        }
    }
}

static void reveal_rotate_cw90(uint16_t *p, int n, int q) {
    switch (q) {
    case 1: /* 90 CW: transpose, then reverse each row */
        reveal_transpose(p, n);
        reveal_reverse_rows(p, n);
        break;
    case 2:
        for (size_t a = 0, b = (size_t)n * n - 1; a < b; a++, b--) {
            const uint16_t t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
        break;
    case 3: /* 270 CW: reverse each row, then transpose */
        reveal_reverse_rows(p, n);
        reveal_transpose(p, n);
        break;
    default:
        break;
    }
}

static int write_png(const char *path, const lv_draw_buf_t *buf) {
    uint8_t *rgb565 = buf->data;
    uint32_t w = buf->header.w, h = buf->header.h, stride = buf->header.stride;
    if (w != W || h != H) {
        fprintf(stderr, "snapshot %ux%u != %ux%u\n", w, h, W, H);
        return 1;
    }
    static uint8_t *rgb;
    if (!rgb)
        rgb = malloc((size_t)W * H * 3);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint16_t p = ((const uint16_t *)(rgb565 + y * stride))[x];
            uint8_t r5 = p >> 11, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
            rgb[(y * W + x) * 3 + 0] = (uint8_t)(r5 * 255 / 31);
            rgb[(y * W + x) * 3 + 1] = (uint8_t)(g6 * 255 / 63);
            rgb[(y * W + x) * 3 + 2] = (uint8_t)(b5 * 255 / 31);
        }
    }
    if (!stbi_write_png(path, W, H, 3, rgb, W * 3)) {
        fprintf(stderr, "failed to write %s\n", path);
        return 1;
    }
    printf("wrote %s\n", path);
    return 0;
}

/* ---- scripted pointer indev (the --wheel capture) ------------------
 * The input is driven by manual lv_indev_read() calls: the indev's read
 * timer does not fire on the sim's lv_timer_handler loop, so each
 * scripted point is pushed through the indev layer by hand (the same
 * path the timer would take). */

static lv_indev_t *s_wheel_indev;
static lv_indev_state_t s_wheel_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_wheel_pt;

static void wheel_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->state = s_wheel_state;
    data->point = s_wheel_pt;
    /* The state machine times presses off this stamp (the long-press
     * check is lv_tick_diff(timestamp, pr_timestamp)) -- feed it the
     * sim clock so a held point ages into LONG_PRESSED. */
    data->timestamp = s_now_ms;
}

static void wheel_step(lv_display_t *disp, lv_indev_state_t st, int32_t x, int32_t y) {
    s_wheel_pt.x = x;
    s_wheel_pt.y = y;
    s_wheel_state = st;
    lv_indev_read(s_wheel_indev);
    pump(disp, 20);
}

static void wheel_press_at(lv_display_t *disp, int32_t x, int32_t y) { wheel_step(disp, LV_INDEV_STATE_PRESSED, x, y); }

static void wheel_move_to(lv_display_t *disp, int32_t x, int32_t y) { wheel_step(disp, LV_INDEV_STATE_PRESSED, x, y); }

/* Hold a pressed point for `ms` ms: re-read the indev (PRESSED, same
 * point) every 10 ms while the tick advances, so LVGL's long-press state
 * machine (the lv_tick_diff vs pr_timestamp check inside the indev read)
 * gets the 400 ms window. The indev's read timer does NOT fire on the
 * sim's lv_timer_handler loop (see the flow note), so a bare pump()
 * never advances the state machine -- the hold re-drives it by hand. */
static void wheel_hold(lv_display_t *disp, int32_t x, int32_t y, uint32_t ms) {
    uint32_t t = 0;
    while (t < ms) {
        t += 10;
        s_now_ms += 10;
        wheel_step(disp, LV_INDEV_STATE_PRESSED, x, y);
        lv_timer_handler();
    }
    lv_refr_now(disp);
}

static void wheel_release_at(lv_display_t *disp, int32_t x, int32_t y) {
    wheel_step(disp, LV_INDEV_STATE_RELEASED, x, y);
}

static void wheel_tap(lv_display_t *disp, int32_t x, int32_t y) {
    wheel_press_at(disp, x, y);
    wheel_release_at(disp, x, y);
}

/* Panel point at LVGL angle a (deg: 0 = right, 90 = down, CW, y down) at
 * radius r from the panel centre (the wheel's geometry, like the screens,
 * scales with the panel via ui_px.h). */
static void wheel_point_deg(double a_deg, double r, lv_point_t *p) {
    double t = a_deg * M_PI / 180.0;
    double c = (double)UI_PANEL_PX / 2.0;
    p->x = (int32_t)lround(c + r * cos(t));
    p->y = (int32_t)lround(c + r * sin(t));
}

/* The wheel's slot centres (the device's wheel_slot_center, 26 slots). */
static double wheel_slot_deg(int j) { return (j + 0.5) * 360.0 / 26.0 - 90.0; }

static int wheel_snapshot(lv_display_t *disp, const char *name) {
    lv_obj_t *root = ((lv_obj_t **)&objects)[SCREEN_ID_SETTINGS_WIFI_PASSWORD - 1];
    lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
    if (!buf) {
        fprintf(stderr, "wheel snapshot failed: %s\n", name);
        return 1;
    }
    char path[64];
    snprintf(path, sizeof path, "out/wheel_%s.png", name);
    int rc = write_png(path, buf);
    lv_draw_buf_destroy(buf);
    return rc;
}

/* The rot-mode snapshot: the DEVICE render path -- the wheel root carries
 * the orientation pipeline's +90-degree transform, so the flush renders
 * the rotated screen into the display front buffer (the transform layer
 * is exercised for real, unlike the screen-local lv_snapshot above,
 * which ignores the transform). Dump s_fb directly. */
static int wheel_snapshot_rot(lv_display_t *disp, const char *name) {
    lv_refr_now(disp);
    char path[64];
    snprintf(path, sizeof path, "out/wheel_%s.png", name);
    static uint8_t *rgb;
    if (!rgb) {
        rgb = malloc((size_t)W * H * 3);
        if (!rgb)
            return 1;
    }
    for (int i = 0; i < W * H; i++) {
        uint16_t p = ((const uint16_t *)s_fb)[i];
        rgb[i * 3] = (uint8_t)((p >> 11) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)((p >> 5 & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((p & 0x1F) * 255 / 31);
    }
    return stbi_write_png(path, W, H, 3, rgb, W * 3) ? 0 : 1;
}

/* Drive the real flow to the wheel: settings-hub WiFi icon (on the icon
 * ring) -> the WiFi list -> a tap on the roller's window centre (the
 * SELECTED entry) = SSID prefill + the wheel. The roller's CLICKED cb
 * prefills the (stub) first AP's SSID.
 *
 * The hub's ring (create_screen_settings_hub): n icons 360/n apart,
 * the deep-sleep icon anchored at 180 deg -- found BY TARGET on the
 * device (index 2 of the S3's 6-icon ring, 3 of the P4's 7-icon ring;
 * the P4 ring opens with the clock icon at 12 o'clock), th =
 * 180 + step*(i - deep) degrees clockwise from 12 o'clock, position
 * (R sin th, -R cos th) about the centre (R = UI_PX(140)). The WiFi
 * entry's index differs per board, so mirror both indices here. */
static void wheel_hub_wifi_icon(int32_t *x, int32_t *y) {
    int n;
    int wifi_i;
    int deep_i;
#if defined(CONFIG_BOARD_P4)
    n = 7;
    wifi_i = 4;
    deep_i = 3;
#else
    n = 6;
    wifi_i = 3;
    deep_i = 2;
#endif
    const float step = 360.0f / (float)n;
    const float th = ((180.0f + step * ((float)wifi_i - (float)deep_i)) * 3.14159265f) / 180.0f;
    const int32_t R = UI_PX(140);
    *x = UI_PANEL_PX / 2 + (int32_t)(R * sinf(th));
    *y = UI_PANEL_PX / 2 - (int32_t)(R * cosf(th));
}

static void wheel_enter(lv_display_t *disp, lv_indev_t *indev) {
    loadScreen(SCREEN_ID_SETTINGS_HUB);
    ui_tick();
    pump(disp, 100);
    int32_t ix, iy;
    wheel_hub_wifi_icon(&ix, &iy);
    wheel_tap(disp, ix, iy);
    pump(disp, 100);
    /* Tick the WiFi list once so the roller renders its options (its
     * text is what gives the rows content size -- tapping before the
     * render hits empty rows and misses). */
    ui_tick();
    pump(disp, 100);
    /* Tap the roller's WINDOW CENTRE (aligned CENTER + UI_PX(18), so
     * that offset is the SELECTED entry -- family-wide since
     * 2026-11-11). Tapping above the centre lands on the empty slot
     * NORMAL mode draws over the clamped first row, and a disabled
     * cell sends no CLICKED. */
    wheel_tap(disp, UI_PANEL_PX / 2, UI_PANEL_PX / 2 + UI_PX(18));
    pump(disp, 100);
    (void)indev;
}

/* ---- --tour: the docs promo film (the scripted round trip) ----------
 * The whole UI as a 15 fps film: one frame per 20 ms of sim clock,
 * dumped from the display front buffer (the sim's screen switches are
 * lv_scr_load with no fade, so a per-frame full-panel dump IS the true
 * panel state). The input is the --wheel scripted indev: real widgets
 * tapped at their AABB centres (rotation-safe), real rollers dragged
 * (press - move - release = one detent, the RELEASED persist), the
 * wheel keyboard typed with the on-device tour's never-tap rules
 * (docs/verification/demo-mode): NEVER the COMMIT key (LVGL 60 deg - it
 * writes wifi_credentials.txt + starts a connect) and NEVER the alarm
 * master glyph (it deactivates every rule); the wheel is left with
 * DISCARD (LVGL 120 deg) + the centre 400 ms long-press. */

static int s_tour_n; /* the frame counter (out/tour_%04d.png) */

/* A settle after a tap / screen swap (~0.5 s of the film). */
#define TOUR_SETTLE 8
/* A sub-screen / machine-screen dwell (~1.3 s). */
#define TOUR_DWELL 20

/* Dump the display front buffer as the next numbered frame. */
static int tour_capture(lv_display_t *disp) {
    lv_refr_now(disp);
    static uint8_t *rgb;
    if (!rgb) {
        rgb = malloc((size_t)W * H * 3);
        if (!rgb)
            return 1;
    }
    for (int i = 0; i < W * H; i++) {
        uint16_t p = ((const uint16_t *)s_fb)[i];
        rgb[i * 3] = (uint8_t)((p >> 11) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)((p >> 5 & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((p & 0x1F) * 255 / 31);
    }
    char path[64];
    snprintf(path, sizeof path, "out/tour_%04d.png", s_tour_n++);
    if (!stbi_write_png(path, W, H, 3, rgb, W * 3)) {
        fprintf(stderr, "tour: failed to write %s\n", path);
        return 1;
    }
    return 0;
}

/* `frames` film frames: the sim clock advances 20 ms per frame (the
 * seconds arc and the HH:MM roll keep moving - the film shows a live
 * clock, not frozen stills). The device's screen tick runs on its own
 * timer (not the sim's lv_timer_handler), so the first frame gets one
 * ui_tick() - without it the WiFi list would freeze on its SCANNING
 * placeholder (the rows render inside the tick, after the stub scan
 * completes). */
static int tour_dwell(lv_display_t *disp, int frames) {
    ui_tick();
    for (int i = 0; i < frames; i++) {
        pump(disp, 20);
        if (tour_capture(disp))
            return 1;
    }
    return 0;
}

/* A scripted tap at a panel point: the press frame (the touched state)
 * + a settle dwell. */
static int tour_tap_at(lv_display_t *disp, int32_t x, int32_t y) {
    wheel_press_at(disp, x, y);
    if (tour_capture(disp))
        return 1;
    wheel_release_at(disp, x, y);
    return tour_dwell(disp, TOUR_SETTLE);
}

/* Tap a real widget at its AABB centre. */
static int tour_tap_obj(lv_display_t *disp, lv_obj_t *obj) {
    if (!obj)
        return 1;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return tour_tap_at(disp, (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

/* One roller detent at a panel point: press, drag `dy` px in 5 steps
 * (so the gesture reads on camera), release (the RELEASED persist). */
static int tour_roller_drag(lv_display_t *disp, int32_t x, int32_t y, int32_t dy) {
    wheel_press_at(disp, x, y);
    const int n = 5;
    for (int i = 1; i <= n; i++)
        wheel_move_to(disp, x, y + dy * i / n);
    if (tour_capture(disp)) /* the mid-drag frame (the rows shifted) */
        return 1;
    wheel_release_at(disp, x, y + dy);
    return tour_dwell(disp, TOUR_SETTLE);
}

/* One detent on a real roller object (at its centre). */
static int tour_roller_obj(lv_display_t *disp, lv_obj_t *obj, int32_t dy) {
    if (!obj)
        return 1;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return tour_roller_drag(disp, (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2, dy);
}

/* The screen ROOT of an id (the layout contract: the objects array is
 * the ScreensEnum order, 1-based). */
static lv_obj_t *tour_root(enum ScreensEnum id) { return ((lv_obj_t **)&objects)[id - 1]; }

/* A machine-only screen (the sleep states, the transient sync, the
 * alarm fire): the sleep MACHINE enters these, not a touch, so the
 * film loads them directly with the device entry's look mirrored
 * (snapshot_one): light sleep dims the clock, deep sleep hides it. */
static int tour_machine_screen(lv_display_t *disp, enum ScreensEnum id) {
    loadScreen(id);
    ui_tick();
    if (id == SCREEN_ID_LIGHT_SLEEP) {
#ifndef CONFIG_UI_CLOCK_ALPHA_PCT
#define CONFIG_UI_CLOCK_ALPHA_PCT 20
#endif
        lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_MAX * CONFIG_UI_CLOCK_ALPHA_PCT / 100, 0);
    } else if (id == SCREEN_ID_DEEP_SLEEP) {
        lv_obj_add_flag(objects.deep_sleep_clock, LV_OBJ_FLAG_HIDDEN);
    } else if (id == SCREEN_ID_SETTINGS_WIFI) {
        /* The hub-icon entry path: arm the scan (the stub's canned
         * list is immediately complete) + render the rows. */
        settings_screen_refresh_for(id);
        ui_tick();
    }
    pump(disp, 300); /* the HH:MM attach roll must finish before the film */
    return tour_dwell(disp, TOUR_DWELL);
}

/* The whole tour: home -> hub -> every ring icon (with its
 * sub-screen's interactions) -> hub exit -> the machine-only screens
 * -> home. Returns 0 on success. */
static int tour_run(lv_display_t *disp) {
    const int32_t C = UI_PANEL_PX / 2;
    lv_obj_t *icons[8];
    enum ScreensEnum targets[8];
    const int nicons = screens_hub_icons((const lv_obj_t **)icons, targets, 8);

    /* 1. Home: the live moon at the sim's seed, the clock, the seconds
     * arc - the hero shot. The root pass-through tap opens the hub. */
    if (tour_dwell(disp, 45))
        return 1;
    if (tour_tap_at(disp, C, C))
        return 1;
    if (tour_dwell(disp, 45)) /* the hub: the ring reads on camera */
        return 1;

    /* 2. The ring walk, in ring order (the registry order). */
    for (int i = 0; i < nicons; i++) {
        int exited = 0; /* the flow already back at the hub */
        if (tour_tap_obj(disp, icons[i]))
            return 1;
        switch (targets[i]) {
        case SCREEN_ID_SETTINGS_ALARM:
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            /* NEVER the master glyph (the deactivate-all rule). The
             * two entries: the pomodoro interval, then the real-alarm
             * time - each a round trip through its own sub-screen. */
            if (tour_tap_obj(disp, objects.settings_alarm_pomodoro))
                return 1;
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            if (tour_roller_obj(disp, objects.settings_pomodoro_roller_h, -100))
                return 1;
            if (tour_tap_obj(disp, screens_sub_exit_get(tour_root(SCREEN_ID_SETTINGS_POMODORO))))
                return 1;
            if (tour_dwell(disp, 6))
                return 1;
            if (tour_tap_obj(disp, objects.settings_alarm_clock))
                return 1;
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            if (tour_roller_obj(disp, objects.settings_alarm_time_roller_h, -100))
                return 1;
            if (tour_tap_obj(disp, screens_sub_exit_get(tour_root(SCREEN_ID_SETTINGS_ALARM_TIME))))
                return 1;
            break;
        case SCREEN_ID_SETTINGS_LIGHT_SLEEP:
        case SCREEN_ID_SETTINGS_DEEP_SLEEP:
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            if (tour_roller_drag(disp, C, C, -100)) /* the centred minutes roller */
                return 1;
            break;
        case SCREEN_ID_SETTINGS_WIFI:
            if (tour_dwell(disp, TOUR_DWELL)) /* the canned scan lands: rows */
                return 1;
            /* The AP row (the roller's window centre = the selected
             * entry) opens the wheel keyboard with the SSID prefilled. */
            if (tour_tap_obj(disp, screens_wifi_row_get(0)))
                return 1;
            if (tour_dwell(disp, 30)) /* the wheel: the keyboard hero */
                return 1;
            {
                /* Two letters (press + release commits on release). */
                lv_point_t p;
                for (int slot = 7; slot <= 8; slot++) {
                    wheel_point_deg(wheel_slot_deg(slot), UI_PX(188), &p);
                    wheel_press_at(disp, p.x, p.y);
                    if (tour_capture(disp))
                        return 1;
                    wheel_release_at(disp, p.x, p.y);
                    if (tour_dwell(disp, 4))
                        return 1;
                }
                /* The register keys (LVGL 180+60k, r 114): ABC, 123,
                 * back to abc - the remap reads on camera. */
                const double regs[3] = {240.0, 300.0, 180.0};
                for (int k = 0; k < 3; k++) {
                    wheel_point_deg(regs[k], UI_PX(114), &p);
                    if (tour_tap_at(disp, p.x, p.y))
                        return 1;
                }
                /* DISCARD (LVGL 120): the password clears. */
                wheel_point_deg(120.0, UI_PX(114), &p);
                if (tour_tap_at(disp, p.x, p.y))
                    return 1;
                /* The centre long-press exits to the list (a plain tap
                 * only backspaces). The hold re-drives the indev so the
                 * 400 ms long-press state machine ages. */
                wheel_press_at(disp, C, C);
                if (tour_capture(disp))
                    return 1;
                wheel_hold(disp, C, C, 200);  /* ~600 ms of tick: past 400 */
                wheel_release_at(disp, 5, 5); /* off any row */
                if (tour_dwell(disp, 6))
                    return 1;
            }
            /* The list's exit back to the hub. */
            if (tour_tap_obj(disp, screens_sub_exit_get(tour_root(SCREEN_ID_SETTINGS_WIFI))))
                return 1;
            exited = 1;
            break;
        case SCREEN_ID_SETTINGS_TZ:
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            if (tour_roller_obj(disp, objects.settings_tz_roller, -100))
                return 1;
            break;
        case SCREEN_ID_SETTINGS_ALARM_SOUND:
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            if (tour_roller_obj(disp, objects.settings_alarm_sound_roller, -100))
                return 1;
            if (tour_tap_at(disp, C + UI_PX(150), C)) /* the preview speaker */
                return 1;
            break;
        case SCREEN_ID_SETTINGS_DISPLAY:
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            /* The 0..359 trim roller (the centre card tips live): at
             * the panel centre minus UI_PX(100). */
            if (tour_roller_drag(disp, C, C - UI_PX(100), -60))
                return 1;
            break;
        default: /* the P4's clock icon (not on the S3/knob ring): dwell */
            if (tour_dwell(disp, TOUR_DWELL))
                return 1;
            break;
        }
        if (!exited) {
            /* Every icon's sub-screen exits to the hub (the shell's
             * bottom-centre X). */
            if (tour_tap_obj(disp, screens_sub_exit_get(tour_root(targets[i]))))
                return 1;
            if (tour_dwell(disp, 6)) /* a beat so the ring reads again */
                return 1;
        }
    }

    /* 3. The hub's central exit -> home. */
    if (tour_dwell(disp, 15))
        return 1;
    if (tour_tap_obj(disp, objects.settings_hub_exit))
        return 1;
    if (tour_dwell(disp, 15))
        return 1;

    /* 4. The machine-only screens (the sleep machine + the transient
     * sync + the alarm fire), then the final home hold. */
    if (tour_machine_screen(disp, SCREEN_ID_LIGHT_SLEEP))
        return 1;
    if (tour_machine_screen(disp, SCREEN_ID_DEEP_SLEEP))
        return 1;
    if (tour_machine_screen(disp, SCREEN_ID_WIFI))
        return 1;
    if (tour_machine_screen(disp, SCREEN_ID_ALARM))
        return 1;
    loadScreen(SCREEN_ID_MODEM_SLEEP);
    ui_tick();
    pump(disp, 300); /* the attach roll settles before the last frames */
    return tour_dwell(disp, 45);
}

struct screen_entry {
    enum ScreensEnum id;
    const char *name;
};

static const struct screen_entry s_entries[] = {
    {SCREEN_ID_MODEM_SLEEP, "modem_sleep"},
    {SCREEN_ID_WIFI, "wifi"},
    {SCREEN_ID_LIGHT_SLEEP, "light_sleep"},
    {SCREEN_ID_DEEP_SLEEP, "deep_sleep"},
    {SCREEN_ID_ALARM, "alarm"},
    {SCREEN_ID_SETTINGS_HUB, "settings_hub"},
    {SCREEN_ID_SETTINGS_ALARM, "settings_alarm"},
    {SCREEN_ID_SETTINGS_POMODORO, "settings_pomodoro"},
    {SCREEN_ID_SETTINGS_ALARM_TIME, "settings_alarm_time"},
    {SCREEN_ID_SETTINGS_LIGHT_SLEEP, "settings_light_sleep"},
    {SCREEN_ID_SETTINGS_DEEP_SLEEP, "settings_deep_sleep"},
    {SCREEN_ID_SETTINGS_WIFI, "settings_wifi"},
    {SCREEN_ID_SETTINGS_WIFI_PASSWORD, "settings_wifi_password"},
    {SCREEN_ID_SETTINGS_TIME, "settings_time"},
    {SCREEN_ID_SETTINGS_TZ, "settings_tz"},
    {SCREEN_ID_SETTINGS_ALARM_SOUND, "settings_alarm_sound"},
    {SCREEN_ID_SETTINGS_DISPLAY, "settings_display"},
};

static int snapshot_one(lv_display_t *disp, enum ScreensEnum id) {
    const char *name = NULL;
    for (size_t i = 0; i < sizeof s_entries / sizeof s_entries[0]; i++)
        if (s_entries[i].id == id)
            name = s_entries[i].name;
    if (!name)
        return 1;

    loadScreen(id);
    ui_tick(); /* seed the screen's widgets from the stubbed state */
    /* Mirror the device's sleep ENTRIES (actions.c) so the snapshot is
     * the device's actual look, not the bare screen:
     *  - light sleep: the clock container dims to UI_CLOCK_ALPHA_PCT
     *    (the clock's "alpha"; the panel's own hardware dim is a
     *    brightness ramp, not pixels -- not simulated);
     *  - deep sleep: the clock is HIDDEN (the dead display: the device
     *    also turns the backlight off, which is not a pixel change). */
    if (id == SCREEN_ID_LIGHT_SLEEP) {
#ifndef CONFIG_UI_CLOCK_ALPHA_PCT
#define CONFIG_UI_CLOCK_ALPHA_PCT 20
#endif
        lv_obj_set_style_opa(objects.light_sleep_clock, LV_OPA_MAX * CONFIG_UI_CLOCK_ALPHA_PCT / 100, 0);
    } else if (id == SCREEN_ID_DEEP_SLEEP) {
        lv_obj_add_flag(objects.deep_sleep_clock, LV_OBJ_FLAG_HIDDEN);
    } else if (id == SCREEN_ID_SETTINGS_WIFI) {
        /* Mirror the device's entry path (the hub tap helper calls
         * settings_screen_refresh_for): the scan is armed, the stub's
         * canned list is immediately complete, and this extra tick
         * renders the roller's options -- without it the snapshot
         * would freeze the roller on its SCANNING placeholder (the
         * s_wifi_scan_rendered init-true skips the first tick render,
         * exactly as on the device before its entry refresh). */
        settings_screen_refresh_for(id);
        ui_tick();
    }
    /* 300 ms: the HH:MM digit roll (the attach refresh rolls changed
     * digits through the 300 ms odometer slide) must have FINISHED
     * before the capture, or the digits would read mid-roll in the
     * snapshot. */
    pump(disp, 300); /* let pending invalidations + the roll settle */
    lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
    lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
    if (!buf) {
        fprintf(stderr, "snapshot failed for %s\n", name);
        return 1;
    }
    char path[128];
    snprintf(path, sizeof path, "out/%s.png", name);
    int rc = write_png(path, buf);
    lv_draw_buf_destroy(buf);
    return rc;
}

int main(int argc, char **argv) {
    mkdir("out", 0755);

    lv_init();
    /* The tick callback comes AFTER lv_init: lv_init's
     * LV_GLOBAL_INIT zeroes the global (incl. the tick state), so a
     * callback set before it is wiped and lv_tick_get() stays pinned at
     * 0 -- the animation engine (time-based) then never advances in the
     * sim. The device build is unaffected (its tick comes from the OS
     * layer). */
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    create_screens(); /* real device code: theme + all 12 screens */
    pump(disp, 200);

    /* --rottest: prove whether lv_snapshot_take honors the root's LVGL
     * transform (the concentric reveal assumes the +90 deg base rotation
     * lands in the snapshot buffer). The alarm screen is asymmetric:
     * header icon top-centre, read-back text near the bottom. A +90 deg
     * CW rotation about the centre must move the header to the RIGHT
     * edge and the text to the LEFT edge in the snapshot. */
    if (argc > 1 && !strcmp(argv[1], "--rottest")) {
        const enum ScreensEnum id = SCREEN_ID_SETTINGS_ALARM;
        loadScreen(id);
        ui_tick();
        pump(disp, 100);
        lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
        lv_obj_set_style_transform_pivot_x(root, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(root, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(root, 900, 0);
        pump(disp, 100);
        lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        if (!buf) {
            fprintf(stderr, "rottest: snapshot failed\n");
            return 1;
        }
        int rc = write_png("out/rottest_90.png", buf);
        lv_draw_buf_destroy(buf);
        return rc;
    }

#ifdef CONFIG_BOARD_KNOB
    /* --dial [screen]: the knob's dial edit cursor (screens.c "P2 dial
     * surface"). The knob's hardware encoder is not in the sim, but the
     * cursor logic is pure screens.c -- drive it through its own entry
     * points: screens_dial_init (the post-ui_init focus state), load
     * the screen, one clockwise detent, capture. The docs use this for
     * the hub's focused-ring-icon shot (the knob column of the UI
     * pages). */
    if (argc > 1 && !strcmp(argv[1], "--dial")) {
        const char *name = (argc > 2) ? argv[2] : "settings_hub";
        enum ScreensEnum id = SCREEN_ID_SETTINGS_HUB;
        for (size_t j = 0; j < sizeof s_entries / sizeof s_entries[0]; j++)
            if (!strcmp(s_entries[j].name, name))
                id = s_entries[j].id;
        loadScreen(id);
        ui_tick();
        pump(disp, 100);
        screens_dial_init();
        screens_dial_step(1);
        pump(disp, 100);
        lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
        lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        if (!buf) {
            fprintf(stderr, "dial: snapshot failed\n");
            return 1;
        }
        char path[96];
        snprintf(path, sizeof path, "out/%s_dial.png", name);
        int rc = write_png(path, buf);
        lv_draw_buf_destroy(buf);
        return rc;
    }
#endif

    /* --rolltest: verify the HH:MM PAIR ROLL (odometer, the settings
     * roller's motion). Load the home screen (the shared clock widget
     * shows 12:35 from the stub), then change the minute to 12:36
     * through the REAL var + tick path and capture: the MM pair should
     * roll UP (the old "35" exits the top of its one-line window as
     * "36" enters from the bottom, as one block), then back DOWN.
     * Five frames: settled before, up mid-roll (~150 ms), up settled,
     * down mid-roll, down settled. */
    if (argc > 1 && !strcmp(argv[1], "--rolltest")) {
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        pump(disp, 300); /* let the initial digits settle (no roll at rest) */
        lv_obj_t *root = ((lv_obj_t **)&objects)[SCREEN_ID_MODEM_SLEEP - 1];
        lv_draw_buf_t *b1 = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        write_png("out/roll_before.png", b1);
        lv_draw_buf_destroy(b1);
        /* Trigger the minute change through the real var + tick path:
         * 12:35 -> 12:36 (the "5" rolls UP into "6", ease-out). */
        set_var_clock_label("12:36");
        tick_user_widget_clock(17);
        pump(disp, 150); /* mid-roll: the "5" sliding up out of the window */
        lv_draw_buf_t *b2 = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        write_png("out/roll_up_mid.png", b2);
        lv_draw_buf_destroy(b2);
        pump(disp, 300); /* let the up roll finish + settle */
        lv_draw_buf_t *b3 = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        write_png("out/roll_up_after.png", b3);
        lv_draw_buf_destroy(b3);
        /* Now the reverse direction: 12:36 -> 12:35 (the "6" rolls DOWN
         * into "5"). Verify the direction-aware scroll + that no
         * adjacent digit peeks into the one-line window. */
        set_var_clock_label("12:35");
        tick_user_widget_clock(17);
        pump(disp, 150); /* mid-roll: the "6" sliding down out of the window */
        lv_draw_buf_t *b4 = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        write_png("out/roll_down_mid.png", b4);
        lv_draw_buf_destroy(b4);
        pump(disp, 300); /* let the down roll finish + settle */
        lv_draw_buf_t *b5 = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        write_png("out/roll_down_after.png", b5);
        lv_draw_buf_destroy(b5);
        printf("[rolltest] before / up_mid / up_after / down_mid / down_after written\n");
        return 0;
    }

    /* --clockprobe: dump the REAL LVGL geometry of the HH:MM pair cells
     * (the full-screen container, the slot row, its three clipping
     * cells and the fixed-width labels inside them) at the current
     * UISIM_TIME -- authoritative numbers for the cell sizing work.
     * Usage:  UISIM_TIME=21:26 uisim --clockprobe */
    if (argc > 1 && !strcmp(argv[1], "--clockprobe")) {
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        pump(disp, 300); /* settle any attach-refresh roll */
        lv_obj_t *cont = ((lv_obj_t **)&objects)[17];
        lv_obj_t *row = ((lv_obj_t **)&objects)[18];
        lv_area_t a;
        lv_obj_get_coords(cont, &a);
        printf("CONT[17] x=%ld y=%ld w=%ld h=%ld\n", (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1),
               (long)(a.y2 - a.y1 + 1));
        lv_obj_get_coords(row, &a);
        printf("ROW[18]  x=%ld y=%ld w=%ld h=%ld padRow=%d padCol=%d\n", (long)a.x1, (long)a.y1,
               (long)(a.x2 - a.x1 + 1), (long)(a.y2 - a.y1 + 1), lv_obj_get_style_pad_row(row, 0),
               lv_obj_get_style_pad_column(row, 0));
        for (int32_t i = 0; i < lv_obj_get_child_count(row); i++) {
            lv_obj_t *cell = lv_obj_get_child(row, i);
            lv_obj_get_coords(cell, &a);
            printf("CELL[%ld] x=%ld y=%ld w=%ld h=%ld "
                   "padL=%d padR=%d padT=%d padB=%d\n",
                   (long)i, (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1), (long)(a.y2 - a.y1 + 1),
                   lv_obj_get_style_pad_left(cell, 0), lv_obj_get_style_pad_right(cell, 0),
                   lv_obj_get_style_pad_top(cell, 0), lv_obj_get_style_pad_bottom(cell, 0));
            for (int32_t j = 0; j < lv_obj_get_child_count(cell); j++) {
                lv_obj_t *lab = lv_obj_get_child(cell, j);
                lv_obj_get_coords(lab, &a);
                const lv_font_t *f = lv_obj_get_style_text_font(lab, 0);
                printf("  LAB[%ld][%ld] x=%ld y=%ld w=%ld h=%ld "
                       "contW=%ld contH=%ld transY=%d align=%d "
                       "text=\"%s\" lh=%d\n",
                       (long)i, (long)j, (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1), (long)(a.y2 - a.y1 + 1),
                       (long)lv_obj_get_content_width(lab), (long)lv_obj_get_content_height(lab),
                       lv_obj_get_style_translate_y(lab, 0), (int)lv_obj_get_style_text_align(lab, 0),
                       lv_label_get_text(lab), f ? (int)lv_font_get_line_height(f) : -1);
            }
        }
        /* The next-alarm ROW (centre-anchored, 2026-11-05 v2):
         * objects[19] is the alarm TIME label, inside the row's RIGHT
         * half-box; the ROW (s_alarm_prefix, a file-static) is its
         * grandparent. The row box must match the time row's width
         * (centre = the colon); the two half-boxes meet at the centre,
         * the prefix's right edge and the time's left edge pinned
         * there. */
        lv_obj_t *arow = lv_obj_get_parent(lv_obj_get_parent(((lv_obj_t **)&objects)[19]));
        lv_obj_get_coords(arow, &a);
        printf("AROW  x=%ld y=%ld w=%ld h=%ld\n", (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1),
               (long)(a.y2 - a.y1 + 1));
        for (int32_t i = 0; i < lv_obj_get_child_count(arow); i++) {
            lv_obj_t *half = lv_obj_get_child(arow, i);
            lv_obj_get_coords(half, &a);
            printf("  HALF[%ld] x=%ld y=%ld w=%ld h=%ld\n", (long)i, (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1),
                   (long)(a.y2 - a.y1 + 1));
            for (int32_t j = 0; j < lv_obj_get_child_count(half); j++) {
                lv_obj_t *lab = lv_obj_get_child(half, j);
                lv_obj_get_coords(lab, &a);
                printf("    ALAB[%ld][%ld] x=%ld y=%ld w=%ld h=%ld contW=%ld contH=%ld padL=%d padR=%d "
                       "text=\"%s\"\n",
                       (long)i, (long)j, (long)a.x1, (long)a.y1, (long)(a.x2 - a.x1 + 1), (long)(a.y2 - a.y1 + 1),
                       (long)lv_obj_get_content_width(lab), (long)lv_obj_get_content_height(lab),
                       lv_obj_get_style_pad_left(lab, 0), lv_obj_get_style_pad_right(lab, 0), lv_label_get_text(lab));
            }
        }
        return 0;
    }

    /* --reveal: prove the device's reveal strip write matches the
     * trailing lv_refr_now flush. The given screen (default hub) gets
     * the root rotation the device's orientation pipeline applies; the
     * snapshot (screen-local, unrotated -- lv_snapshot ignores the
     * transform) is rotated in place exactly as concentric_reveal
     * does, then compared pixel-by-pixel with the front buffer of the
     * real rotated flush. Both are LVGL-native RGB565 (the panel
     * byte-swap is self-inverse and cancels). Usage: --reveal <q>
     * [screen] with q = 0..3 clockwise 90-degree steps (1 = the base
     * +90). */
    if (argc > 1 && !strcmp(argv[1], "--reveal")) {
        const int q = (argc > 2) ? atoi(argv[2]) : 1;
        enum ScreensEnum id = SCREEN_ID_SETTINGS_HUB;
        if (argc > 3) {
            for (size_t j = 0; j < sizeof s_entries / sizeof s_entries[0]; j++)
                if (!strcmp(s_entries[j].name, argv[3]))
                    id = s_entries[j].id;
        }
        loadScreen(id);
        ui_tick();
        pump(disp, 100);
        lv_obj_t *root = ((lv_obj_t **)&objects)[id - 1];
        lv_obj_set_style_transform_pivot_x(root, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(root, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(root, 900 * q, 0);
        pump(disp, 100);
        lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
        if (!buf) {
            fprintf(stderr, "reveal: snapshot failed\n");
            return 1;
        }
        if (buf->header.w != W || buf->header.h != H || buf->header.stride != W * 2) {
            fprintf(stderr, "reveal: snapshot %ux%u stride %u unexpected\n", buf->header.w, buf->header.h,
                    buf->header.stride);
            return 1;
        }
        /* The real flush renders the rotated screen into s_fb. */
        lv_refr_now(disp);
        /* Dump the RAW snapshot (before the in-place rotation) so the
         * transform behaviour of lv_snapshot_take is visible directly. */
        char rp[64];
        snprintf(rp, sizeof rp, "out/snap_raw_q%d.png", q);
        (void)write_png(rp, buf);
        /* The reveal's in-place rotation (the byte-swap is self-inverse
         * and applies to both sides, so compare pre-swap). */
        reveal_rotate_cw90((uint16_t *)buf->data, W, q);
        uint32_t bad = 0, tot = 0;
        int32_t maxd = 0;
        for (int i = 0; i < W * H; i++) {
            int32_t d = (int32_t)buf->data[2 * i] - (int32_t)s_fb[2 * i];
            int32_t d2 = (int32_t)buf->data[2 * i + 1] - (int32_t)s_fb[2 * i + 1];
            int32_t a = d < 0 ? -d : d;
            int32_t b = d2 < 0 ? -d2 : d2;
            tot += (a > b ? a : b);
            if (b > a)
                a = b;
            if (a > maxd)
                maxd = a;
            if (d != 0 || d2 != 0)
                bad++;
        }
        printf("reveal q=%d: %u/%d px differ, max byte diff %d, mean %u\n", q, bad, W * H, maxd, tot / (W * H));
        /* Save the flush front buffer and a diff map (white = differs,
         * grey = the rotated snapshot) for localization. */
        static uint8_t *rgb2, *diffm;
        if (!rgb2) {
            rgb2 = malloc((size_t)W * H * 3);
            diffm = malloc((size_t)W * H * 3);
        }
        for (int i = 0; i < W * H; i++) {
            uint16_t p = ((const uint16_t *)s_fb)[i];
            rgb2[i * 3] = (uint8_t)((p >> 11) * 255 / 31);
            rgb2[i * 3 + 1] = (uint8_t)((p >> 5 & 0x3F) * 255 / 63);
            rgb2[i * 3 + 2] = (uint8_t)((p & 0x1F) * 255 / 31);
            uint16_t s = ((const uint16_t *)buf->data)[i];
            int d = 0;
            for (int k = 0; k < 2; k++) {
                int a = (int)buf->data[2 * i + k] - (int)s_fb[2 * i + k];
                if (a < 0)
                    a = -a;
                if (a > d)
                    d = a;
            }
            diffm[i * 3] = diffm[i * 3 + 1] = diffm[i * 3 + 2] = (d != 0) ? 0xFF : 0;
            diffm[i * 3 + 0] += 0; /* keep it pure: white = differs */
        }
        char fp[64];
        snprintf(fp, sizeof fp, "out/flush_q%d.png", q);
        (void)stbi_write_png(fp, W, H, 3, rgb2, W * 3);
        printf("wrote %s\n", fp);
        (void)stbi_write_png("out/reveal_diff.png", W, H, 3, diffm, W * 3);
        int rc = write_png("out/reveal_rotated.png", buf);
        lv_draw_buf_destroy(buf);
        return rc;
    }

    /* --wheel: scripted capture of the circular wheel keyboard. Drives the
     * REAL flow (hub icon -> WiFi list -> row tap = SSID prefill + wheel)
     * with a scripted pointer indev, then snapshots the rest state, the
     * touched-down state (outer frame revealed, 'h' under the finger) and
     * all four register modes. */
    if (argc > 1 && !strcmp(argv[1], "--wheel")) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, wheel_indev_read);
        s_wheel_indev = indev;

        /* rot mode (--wheel rot): the wheel root carries the device
         * orientation pipeline's +90-degree transform (the base offset
         * the device always applies) and the snapshots are the display
         * front buffer (the real rotated flush, transform layer
         * included). Every scripted tap angle gets the same +90-degree
         * display shift (a point at object angle a lands at display
         * angle a+90). */
        int rot = argc > 2 && !strcmp(argv[2], "rot");
        double rot_shift = rot ? 90.0 : 0.0;
        int (*snap)(lv_display_t *, const char *) = rot ? wheel_snapshot_rot : wheel_snapshot;

        int fail = 0;
        wheel_enter(disp, indev);
        if (rot) {
            lv_obj_t *wroot = ((lv_obj_t **)&objects)[SCREEN_ID_SETTINGS_WIFI_PASSWORD - 1];
            lv_obj_set_style_transform_pivot_x(wroot, LV_PCT(50), 0);
            lv_obj_set_style_transform_pivot_y(wroot, LV_PCT(50), 0);
            lv_obj_set_style_transform_rotation(wroot, 900, 0);
            pump(disp, 100);
        }

        /* rest: the prefilled SSID, empty password (the "...." placeholder). */
        fail |= snap(disp, "rest");

        /* touched (v2): press directly in the ring band (r 140..233) at
         * slot 7 ('h', the "gh" sector) and hold -- the pressed sector's
         * boxes are revealed, the exact box lit (inverted), the preview
         * letter at the centre. The radii are the panel-scaled wheel's
         * (ui_px.h); the mid-band radius 188 is the wheel's own middle. */
        lv_point_t p1;
        wheel_point_deg(wheel_slot_deg(7) + rot_shift, UI_PX(188), &p1);
        wheel_press_at(disp, p1.x, p1.y);
        fail |= snap(disp, "touched");
        wheel_release_at(disp, p1.x, p1.y); /* commits the 'h' */

        /* all four register modes: the 240-degree arc's four 60-degree
         * keys at LVGL 180/240/300/360, r 114; a press snaps to the
         * exact key sector under the finger. */
        const char *mode_names[4] = {"abc", "ABC", "123", "sym"};
        for (int k = 0; k < 4; k++) {
            lv_point_t pk;
            wheel_point_deg(180.0 + 60.0 * k + rot_shift, UI_PX(114), &pk);
            wheel_tap(disp, pk.x, pk.y);
            fail |= snap(disp, mode_names[k]);
        }

        /* Commit (v2): a tap on the house circle-xmark key at the bottom
         * (LVGL 60, r 114) -- the stub's wifi_credentials_write +
         * action_start_wifi_sync swaps to the transient WiFi screen, so
         * the shot verifies the save -> connect hand-off (the entered
         * letter was the 'h' from the sweep). A centre tap only
         * backspaces -- it would lose the 'h'. */
        {
            lv_point_t pc;
            wheel_point_deg(60.0 + rot_shift, UI_PX(114), &pc);
            wheel_tap(disp, pc.x, pc.y);
        }
        pump(disp, 100);

        /* Back path (v2): v2 has no in-ring exit -- a 400 ms LONG PRESS
         * on the centre (a plain tap backspaces) is the only way back to
         * the WiFi list. Re-enter the wheel (fresh prefill), hold the
         * centre past the long-press threshold (the hold re-drives the
         * indev state machine, see wheel_hold), and snapshot the list;
         * release OFF any row so the release cannot re-tap one. */
        wheel_enter(disp, indev);
        wheel_press_at(disp, UI_PANEL_PX / 2, UI_PANEL_PX / 2);
        wheel_hold(disp, UI_PANEL_PX / 2, UI_PANEL_PX / 2, 600); /* LONG_PRESSED at 400 ms -> list */
        {
            /* The long press already swapped the screen -- capture the
             * ACTIVE one (the WiFi list), not the wheel's root, which
             * is now only the background screen. */
            lv_obj_t *root = lv_scr_act();
            lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
            if (!buf) {
                fprintf(stderr, "wheel snapshot failed: back\n");
                fail = 1;
            } else {
                if (write_png("out/wheel_back.png", buf))
                    fail = 1;
                lv_draw_buf_destroy(buf);
            }
        }
        wheel_release_at(disp, 5, 5);
        pump(disp, 100);
        {
            lv_obj_t *root = ((lv_obj_t **)&objects)[SCREEN_ID_WIFI - 1];
            lv_draw_buf_t *buf = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565);
            if (!buf) {
                fprintf(stderr, "wheel snapshot failed: committed\n");
                fail = 1;
            } else {
                if (write_png("out/wheel_committed.png", buf))
                    fail = 1;
                lv_draw_buf_destroy(buf);
            }
        }

        lv_indev_delete(indev);
        return fail;
    }

    /* --tour: the docs promo film (the S3 466 set only - docs/gen_media.py
     * pins the frame size like the fade). */
    if (argc > 1 && !strcmp(argv[1], "--tour")) {
        if (W != 466) {
            fprintf(stderr, "--tour is the S3 466 film (run build.sh run --tour without --panel)\n");
            return 1;
        }
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, wheel_indev_read);
        s_wheel_indev = indev;
        /* The home screen at the sim's seed (the film's opening). */
        loadScreen(SCREEN_ID_MODEM_SLEEP);
        ui_tick();
        pump(disp, 300); /* the attach roll settles before the first frame */
        int rc = tour_run(disp);
        lv_indev_delete(indev);
        if (!rc)
            printf("tour: %d frames written (out/tour_*.png)\n", s_tour_n);
        return rc;
    }

    int fail = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            int found = 0;
            for (size_t j = 0; j < sizeof s_entries / sizeof s_entries[0]; j++)
                if (!strcmp(s_entries[j].name, argv[i])) {
                    found = 1;
                    if (snapshot_one(disp, s_entries[j].id))
                        fail = 1;
                }
            if (!found) {
                fprintf(stderr, "unknown screen '%s'\n", argv[i]);
                fail = 1;
            }
        }
    } else {
        for (size_t j = 0; j < sizeof s_entries / sizeof s_entries[0]; j++)
            if (snapshot_one(disp, s_entries[j].id))
                fail = 1;
    }
    return fail;
}
