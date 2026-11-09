# The knob firmware (Waveshare ESP32-S3-Knob-Touch-LCD-1.8, the ESP32-S3R8
# app side): the THIRD board of the moon-clock family — the clock's app
# behaviour with the dial, battery and real deep sleep added. A sibling
# project of the clock: its own build dir (-B build/knob), its own
# dependency lock, its own sdkconfig — but it links the clock's SHARED
# components through EXTRA_COMPONENT_DIRS (below), and a knob-owned board/
# imu shim answers the board-specific symbol surface exactly the way the
# P4 port answered it with its BSP. The vendored AMOLED BSP, the shared
# board shim and the IMU component are NOT added (IDF 5.5.1 excludes by
# name only, so we simply never add the directories — the shims carry the
# component names "board"/"imu" that the shared code REQUIRESs).
#
# Build (sequential with the clock builds — never parallel):
#   idf.py -C knob -B build/knob build
#   idf.py -C knob -B build/knob flash -p <port>   # esptool chip_id first!

cmake_minimum_required(VERSION 3.16)

# Shared-code CMake branch flag for the components/ (the ui component's
# SRCS/REQUIRES/fonts tier). A PROJECT variable, not a CONFIG_ symbol: the
# component manager resolves requirements BEFORE Kconfig runs, and CONFIG_
# variables are unset in that first pass.
set(KNOB_BOARD ON)

# Shared clock-family components, added as individual component
# directories (NOT the parent components/ — that would register
# board/imu/esp32_s3_touch_amoled_1_75 and collide with the knob shims).
# P0: clock (time base + scheduler + settings_store), wifi (credentials +
# scan), moon (the single core provider; knob_moon keeps only glue).
# P1: ui (the shared screens; the knob branch of its CMakeLists drops the
# four AMOLED/board-coupled TUs and the fonts tier switches to
# fonts_knob/). It is added unconditionally; the KNOB_UI_ENABLE umbrella
# (knob_ui's Kconfig) decides whether the seam links it into the image —
# with the switch off the shared objects sit unreferenced in the static
# lib and never load.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/clock"
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/wifi"
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/moon"
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/ui")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component manager's lock lives next to this file, board-suffixed and
# gitignored like the P4's dependencies.lock.p4 (the tracked root lock
# belongs to the clock's S3 build).
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.knob")

project(knob)

# Moon asset guards (K7b; the clock root CMakeLists' shape, MINUS the
# MOON_ALBEDO_W Kconfig check — the knob has no Kconfig mirror; its single
# truth is the pipeline-written manifest + header, constants.TARGETS["knob"]).
# Fails fast at configure, not at flash/boot: the blob must exist and fit the
# moon_assets row of THIS project's partition table (both mismatches
# historically surfaced as obscure esptool/PSRAM failures on the clock).
set(MOON_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../assets/images/moon/out/knob)
set(MOON_ASSETS_MANIFEST ${MOON_ASSET_DIR}/moon_assets_manifest.json)
set(MOON_ASSETS_BIN ${MOON_ASSET_DIR}/moon_assets.bin)
if(NOT EXISTS ${MOON_ASSETS_MANIFEST})
    message(FATAL_ERROR
        "Missing knob moon assets (${MOON_ASSETS_MANIFEST}). Generate them "
        "from the repo root: cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob")
endif()
file(READ ${MOON_ASSETS_MANIFEST} MOON_ASSET_MANIFEST)
# Single-line JSON (the pipeline writes it that way). CMake MATCHES is
# ECMAScript regex; the compact manifest's only whitespace is after colons.
if(NOT MOON_ASSET_MANIFEST MATCHES "\"combined_bytes\" *: *([0-9]+)")
    message(FATAL_ERROR "Malformed manifest ${MOON_ASSETS_MANIFEST} (no combined_bytes): "
        "regenerate with cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob")
endif()
math(EXPR MOON_ASSET_COMBINED ${CMAKE_MATCH_1})
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv MOON_PART_CSV_CONTENT)
# The comma right after "moon_assets" (no space in the data row) keeps this
# from matching the comment header, which spells it "moon_assets (size)"
# without a comma.
if(MOON_PART_CSV_CONTENT MATCHES "moon_assets *,[^\n]*, *([0-9]+)K")
    math(EXPR MOON_PART_BYTES "${CMAKE_MATCH_1} * 1024")
else()
    message(FATAL_ERROR "No 'moon_assets' partition row (size in K) found in "
        "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv -- the asset pipeline needs it to verify the blob fits.")
endif()
if(MOON_ASSET_COMBINED GREATER MOON_PART_BYTES)
    math(EXPR MOON_PART_NEED_KIB "(${MOON_ASSET_COMBINED} + 1023) / 1024")
    message(FATAL_ERROR "Knob moon assets ${MOON_ASSET_COMBINED} B do not fit the moon_assets partition "
        "(${MOON_PART_BYTES} B in partitions.csv); they need >= ${MOON_PART_NEED_KIB}K.")
endif()

# Bake knob/spiffs/ into the `storage` partition; FLASH_IN_PROJECT makes
# `idf.py flash` carry it (the clock's root CMakeLists pattern). The real
# wifi_credentials.txt is gitignored (the clock's filename — the shared
# components/wifi reads it); only the .example.txt is tracked.
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

# Moon phase assets (K7): flash the combined albedo + relief DEM blob into
# the moon_assets data partition (raw region, read at boot through the
# ported moon_esp_assets.c backend). The manifest guard above already ran,
# so a missing blob here means a hand-deleted .bin beside a live manifest.
if(EXISTS ${MOON_ASSETS_BIN})
    esptool_py_flash_to_partition(flash "moon_assets" ${MOON_ASSETS_BIN})
else()
    message(WARNING "knob moon assets not found at ${MOON_ASSETS_BIN} -- "
        "run 'cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob' "
        "(the build succeeds, but the moon_assets partition is not flashed).")
endif()
