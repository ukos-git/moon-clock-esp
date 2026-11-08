# Power posture for the knob (K7e): idle dim -> deep sleep, EXT0 dial-A
# wake. Talks to the backlight + encoder shim only (no lvgl -- the view
# reports activity/hold to US, not the other way round).
idf_component_register(SRCS "knob_power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES knob_board esp_hw_support)
