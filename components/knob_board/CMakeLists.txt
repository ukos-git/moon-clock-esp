# The knob's own panel/driver bring-up (SH8601 QSPI + CST816S + EC1 dial):
# compiled ONLY on the knob build. Off the knob (the S3/P4 clock targets)
# the component registers with no SRCS — the knob's objects are never built
# for the clocks, and the sh8601 driver's objects (fetched via the
# idf_component.yml for all targets) never link either. The SRCS/REQUIRES
# are spelled UNCONDITIONALLY inside the branch (the early requirements pass
# sees the
# branch's register; KNOB_BOARD is promoted into it by the root CMakeLists).
if(KNOB_BOARD)
idf_component_register(SRCS "knob_board_display.c" "knob_board_touch.c" "knob_board_encoder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi esp_timer
                       PRIV_REQUIRES espressif__esp_lcd_sh8601)
else()
idf_component_register()
endif()
