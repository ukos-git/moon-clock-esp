# Knob board shim: the third-board answer to the shared components'
# "board" REQUIRES + BSP header surface (the P4 port answered the same
# surface with its registry BSP). Carries the BOARD Kconfig choice so
# CONFIG_BOARD_KNOB exists for the shared components' CMake/Kconfig
# branches. lvgl is REQUIRESd PUBLICLY because components/clock's
# clock_cb.h/clock_idle.h include lvgl.h and reach it transitively
# through "board" on the real boards (BSP -> LVGL port).
# pcf85063a: components/clock's clock.c includes "pcf85063a.h" even on the
# non-PCF build (the driver's header for the s_rtc_dev global); on the
# clock boards it arrives through the BSP's public deps, so the shim
# carries it the same way. esp_io_expander: clock REQUIRESes it by name.
idf_component_register(
    SRCS "board_display.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl esp_lcd esp_driver_i2c esp_io_expander pcf85063a
)
