# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# P4 target block. (The partition table is NOT set here: the partition_table
# component's project hook re-derives PARTITION_CSV_PATH from the Kconfig
# values, so a plain set() before project() is clobbered. The selection
# lives in the per-target defaults files instead — the shared
# sdkconfig.defaults sets CONFIG_PARTITION_TABLE_CUSTOM=y, and
# sdkconfig.defaults.esp32p4 sets
# CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_p4.csv" (32 MB layout,
# 9216K factory); the S3 keeps the default "partitions.csv".)
if(IDF_TARGET STREQUAL "esp32p4")
    # The vendored S3 BSP is S3-only (its manifest declares
    # targets: [esp32s3]); EXCLUDE_COMPONENTS keeps it out of the P4
    # build AND of the component manager's local-manifest list (which
    # would otherwise FATAL_ERROR on the target mismatch). The knob
    # component joins the exclusion: EXCLUDE_COMPONENTS also keeps it out
    # of the component manager's local-manifest list, so its yml (the
    # knob-only managed deps) is never evaluated on the clock builds and
    # those deps stay out of the P4 lock too.
    set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75;knob_board")
    # The component manager rewrites its dependencies lock on every
    # configure and the two targets resolve DIFFERENT dependency sets
    # (different BSPs + driver stacks), so the P4 build uses its own
    # lock file (dependencies.lock.p4, gitignored) and never touches the
    # tracked S3 dependencies.lock. The property must be set before
    # project(), which runs the component manager.
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.p4")
endif()

# S3 clock block. The knob component is an esp32s3 TARGET with a manifest
# (components/knob_board/idf_component.yml) that resolves the knob-only
# managed deps on that target; the S3 clock shares the target, so the
# manifest cannot discriminate by target alone — the exclusion does.
# EXCLUDE_COMPONENTS keeps knob_board out of the component manager's
# local-manifest list, so its yml is never evaluated here and its deps
# (esp_lcd_sh8601, esp_codec_dev) stay out of the tracked S3 lock. Nothing
# on the clock build requires knob_board by name (the board component
# selects the S3 BSP there), so the exclusion drops nothing.
if(NOT KNOB_BOARD AND NOT IDF_TARGET STREQUAL "esp32p4")
    set(EXCLUDE_COMPONENTS "knob_board")
endif()

# Knob target block. The knob IS an esp32s3 (same target as the S3 clock),
# so it is selected by -D KNOB_BOARD=ON rather than a different IDF_TARGET.
# That flag must reach the component manager's early requirements pass,
# which a plain -D cache variable does NOT (that pass is a `cmake -P`
# subprocess, component.cmake, that only sees the four -D args it is given
# plus the __BUILD_PROPERTIES dump). idf_build_set_property promotes it
# into that dump (the same channel IDF_TARGET itself rides, build.cmake),
# so the board/imu/knob_* CMakeLists' `if(KNOB_BOARD)` SRCS/REQUIRES
# branches resolve in BOTH the early pass and the target pass.
#   - EXCLUDE_COMPONENTS the vendored S3 BSP: its 466 px bsp/display.h
#     would scale the knob's 360 px round layout wrong, and its esp_codec_dev
#     public dep would leak in (the knob instead answers that name with the
#     real driver, declared in components/knob_board/idf_component.yml).
#   - its own dependencies lock (the knob adds esp_lcd_sh8601 + esp_codec_dev,
#     a different set than the S3), gitignored like the P4's.
if(KNOB_BOARD)
    set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75")
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.knob")
    idf_build_set_property(KNOB_BOARD ON)
endif()

project(moon-phase)

