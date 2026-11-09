# Power posture for the knob (K7e): idle dim -> deep sleep, EXT0 dial-A
# wake (+EXT1 CST816S INT tap wake). Talks to the backlight + encoder
# shim (no lvgl -- the view reports activity/hold to US, not the other
# way round) and to knob_time for the pre-sleep epoch snapshot (the
# board has no RTC; wakes arm the static moon from NVS).
idf_component_register(SRCS "knob_power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES knob_board esp_hw_support clock)
