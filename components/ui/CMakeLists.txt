file(GLOB_RECURSE IMAGE_SOURCES "images/*.c")
# The font bitmaps are per-panel: fonts/ is the S3's 466 px set (96/48/32),
# fonts_p4/ the P4's 720 px set (148/74/50) regenerated at the panel ratio
# from the SAME source Opts — the lv_font_t symbol names are identical, so
# fonts.h and every consumer are unchanged.
if(CONFIG_BOARD_P4)
  set(FONT_DIR "fonts_p4")
else()
  set(FONT_DIR "fonts")
endif()
file(GLOB_RECURSE FONT_SOURCES "${FONT_DIR}/*.c")

idf_component_register(
  SRCS
    "ui.c"
    "screens.c"
    "images.c"
    "styles.c"
    "actions.c"
    "vars.c"
    "moon_ui.c"
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
  REQUIRES driver esp_lcd lvgl clock heap log freertos esp_timer esp_partition moon imu wifi board
  INCLUDE_DIRS "./")
