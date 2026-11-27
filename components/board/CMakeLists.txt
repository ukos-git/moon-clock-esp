# Board shim: one stable include surface (include/board_display.h) over the
# three boards. The active provider is selected by the build:
#   - knob (KNOB_BOARD, esp32s3 target): the knob_board component (its own
#     SH8601 QSPI bring-up) + the knob's 360 px bsp/ headers (knob_include).
#   - P4 (esp32p4 target): the registry Waveshare P4 BSP.
#   - S3 (esp32s3 target, default): the vendored Waveshare S3 BSP.
# KNOB_BOARD (promoted into the early pass by the root CMakeLists) must be
# checked BEFORE the target: the knob IS an esp32s3, so a target-only key
# would resolve the S3 BSP for it and leak the 466 px bsp/display.h. The
# knob build also EXCLUDEs the S3 BSP (root CMakeLists), so only the knob's
# own bsp/ headers are on the path there.
if(KNOB_BOARD)
    set(BSP_COMPONENT knob_board)
    set(BOARD_EXTRA_INCLUDES "knob_include")
elseif(IDF_TARGET STREQUAL "esp32p4")
    set(BSP_COMPONENT waveshare__esp32_p4_wifi6_touch_lcd_xc)
    set(BOARD_EXTRA_INCLUDES "")
else()
    set(BSP_COMPONENT esp32_s3_touch_amoled_1_75)
    set(BOARD_EXTRA_INCLUDES "")
endif()

# esp_lcd / esp_driver_i2c / esp_io_expander: board_display.h includes
# their headers directly. (The P4 BSP keeps esp_lcd private, so the shim
# must take it publicly; on the S3 the vendored BSP already exports it.)
# lvgl: the knob's knob_board carries no lvgl (its LVGL port is in knob_ui),
# so the knob's shared clock TUs (clock_cb.h / clock_idle.h include lvgl.h,
# reached transitively through "board") need board to export it there; on
# the clocks it is already in the graph via the BSP, so taking it directly
# is an idempotent no-op.
idf_component_register(
    SRCS "board_display.c"
    INCLUDE_DIRS "include" ${BOARD_EXTRA_INCLUDES}
    REQUIRES ${BSP_COMPONENT} lvgl esp_lcd esp_driver_i2c esp_io_expander
)
