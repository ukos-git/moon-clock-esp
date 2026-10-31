#if defined(EEZ_FOR_LVGL)
#include <eez/core/vars.h>
#endif

#include "actions.h"
#include "images.h"
#include "moon_ui.h"
#include "screens.h"
#include "ui.h"
#include "vars.h"

#if defined(EEZ_FOR_LVGL)

void ui_init() {
    eez_flow_init(assets, sizeof(assets), (lv_obj_t **)&objects, sizeof(objects), images, sizeof(images), actions);
}

void ui_tick() {
    eez_flow_tick();
    tick_screen(g_currentScreen);
}

#else

#include <string.h>

static int16_t currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&objects)[index];
}

/* Screen switch (UI_SCREEN_SWITCH_FADE): a switch that CROSSES the
 * moon / moon-less boundary (home <-> settings, the alarm screen)
 * dims the panel's OWN hardware brightness (the CO5300 0x51 command)
 * to black (moon_ui_fade_out, before lv_scr_load), loads, and ramps
 * it back (moon_ui_screen_switched) -- the transition writes no
 * pixels, so it costs nothing on the panel bus. Both-moon switches
 * (airplane / wifi / the sleep screens share the one moon image) and
 * both-moon-less switches (settings <-> settings) keep the plain
 * atomic whole-panel display -- a fade there would only dim the moon,
 * which is the same disk on both screens. History: the first
 * screen-load fade rode LVGL's async refresh and the whole panel
 * drained in 25-row QSPI strips (the moon crept in top-to-bottom for
 * over a second on every switch); the overlay fade made each step one
 * atomic whole-panel lv_refr_now, but every step still charged a
 * full-screen re-render + ~175 ms of strip traffic; the brightness
 * ramp needs no pixels at all. */
void loadScreen(enum ScreensEnum screenId) {
    if (currentScreen >= 0) {
        enum ScreensEnum oldId = (enum ScreensEnum)(currentScreen + 1);
        if (oldId != screenId && moon_ui_screen_is_moon(oldId) != moon_ui_screen_is_moon(screenId))
            moon_ui_fade_out(); /* self-guarded (LVGL task, fade on,
                                 * no pipeline owns the buffer) */
    }
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    lv_scr_load(screen);
    moon_ui_screen_switched();
}

void ui_init() {
    create_screens();
    /* The airplane (modem-sleep) screen is the home screen. The boot
     * sweep plays on it: at boot the clock is acquired by the blocking
     * boot sync (app_main, before the UI) and action_wake_screen lands
     * here. The transient WiFi screen is only shown while a sync is in
     * flight (wake / manual / hourly) and drops to airplane on
     * s_wifi_sync_done (ui_tick_timer_cb). */
    loadScreen(SCREEN_ID_MODEM_SLEEP);
}

void ui_tick() { tick_screen(currentScreen); }

enum ScreensEnum ui_current_screen(void) { return (enum ScreensEnum)(currentScreen + 1); }

#endif
