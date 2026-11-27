# Power posture for the knob (K7e): idle dim -> deep sleep, EXT0 dial-A
# wake (+EXT1 CST816S INT tap wake). Talks to the backlight + encoder
# shim and to the clock for the pre-sleep epoch snapshot (the board has
# no RTC; wakes arm the static moon from NVS). On the shared-UI path it
# ALSO drives the family sleep screens (ui: the dim rung loads the
# light-sleep screen, the deep rung the deep-sleep screen as the last
# lit frame) -- hence the ui require (loadScreen / ui_tick / screens.h /
# moon_ui.h); the LVGL mutex is passed in from main (knob_ui already
# REQUIRES knob_power, so requiring knob_ui back would cycle). The
# legacy own-screen path (KNOB_UI_ENABLE=n) keeps the in-place dim.
# Gated on KNOB_BOARD (promoted into the early pass by the root CMakeLists):
# the real deep-sleep + EXT0/EXT1/RTC-TIMER wake posture is knob hardware;
# off the knob the component registers with no SRCS.
if(KNOB_BOARD)
idf_component_register(SRCS "knob_power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES knob_board esp_hw_support clock ui)
else()
idf_component_register()
endif()
