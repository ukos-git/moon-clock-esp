file(GLOB_RECURSE IMAGE_SOURCES "images/*.c")
# The font bitmaps are per-panel: fonts/ is the S3's 466 px set (96/48/32),
# fonts_p4/ the P4's 720 px set (148/74/50) regenerated at the panel ratio
# from the SAME source Opts — the lv_font_t symbol names are identical, so
# fonts.h and every consumer are unchanged. fonts_knob/ is the knob's 360 px
# tier (74/37/25, same Opts scaled by 360/466). The one exception to
# "SAME source Opts": the regular icon face carries the clock 61463
# (U+F017) only in the 720/360 tiers -- it is the P4 + knob hub ring's
# 8th entry (screens.c's #if CONFIG_BOARD_P4 || CONFIG_BOARD_KNOB), so
# the knob inherited that glyph from the P4 bake and the S3 (the icon's
# board gate) has no need of it. The other six faces are
# charset-identical across tiers; docs/tools/gen_perf_glyphs.py asserts
# all of it at figure time.
# The branch keys are PROJECT variables (KNOB_BOARD is promoted into the
# early pass by the root CMakeLists' idf_build_set_property), deliberately
# NOT CONFIG_ symbols:
# the component manager's early requirements pass (build.cmake's
# __component_get_requirements -> cmake -P component_get_requirements.cmake)
# evaluates this file with NEITHER project variables NOR CONFIG_ symbols,
# and CONFIG_BOARD_P4 reads false in that pass (harmless: SRCS are only
# consumed by the second, target-owning pass, which sees the full scope).
if(KNOB_BOARD)
  set(FONT_DIR "fonts_knob")
elseif(CONFIG_BOARD_P4)
  set(FONT_DIR "fonts_p4")
else()
  set(FONT_DIR "fonts")
endif()
file(GLOB_RECURSE FONT_SOURCES "${FONT_DIR}/*.c")

# The knob (third board of the family, P1): the shared screens/vars/actions
# compile UNCHANGED; the four AMOLED/board-coupled TUs drop out and the knob
# answers their public headers from its own components (knob_moon_render.c
# implements moon_ui.h over the knob_ui producer core; a stub implements
# alarm_audio.h -- the knob is a codec-less silent board). demo.c and
# moon_tfcache.c have no knob consumers (their call sites are Kconfig-guarded
# or live in the excluded TUs).
set(UI_CORE_SRCS "ui.c" "screens.c" "images.c" "styles.c" "actions.c" "vars.c")
set(UI_BOARD_SRCS "alarm_audio.c" "demo.c" "moon_ui.c" "moon_tfcache.c")
if(KNOB_BOARD)
  set(UI_BOARD_SRCS "")
endif()

# REQUIRES/PRIV_REQUIRES are spelled out UNCONDITIONALLY on purpose (the
# same early-pass rule as above): every name they carry must resolve in all
# three boards. esp_codec_dev is satisfied on the knob by the real driver,
# declared component-locally in components/knob_board/idf_component.yml
# (knob_board is EXCLUDED from the S3/P4 builds, so its yml — and this
# dep — is never evaluated there and stays out of the S3/P4 locks; unused
# on the codec-less board, so its objects never link), and on the clocks by
# the vendored BSP's public exposure; the other names are IDF
# core components or the manager-vendored esp_new_jpeg (declared in
# idf_component.yml, so the knob lock file carries it too).
idf_component_register(
  SRCS
    ${UI_CORE_SRCS}
    ${UI_BOARD_SRCS}
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
  # esp_codec_dev: the alarm ring + sound selection (both clock boards --
  # each carries the ES8311 speaker, and both BSPs expose the component
  # publicly, so it resolves on either target). esp_driver_i2s: the I2S
  # std headers the codec's sample-info types lean on (BSP public
  # REQUIRES, taken directly for the alarm_audio.c include surface).
  REQUIRES driver esp_lcd lvgl clock heap log freertos esp_timer esp_partition
           moon imu wifi board esp_codec_dev esp_driver_i2s
  # espressif__esp_new_jpeg: the gray JPEG codec of the TF sweep cache
  # (moon_tfcache.c). Declared at the same range esp_lv_decoder pulls it
  # at (1.*), so the solver keeps the single vendored 1.0.2. PRIVATE:
  # only moon_tfcache.c includes its headers. fatfs/esp_driver_sdspi/
  # esp_driver_spi: the S3's SPI-host SD mount (card_mount() calls
  # esp_vfs_fat_sdspi_mount directly -- the BSP's 1-bit SDMMC mapping of
  # the SPI-wired socket collapses under load); the P4 still mounts via
  # the BSP's bsp_sdcard_mount(). All knob-unused (the TUs that use them
  # are excluded there; the include paths are harmless).
  PRIV_REQUIRES espressif__esp_new_jpeg fatfs esp_driver_sdspi esp_driver_spi
  INCLUDE_DIRS "./")
# The lv_font_conv >= 1.5 generated font files include "lvgl/lvgl.h"
# (a path this project's include set does not carry) unless
# LV_LVGL_H_INCLUDE_SIMPLE is pre-defined -- then they include "lvgl.h",
# which resolves from the lvgl component root.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
