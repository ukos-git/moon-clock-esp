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

/* Plain lv_scr_load (no fade) + one atomic whole-panel display of the
 * new screen (moon_ui_screen_switched, under the moon's buffer mutex).
 * The old FADE_IN screen-load re-invalidated the whole panel for the
 * 200 ms anim and LVGL's async refresh drained it in 25-row QSPI
 * strips -- the moon crept in top-to-bottom for over a second on every
 * screen switch, and the fade's pending invalidation also rode along
 * in the spin's atomic full-frame flushes, breaking their atomicity
 * (the "nasty top-to-bottom redraw artifacts" on wake from deep
 * sleep). */
void loadScreen(enum ScreensEnum screenId) {
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    lv_scr_load(screen);
    moon_ui_screen_switched();
}

void ui_init() {
    create_screens();
    loadScreen(SCREEN_ID_MAIN);
}

void ui_tick() { tick_screen(currentScreen); }

#endif