# Calculation version for the TF sweep cache (components/ui/moon_tfcache.c):
# the cache folder is /moon_cache/<calcver>_<target>_<bakepx>/. The key is
# the version set MANUALLY via commitizen (`cz bump` writes
# version = "x.y.z" into .cz.toml): the card re-keys only on a deliberate
# version bump, so commits alone never invalidate the cache again (the old
# per-commit git-describe key rolled a fresh folder per build -- including
# `-dirty` keys no commit ever owned). The git-describe short hash is the
# fallback ONLY while the version line is missing (pre-0.2.0: the first
# bump, 2026-11-20, made the version line the key). Re-configure is
# triggered on .cz.toml and on
# commit/tag movement (.git/HEAD, packed-refs) so both sources take effect
# without a fullclean.
set(MOON_CALC_VER "")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/.cz.toml)
if(EXISTS ${CMAKE_SOURCE_DIR}/.cz.toml)
    file(READ ${CMAKE_SOURCE_DIR}/.cz.toml _cz_toml)
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([^\"]+)\"" _cz_match "${_cz_toml}")
    if(_cz_match)
        set(MOON_CALC_VER "${CMAKE_MATCH_1}")
    endif()
endif()
if(NOT MOON_CALC_VER)
    find_package(Git QUIET)
    if(GIT_FOUND)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${CMAKE_SOURCE_DIR}/.git/HEAD
            ${CMAKE_SOURCE_DIR}/.git/packed-refs)
        execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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
message(STATUS "moon calc version (TF sweep-cache folder prefix): ${MOON_CALC_VER}")

# Target/board mismatch guard (risk: a stray CONFIG_BOARD_P4=y in an S3
# build, or vice versa, would resolve two BSPs / the wrong partition
# table and fail obscurely deep in the build).
if((IDF_TARGET STREQUAL "esp32p4" AND NOT CONFIG_BOARD_P4) OR
        (IDF_TARGET STREQUAL "esp32s3" AND NOT CONFIG_BOARD_S3 AND NOT CONFIG_BOARD_KNOB))
    message(FATAL_ERROR "Board mismatch: IDF_TARGET=${IDF_TARGET} (CONFIG_BOARD_S3='${CONFIG_BOARD_S3}', "
        "CONFIG_BOARD_P4='${CONFIG_BOARD_P4}', CONFIG_BOARD_KNOB='${CONFIG_BOARD_KNOB}'). Pass the matching "
        "-D IDF_TARGET / sdkconfig.defaults.<target> so the build target and the Kconfig BOARD choice agree.")
endif()

# The knob is an esp32s3 target, so the guard above alone would let a knob
# sdkconfig (CONFIG_BOARD_KNOB=y) resolve against the S3 build: the vendored
# S3 BSP would register (466 px bsp/ headers, the S3 lock) and the knob's
# hardware would be misdriven. Require the -D KNOB_BOARD=ON flag that the
# pre-project leg uses to exclude the S3 BSP + select the knob lock, so a
# stray knob sdkconfig fails loudly instead of silently misbuilding.
if(CONFIG_BOARD_KNOB AND NOT KNOB_BOARD)
    message(FATAL_ERROR "CONFIG_BOARD_KNOB is set but -D KNOB_BOARD=ON was not passed. The knob build must "
        "exclude the vendored S3 BSP and use its own lock. Re-run with: idf.py -D KNOB_BOARD=ON "
        "-D SDKCONFIG=sdkconfig.knob -D SDKCONFIG_DEFAULTS=\"sdkconfig.defaults;sdkconfig.defaults.knob\" -B build/knob build")
endif()

# Moon asset consistency guard (fails fast at configure, not at flash/boot).
# The pre-baked assets (assets/images/moon/out/<target>/, gitignored) must
# agree with the board: an even equirect albedo width (MOON_ALBEDO_W), the
# manifest's baked width equal to the configured width, and the combined
# blob within the board's moon_assets partition. All three mismatches have
# historically surfaced as obscure build/boot failures (wrong texture dims
# compiled in, esptool "data bigger than partition", PSRAM alloc failure),
# so they are FATAL_ERRORs here.
if(IDF_TARGET STREQUAL "esp32p4")
    set(MOON_ASSET_TARGET p4)
elseif(CONFIG_BOARD_KNOB)
    set(MOON_ASSET_TARGET knob)
else()
    set(MOON_ASSET_TARGET s3)
