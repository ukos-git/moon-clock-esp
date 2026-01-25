idf_component_register(
    SRCS "clock.c" "clock_timer.c" "clock_interrupt.c" "clock_alarm.c" "clock_alarm_scheduler.c" "clock_cb.c" "clock_sntp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_io_expander waveshare__esp32_s3_touch_amoled_1_75 wifi esp_netif lwip freertos spiffs ui
)
