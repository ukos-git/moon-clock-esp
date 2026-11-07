idf_component_register(SRCS "knob_board_display.c" "knob_board_touch.c" "knob_board_encoder.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi esp_timer
                       PRIV_REQUIRES espressif__esp_lcd_sh8601)