endif()
set(MOON_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/moon/out/${MOON_ASSET_TARGET})
# An int Kconfig is always defined; seed the guard's local from the
# CONFIG_-prefixed Kconfig value (ESP-IDF exposes Kconfig symbols to CMake
# as CONFIG_<NAME>) and normalize it. CMake if() does not evaluate
# arithmetic -- math() does.
math(EXPR MOON_ALBEDO_W "${CONFIG_MOON_ALBEDO_W}")
math(EXPR MOON_ALBEDO_H "${MOON_ALBEDO_W} / 2")
math(EXPR MOON_ALBEDO_W_ODD "${MOON_ALBEDO_W} % 2")
if(NOT MOON_ALBEDO_W OR MOON_ALBEDO_W_ODD)
    message(FATAL_ERROR "MOON_ALBEDO_W=${MOON_ALBEDO_W} must be an even width (equirect 2:1; "
        "height = width / 2). Set a Kconfig value from {864, 1024, ..., 4096} and regenerate: "
        "cd assets/images/moon && .venv/bin/python -m moon.assets --target ${MOON_ASSET_TARGET} --alb ${MOON_ALBEDO_W}x${MOON_ALBEDO_H}")
endif()
set(MOON_ASSETS_MANIFEST ${MOON_ASSET_DIR}/moon_assets_manifest.json)
set(MOON_ASSETS_BIN ${MOON_ASSET_DIR}/moon_assets.bin)
if(EXISTS ${MOON_ASSETS_MANIFEST})
    file(READ ${MOON_ASSETS_MANIFEST} MOON_ASSET_MANIFEST)
    # Single-line JSON (the pipeline writes it that way): pull the baked
    # width and the combined blob size. (CMake MATCHES is ECMAScript regex --
    # no POSIX [[:space:]] class; the manifest is compact, so a plain space
    # class covers the only whitespace that could appear.)
    if(NOT MOON_ASSET_MANIFEST MATCHES "\"alb_w\" *: *([0-9]+)")
        message(FATAL_ERROR "Malformed manifest ${MOON_ASSETS_MANIFEST} (no alb_w): "
            "regenerate with cd assets/images/moon && .venv/bin/python -m moon.assets --target ${MOON_ASSET_TARGET}")
    endif()
    math(EXPR MOON_ASSET_MANIFEST_ALB_W ${CMAKE_MATCH_1})
    if(NOT MOON_ASSET_MANIFEST MATCHES "\"combined_bytes\" *: *([0-9]+)")
        message(FATAL_ERROR "Malformed manifest ${MOON_ASSETS_MANIFEST} (no combined_bytes): "
            "regenerate with cd assets/images/moon && .venv/bin/python -m moon.assets --target ${MOON_ASSET_TARGET}")
    endif()
    math(EXPR MOON_ASSET_COMBINED ${CMAKE_MATCH_1})
    # The knob has NO MOON_ALBEDO_W Kconfig mirror (the moon Kconfig's
    # `default 2048 if BOARD_P4` / `default 864` fallback is its only value,
    # and the knob code reads the width from the generated header + manifest,
    # not from Kconfig) -- so the manifest-vs-Kconfig cross-check is skipped
    # for the knob target. The manifest's own alb_w + the partition fit
    # below remain the knob's guards (its original knob/CMakeLists.txt shape).
    if(NOT MOON_ASSET_TARGET STREQUAL "knob" AND NOT MOON_ASSET_MANIFEST_ALB_W EQUAL MOON_ALBEDO_W)
        message(FATAL_ERROR "Moon assets were baked at albedo width ${MOON_ASSET_MANIFEST_ALB_W} "
            "(manifest ${MOON_ASSETS_MANIFEST}) but the Kconfig is MOON_ALBEDO_W=${MOON_ALBEDO_W}. "
            "Regenerate the assets: cd assets/images/moon && .venv/bin/python -m moon.assets "
            "--target ${MOON_ASSET_TARGET} --alb ${MOON_ALBEDO_W}x${MOON_ALBEDO_H} "
            "or restore the Kconfig value to ${MOON_ASSET_MANIFEST_ALB_W}.")
    endif()
    # The blob must fit the board's moon_assets partition. The CSV is the
    # one the build actually uses: CONFIG_PARTITION_TABLE_CUSTOM_FILENAME
    # when set (the P4 defaults file pins partitions_p4.csv), else the
    # IDF default partitions.csv (the S3).
    if(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME)
        set(MOON_PART_CSV ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME})
    else()
        set(MOON_PART_CSV ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)
    endif()
    file(READ ${MOON_PART_CSV} MOON_PART_CSV_CONTENT)
    # The comma right after "moon_assets" (no space in the data row) keeps
    # this from matching the header COMMENT line, which is "moon_assets
    # <size>K (..." with no comma.
    if(MOON_PART_CSV_CONTENT MATCHES "moon_assets *,[^\n]*, *([0-9]+)K")
        math(EXPR MOON_PART_BYTES "${CMAKE_MATCH_1} * 1024")
    else()
        message(FATAL_ERROR "No 'moon_assets' partition row (size in K) found in ${MOON_PART_CSV} "
            "-- the asset pipeline needs it to verify the blob fits.")
    endif()
    if(MOON_ASSET_COMBINED GREATER MOON_PART_BYTES)
        # ${}-expand the variable inside the quoted math expression (a bare
        # name is not substituted and would compute to 0).
        math(EXPR MOON_PART_NEED_KIB "(${MOON_ASSET_COMBINED} + 1023) / 1024")
        message(FATAL_ERROR "Moon assets ${MOON_ASSET_COMBINED} B (albedo ${MOON_ALBEDO_W} + DEM) do not "
            "fit the ${MOON_ASSET_TARGET}'s moon_assets partition (${MOON_PART_BYTES} B in "
            "${MOON_PART_CSV}); they need >= ${MOON_PART_NEED_KIB}K. Widen the partition row or "
            "shrink the albedo (MOON_ALBEDO_W).")
    endif()
