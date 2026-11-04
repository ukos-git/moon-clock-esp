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
 *   --rottest / --reveal <q> [screen] / --wheel: the other verification
 *               hooks (see their blocks below).
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

/* Drive the real flow to the wheel: settings-hub WiFi icon (on the icon
 * ring) -> the WiFi list -> AP row 0 (y UI_PX(185)) = SSID prefill + the
 * wheel. The row handler prefills the (stub) first AP's SSID.
 *
 * The hub's ring (create_screen_settings_hub): n icons 360/n apart,
 * deep-sleep anchored at 180 deg, th = 180 + step*(i - (n-3)) degrees
 * clockwise from 12 o'clock, position (R sin th, -R cos th) about the
 * centre (R = UI_PX(117)). The WiFi entry's index differs per board (the
 * P4 ring opens with the clock icon at 12 o'clock), so mirror it here. */
static void wheel_hub_wifi_icon(int32_t *x, int32_t *y) {
    int n;
    int wifi_i;
#if defined(CONFIG_BOARD_P4)
    n = 6;
    wifi_i = 4;
#else
    n = 5;
    wifi_i = 3;
#endif
    const float step = 360.0f / (float)n;
    const float deep = (float)n - 3.0f;
    const float th = ((180.0f + step * ((float)wifi_i - deep)) * 3.14159265f) / 180.0f;
    const int32_t R = UI_PX(117);
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
    /* Tick the WiFi list once so the rows render (their text is what
     * gives the labels content size -- tapping before the render hits
     * zero-size labels and misses). */
    ui_tick();
    pump(disp, 100);
    wheel_tap(disp, UI_PANEL_PX / 2, UI_PX(185));
    pump(disp, 100);
    (void)indev;
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

        int fail = 0;
        wheel_enter(disp, indev);

        /* rest: the prefilled SSID, empty password (the "...." placeholder). */
        fail |= wheel_snapshot(disp, "rest");

        /* touched (v2): press directly in the ring band (r 140..233) at
         * slot 7 ('h', the "gh" sector) and hold -- the pressed sector's
         * boxes are revealed, the exact box lit (inverted), the preview
         * letter at the centre. The radii are the panel-scaled wheel's
         * (ui_px.h); the mid-band radius 188 is the wheel's own middle. */
        lv_point_t p1;
        wheel_point_deg(wheel_slot_deg(7), UI_PX(188), &p1);
        wheel_press_at(disp, p1.x, p1.y);
        fail |= wheel_snapshot(disp, "touched");
        wheel_release_at(disp, p1.x, p1.y); /* commits the 'h' */

        /* all four register modes: the 240-degree arc's four 60-degree
         * keys at LVGL 180/240/300/360, r 114; a press snaps to the
         * exact key sector under the finger. */
        const char *mode_names[4] = {"abc", "ABC", "123", "sym"};
        for (int k = 0; k < 4; k++) {
            lv_point_t pk;
            wheel_point_deg(180.0 + 60.0 * k, UI_PX(114), &pk);
            wheel_tap(disp, pk.x, pk.y);
            fail |= wheel_snapshot(disp, mode_names[k]);
        }

        /* Commit (v2): a tap on the house circle-xmark key at the bottom
         * (LVGL 60, r 114) -- the stub's wifi_credentials_write +
         * action_start_wifi_sync swaps to the transient WiFi screen, so
         * the shot verifies the save -> connect hand-off (the entered
         * letter was the 'h' from the sweep). A centre tap only
         * backspaces -- it would lose the 'h'. */
        {
            lv_point_t pc;
            wheel_point_deg(60.0, UI_PX(114), &pc);
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
