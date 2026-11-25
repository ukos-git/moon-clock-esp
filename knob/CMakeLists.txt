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

# calcver (the root CMakeLists' block, adapted): the .cz.toml and the
# git metadata live at the REPO ROOT (one level up — this sibling builds
# OUTSIDE the repo). Without the -DMOON_CALC_VER define the shared
# moon_movie reader falls back to "unknown" and rejects every baked
# movie at boot (the reader's calcver check), so the knob would never
# play its own movie.
set(MOON_CALC_VER "")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../.cz.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../.cz.toml)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../.cz.toml _cz_toml)
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([^\"]+)\"" _cz_match "${_cz_toml}")
    if(_cz_match)
        set(MOON_CALC_VER "${CMAKE_MATCH_1}")
    endif()
endif()
if(NOT MOON_CALC_VER)
    find_package(Git QUIET)
    if(GIT_FOUND)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/../.git/HEAD
            ${CMAKE_CURRENT_SOURCE_DIR}/../.git/packed-refs)
        execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
            OUTPUT_VARIABLE MOON_CALC_VER_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(MOON_CALC_VER_RAW)
            set(MOON_CALC_VER ${MOON_CALC_VER_RAW})
        endif()
    endif()
endif()
if(NOT MOON_CALC_VER)
    set(MOON_CALC_VER "unknown")
endif()
idf_build_set_property(COMPILE_OPTIONS "-DMOON_CALC_VER=\"${MOON_CALC_VER}\"" APPEND)
message(STATUS "moon calc version (boot-movie calcver check): ${MOON_CALC_VER}")

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
        "Missing knob moon assets (${MOON_ASSETS_MANIFEST}). Acquire + bake "
        "from the repo root: python3 assets/images/moon/get_assets.py --target knob "
        "(re-bake alone: cd assets/images/moon && .venv/bin/python -m moon.assets --target s3 p4 knob)")
endif()
file(READ ${MOON_ASSETS_MANIFEST} MOON_ASSET_MANIFEST)
# Single-line JSON (the pipeline writes it that way). CMake MATCHES is
# ECMAScript regex; the compact manifest's only whitespace is after colons.
if(NOT MOON_ASSET_MANIFEST MATCHES "\"combined_bytes\" *: *([0-9]+)")
    message(FATAL_ERROR "Malformed manifest ${MOON_ASSETS_MANIFEST} (no combined_bytes): "
        "regenerate with cd assets/images/moon && .venv/bin/python -m moon.assets --target knob")
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