endif()

# Automatically include SPIFFS data from spiffs/ folder
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

# Moon phase assets: flash the combined equirect albedo + relief DEM blob
# into the moon_assets data partition (subtype "undefined" = plain raw
# binary region; read at boot via esp_partition_read). The blob is
# generated by the asset pipeline (local-only, gitignored) -- one per board
# target, selected by IDF_TARGET above (the consistency guard cross-checks
# it against MOON_ALBEDO_W and the partition size). Fresh clone: acquire +
# bake in one go with
#   python3 assets/images/moon/get_assets.py --target s3|p4
# (fetches the NASA sources into moon-kit/, bootstraps the .venv, bakes);
# re-bake alone: cd assets/images/moon && .venv/bin/python -m moon.assets.
# It emits out/<target>/moon_assets.bin (albedo MOON_ALB_W x MOON_ALB_H u8
# at 0, then relief DEM MOON_DEM_W x MOON_DEM_H int8).
if(EXISTS ${MOON_ASSETS_BIN})
    esptool_py_flash_to_partition(flash "moon_assets" ${MOON_ASSETS_BIN})
else()
    message(WARNING "moon assets not found at ${MOON_ASSETS_BIN} -- "
        "run 'python3 assets/images/moon/get_assets.py --target ${MOON_ASSET_TARGET}' to "
        "fetch and bake them (or the bake alone via the .venv once the sources are "
        "cached; the build succeeds either way, but the moon_assets partition is "
        "not flashed).")
endif()

# Boot movie: flash the precomputed synodic cycle into the moon_movie data
# partition (raw region; read at boot through esp/moon_esp_movie.c). Same
# build-time bake story as the moon_assets blob, one step further: the
# generator (python -m moon.movie) drives the portable core via moon_sim
# and emits out/<target>/moon_movie.bin (62 B header + u32 offset table +
# q90 gray JPEGs) sized to the moon_movie ROW of the partition CSV -- the
# "fill the leftover, never hinder the code build" policy. The guards only
# arm when the ACTIVE table carries a moon_movie row: the S3 does, and the
# P4 and the knob pick the row up from their own CSVs (partitions_p4.csv /
# partitions_knob.csv) -- the knob builds through this same file (KNOB_BOARD
# on the esp32s3 target), so there is no knob-side copy.
set(MOON_MOVIE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/images/moon/out/${MOON_ASSET_TARGET})
set(MOON_MOVIE_BIN ${MOON_MOVIE_DIR}/moon_movie.bin)
set(MOON_MOVIE_MANIFEST ${MOON_MOVIE_DIR}/moon_movie_manifest.json)
if(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME)
    set(MOON_MOVIE_CSV ${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME})
else()
    set(MOON_MOVIE_CSV ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)
