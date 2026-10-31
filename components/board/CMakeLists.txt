# Board shim: one stable include surface (include/board_display.h) over the
# two BSPs. The active BSP is selected by the build target — the P4 BSP is a
# registry dep gated to esp32p4 (main/idf_component.yml) and the vendored S3
# BSP registers only on esp32s3 (its own CMakeLists). CONFIG_BOARD_* is not
# in scope during component registration (kconfig runs later), so the target
# keys the choice; the root CMakeLists FATAL_ERRORs on a target/board
# mismatch anyway.
if(IDF_TARGET STREQUAL "esp32p4")
    set(BSP_COMPONENT waveshare__esp32_p4_wifi6_touch_lcd_xc)
else()
    set(BSP_COMPONENT esp32_s3_touch_amoled_1_75)
endif()

# esp_lcd / esp_driver_i2c / esp_io_expander: board_display.h includes
# their headers directly. (The P4 BSP keeps esp_lcd private, so the
# shim must take it publicly; on the S3 the vendored BSP already
# exports it — requiring it again is a no-op there.)
idf_component_register(
    SRCS "board_display.c"
    INCLUDE_DIRS "include"
    REQUIRES ${BSP_COMPONENT} esp_lcd esp_driver_i2c esp_io_expander
)