# Boot movie (the clocks' root-CMakeLists block, mirrored): flash the
# precomputed synodic cycle into the moon_movie data partition (raw
# region; read at boot through the shared esp/moon_esp_movie.c, linked
# in via EXTRA_COMPONENT_DIRS). The knob has no SD socket -- the flash
# movie is its ONLY movie source, but the posture stays the clocks': a
# missing movie NEVER blocks a code build (the boot falls back to the
# saved-frame / disc path). The guards only arm when THIS table carries
# a moon_movie row.
set(MOON_MOVIE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../assets/images/moon/out/knob)
set(MOON_MOVIE_BIN ${MOON_MOVIE_DIR}/moon_movie.bin)
set(MOON_MOVIE_MANIFEST ${MOON_MOVIE_DIR}/moon_movie_manifest.json)
set(MOON_MOVIE_CSV ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)
file(READ ${MOON_MOVIE_CSV} MOON_MOVIE_CSV_CONTENT)
if(MOON_MOVIE_CSV_CONTENT MATCHES "moon_movie *,[^\n]*, *([0-9]+)K")
    math(EXPR MOON_MOVIE_ROW_BYTES "${CMAKE_MATCH_1} * 1024")

    # Guard 1 -- the whole table fits the flash chip (16 MB on this
    # board): the fixed region (36K) + every row must stay within
    # CONFIG_ESPTOOLPY_FLASHSIZE, or the code build would not flash.
    set(MOON_MOVIE_ROWS_KIB 0)
    string(REPLACE "\n" ";" MOON_MOVIE_CSV_LINES "${MOON_MOVIE_CSV_CONTENT}")
    foreach(_line IN LISTS MOON_MOVIE_CSV_LINES)
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^[a-z_]+, *(data|app),")
            string(REPLACE "," ";" _f "${_line}")
            list(GET _f 4 _sz)
            string(STRIP "${_sz}" _sz)
            if(_sz MATCHES "^([0-9]+)K$")
                math(EXPR _kib "${CMAKE_MATCH_1}")
            elseif(_sz MATCHES "^0x[0-9A-Fa-f]+$")
                math(EXPR _kib "(${_sz}) / 1024")
            else()
                message(FATAL_ERROR "Unparseable partition size '${_sz}' (row: ${_line}) in "
                    "${MOON_MOVIE_CSV} -- the moon_movie guards need a KiB or 0x-hex size.")
            endif()
            math(EXPR MOON_MOVIE_ROWS_KIB "${MOON_MOVIE_ROWS_KIB} + ${_kib}")
        endif()
    endforeach()
    if(NOT CONFIG_ESPTOOLPY_FLASHSIZE MATCHES "^([0-9]+)MB$")
        message(FATAL_ERROR "CONFIG_ESPTOOLPY_FLASHSIZE='${CONFIG_ESPTOOLPY_FLASHSIZE}' -- "
            "expected NNNMB (the moon_movie table guard needs the chip size).")
    endif()
    math(EXPR MOON_CHIP_KIB "${CMAKE_MATCH_1} * 1024")
    math(EXPR MOON_FIXED_KIB 36)
    math(EXPR MOON_TABLE_TOTAL_KIB "${MOON_FIXED_KIB} + ${MOON_MOVIE_ROWS_KIB}")
    if(MOON_TABLE_TOTAL_KIB GREATER MOON_CHIP_KIB)
        message(FATAL_ERROR "Partition table in ${MOON_MOVIE_CSV} (${MOON_MOVIE_ROWS_KIB}K rows + "
            "${MOON_FIXED_KIB}K fixed) exceeds the ${MOON_CHIP_KIB}K flash (CONFIG_ESPTOOLPY_FLASHSIZE) "
            "-- the code build would not flash. Shrink the moon_movie row (the generator then "
            "re-bakes at a coarser frame step).")
    endif()

    if(EXISTS ${MOON_MOVIE_BIN})
        # Guard 2 -- the blob fits its row (belt for the generator).
        file(SIZE ${MOON_MOVIE_BIN} MOON_MOVIE_BYTES)
        if(MOON_MOVIE_BYTES GREATER MOON_MOVIE_ROW_BYTES)
            math(EXPR MOON_MOVIE_NEED_KIB "(${MOON_MOVIE_BYTES} + 1023) / 1024")
            message(FATAL_ERROR "knob moon_movie.bin ${MOON_MOVIE_BYTES} B does not fit the "
                "${MOON_MOVIE_ROW_BYTES} B moon_movie row in ${MOON_MOVIE_CSV} (needs >= ${MOON_MOVIE_NEED_KIB}K). "
                "Re-run the generator (python -m moon.movie --target knob) -- it lands on a "
                "coarser frame step -- or widen the row.")
        endif()
        # Guard 3 -- manifest freshness: the bin must be this board's
        # bake at the current calcver (the firmware rejects it at boot
        # either way; fail fast with a clear message instead).
        if(EXISTS ${MOON_MOVIE_MANIFEST})
            file(READ ${MOON_MOVIE_MANIFEST} MOON_MOVIE_MANIFEST)
            if(NOT MOON_MOVIE_MANIFEST MATCHES "\"target\" *: *\"([^\"]+)\"")
                message(FATAL_ERROR "Malformed manifest ${MOON_MOVIE_MANIFEST} (no target): "
                    "regenerate with cd assets/images/moon && .venv/bin/python -m moon.movie --target knob")
            endif()
            if(NOT CMAKE_MATCH_1 STREQUAL "knob")
                message(FATAL_ERROR "knob moon_movie.bin was baked for target '${CMAKE_MATCH_1}' but this build "
                    "is 'knob' (manifest ${MOON_MOVIE_MANIFEST}). Regenerate: "
                    "cd assets/images/moon && .venv/bin/python -m moon.movie --target knob")
            endif()
            if(NOT MOON_MOVIE_MANIFEST MATCHES "\"calcver\" *: *\"([^\"]+)\"")
                message(FATAL_ERROR "Malformed manifest ${MOON_MOVIE_MANIFEST} (no calcver): "
                    "regenerate with cd assets/images/moon && .venv/bin/python -m moon.movie --target knob")
            endif()
            if(NOT CMAKE_MATCH_1 STREQUAL MOON_CALC_VER)
                message(FATAL_ERROR "knob moon_movie.bin was baked at calcver '${CMAKE_MATCH_1}' but the build "
                    "is '${MOON_CALC_VER}'. Regenerate: cd assets/images/moon && .venv/bin/python -m moon.movie --target knob")
            endif()
        else()
            message(WARNING "knob moon_movie.bin present but no manifest at ${MOON_MOVIE_MANIFEST} -- "
                "fresh-clone check of target/calcver skipped; regenerate with "
                "cd assets/images/moon && .venv/bin/python -m moon.movie --target knob")
        endif()
        esptool_py_flash_to_partition(flash "moon_movie" ${MOON_MOVIE_BIN})
    else()
        message(WARNING "knob moon movie not found at ${MOON_MOVIE_BIN} -- run "
            "'cd assets/images/moon && .venv/bin/python -m moon.movie --target knob' to bake it "
            "(the build succeeds either way; without the movie the boot plays the "
            "saved-frame / disc path instead).")
    endif()
endif()