endif()
file(READ ${MOON_MOVIE_CSV} MOON_MOVIE_CSV_CONTENT)
if(MOON_MOVIE_CSV_CONTENT MATCHES "moon_movie *,[^\n]*, *([0-9]+)K")
    math(EXPR MOON_MOVIE_ROW_BYTES "${CMAKE_MATCH_1} * 1024")

    # Guard 1 -- the whole table fits the flash chip: the fixed region
    # (32K bootloader + 4K partition table = 36K; the first row starts at
    # 0x9000) + every row's size (K or 0x-hex, both spellings occur) must
    # stay within CONFIG_ESPTOOLPY_FLASHSIZE. The app is bounded by its own
    # factory row, so the movie can never crowd the code -- but a row sized
    # against the wrong chip would otherwise only surface at flash time.
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
        # Guard 2 -- the blob fits its row (belt for the generator, which
        # measures real JPEG sizes and stops at the step that fits).
        file(SIZE ${MOON_MOVIE_BIN} MOON_MOVIE_BYTES)
        if(MOON_MOVIE_BYTES GREATER MOON_MOVIE_ROW_BYTES)
            math(EXPR MOON_MOVIE_NEED_KIB "(${MOON_MOVIE_BYTES} + 1023) / 1024")
            message(FATAL_ERROR "moon_movie.bin ${MOON_MOVIE_BYTES} B does not fit the "
                "${MOON_MOVIE_ROW_BYTES} B moon_movie row in ${MOON_MOVIE_CSV} (needs >= ${MOON_MOVIE_NEED_KIB}K). "
                "Re-run the generator (python -m moon.movie --target ${MOON_ASSET_TARGET}) -- it lands "
                "on a coarser frame step -- or widen the row.")
        endif()
        # Guard 3 -- manifest freshness: the bin must be this target's and
        # built at the current calcver (a stale calcver implies a stale
        # card cache too; the firmware rejects it at boot either way).
        if(EXISTS ${MOON_MOVIE_MANIFEST})
            file(READ ${MOON_MOVIE_MANIFEST} MOON_MOVIE_MANIFEST)
            if(NOT MOON_MOVIE_MANIFEST MATCHES "\"target\" *: *\"([^\"]+)\"")
                message(FATAL_ERROR "Malformed manifest ${MOON_MOVIE_MANIFEST} (no target): "
                    "regenerate with cd assets/images/moon && .venv/bin/python -m moon.movie")
            endif()
            if(NOT CMAKE_MATCH_1 STREQUAL MOON_ASSET_TARGET)
                message(FATAL_ERROR "moon_movie.bin was baked for target '${CMAKE_MATCH_1}' but this build "
                    "is '${MOON_ASSET_TARGET}' (manifest ${MOON_MOVIE_MANIFEST}). Regenerate: "
                    "cd assets/images/moon && .venv/bin/python -m moon.movie --target ${MOON_ASSET_TARGET}")
            endif()
            if(NOT MOON_MOVIE_MANIFEST MATCHES "\"calcver\" *: *\"([^\"]+)\"")
                message(FATAL_ERROR "Malformed manifest ${MOON_MOVIE_MANIFEST} (no calcver): "
                    "regenerate with cd assets/images/moon && .venv/bin/python -m moon.movie")
            endif()
            if(NOT CMAKE_MATCH_1 STREQUAL MOON_CALC_VER)
                message(FATAL_ERROR "moon_movie.bin was baked at calcver '${CMAKE_MATCH_1}' but the build "
                    "is '${MOON_CALC_VER}'. Regenerate: cd assets/images/moon && .venv/bin/python -m moon.movie")
            endif()
        else()
            message(WARNING "moon_movie.bin present but no manifest at ${MOON_MOVIE_MANIFEST} -- "
                "fresh-clone check of target/calcver skipped; regenerate with "
                "cd assets/images/moon && .venv/bin/python -m moon.movie")
        endif()
        esptool_py_flash_to_partition(flash "moon_movie" ${MOON_MOVIE_BIN})
    else()
        message(WARNING "moon movie not found at ${MOON_MOVIE_BIN} -- run "
            "'cd assets/images/moon && .venv/bin/python -m moon.movie --target ${MOON_ASSET_TARGET}' to "
            "bake it (the build succeeds either way; without the movie the boot falls back to "
            "the card replay / disc path).")
    endif()
endif()
