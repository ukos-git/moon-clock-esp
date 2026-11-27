# Power posture for the knob (K7e): idle dim -> deep sleep, EXT0 dial-A
# wake (+EXT1 CST816S INT tap wake). Talks to the backlight + encoder
# shim (no lvgl -- the view reports activity/hold to US, not the other
# way round) and to knob_time for the pre-sleep epoch snapshot (the
# board has no RTC; wakes arm the static moon from NVS).
# Gated on KNOB_BOARD (promoted into the early pass by the root CMakeLists):
# the real deep-sleep + EXT0/EXT1/RTC-TIMER wake posture is knob hardware;
# off the knob the component registers with no SRCS.
if(KNOB_BOARD)
idf_component_register(SRCS "knob_power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES knob_board esp_hw_support clock)
else()
idf_component_register()
endif()
