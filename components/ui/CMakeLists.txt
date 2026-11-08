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
    "alarm_audio.c"
    "screens.c"
    "images.c"
    "styles.c"
    "actions.c"
    "vars.c"
    "moon_ui.c"
    "moon_tfcache.c"
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
  # esp_codec_dev: the alarm ring + sound selection (both boards — each
  # carries the ES8311 speaker, and both BSPs expose the component
  # publicly, so it resolves on either target). esp_driver_i2s: the I2S
  # std headers the codec's sample-info types lean on (BSP public
  # REQUIRES, taken directly for the alarm_audio.c include surface).
  REQUIRES driver esp_lcd lvgl clock heap log freertos esp_timer esp_partition moon imu wifi board esp_codec_dev esp_driver_i2s
  # espressif__esp_new_jpeg: the gray JPEG codec of the TF sweep cache
  # (moon_tfcache.c). Declared at the same range esp_lv_decoder pulls it
  # at (1.*), so the solver keeps the single vendored 1.0.2. PRIVATE:
  # only moon_tfcache.c includes its headers. fatfs/esp_driver_sdspi/
  # esp_driver_spi: the S3's SPI-host SD mount (card_mount() calls
  # esp_vfs_fat_sdspi_mount directly -- the BSP's 1-bit SDMMC mapping of
  # the SPI-wired socket collapses under load); the P4 still mounts via
  # the BSP's bsp_sdcard_mount().
  PRIV_REQUIRES espressif__esp_new_jpeg fatfs esp_driver_sdspi esp_driver_spi
  INCLUDE_DIRS "./")
# The lv_font_conv >= 1.5 generated font files include "lvgl/lvgl.h"
# (a path this project's include set does not carry) unless
# LV_LVGL_H_INCLUDE_SIMPLE is pre-defined -- then they include "lvgl.h",
# which resolves from the lvgl component root.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
