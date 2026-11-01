idf_component_register(
    SRCS "clock.c" "clock_timer.c" "clock_minute.c" "clock_interrupt.c" "clock_alarm.c" "clock_alarm_scheduler.c" "clock_cb.c"
        "clock_idle.c" "clock_sntp.c" "clock_tz.c" "settings_store.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_io_expander board wifi esp_netif lwip freertos spiffs esp_timer nvs_flash
)
