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
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "screens.h"
#include "stb_image_write.h"
#include "ui.h"

#define W 466
#define H 466

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
};

static int snapshot_one(lv_display_t *disp, enum ScreensEnum id) {
    const char *name = NULL;
    for (size_t i = 0; i < sizeof s_entries / sizeof s_entries[0]; i++)
        if (s_entries[i].id == id)
            name = s_entries[i].name;
    if (!name)
        return 1;

    loadScreen(id);
    ui_tick();       /* seed the screen's widgets from the stubbed state */
    pump(disp, 100); /* let pending invalidations settle */
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

    lv_tick_set_cb(tick_cb);
    lv_init();
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof s_fb, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_default(disp);

    create_screens(); /* real device code: theme + all 12 screens */
    pump(disp, 200);

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
