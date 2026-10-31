# S3-only component: unregistered on other targets (the P4 build uses the
# registry waveshare/esp32_p4_wifi6_touch_lcd_xc instead; an unregistered
# component is harmlessly skipped — its Kconfig is not processed and no
# component REQUIRES it there).
if(IDF_TARGET STREQUAL "esp32s3")
idf_component_register(
    SRCS "esp32_s3_touch_amoled_1_75.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio esp_lcd
    PRIV_REQUIRES esp_timer spiffs esp_psram fatfs usb
)
endif()
