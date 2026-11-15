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
    # would otherwise FATAL_ERROR on the target mismatch).
    set(EXCLUDE_COMPONENTS "esp32_s3_touch_amoled_1_75")
    # The component manager rewrites its dependencies lock on every
    # configure and the two targets resolve DIFFERENT dependency sets
    # (different BSPs + driver stacks), so the P4 build uses its own
    # lock file (dependencies.lock.p4, gitignored) and never touches the
    # tracked S3 dependencies.lock. The property must be set before
    # project(), which runs the component manager.
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.p4")
endif()

project(moon-phase)

# Calculation version for the TF sweep cache (components/ui/moon_tfcache.c):
# the cache folder is /moon_cache/<calcver>_<target>_<bakepx>/. The key is
# the version set MANUALLY via commitizen (`cz bump` writes
# version = "x.y.z" into .cz.toml): the card re-keys only on a deliberate
# version bump, so commits alone never invalidate the cache again (the old
# per-commit git-describe key rolled a fresh folder per build -- including
# `-dirty` keys no commit ever owned). Until the FIRST bump there is no
# version line and the git-describe short hash keys the folder -- equally
# valid as a cache key. Re-configure is triggered on .cz.toml and on
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
        (IDF_TARGET STREQUAL "esp32s3" AND NOT CONFIG_BOARD_S3))
    message(FATAL_ERROR "Board mismatch: IDF_TARGET=${IDF_TARGET} (CONFIG_BOARD_S3='${CONFIG_BOARD_S3}', "
        "CONFIG_BOARD_P4='${CONFIG_BOARD_P4}'). Pass the matching -D IDF_TARGET / "
        "sdkconfig.defaults.<target> so the build target and the Kconfig BOARD choice agree.")
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
    if(NOT MOON_ASSET_MANIFEST_ALB_W EQUAL MOON_ALBEDO_W)
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
# it against MOON_ALBEDO_W and the partition size):
#   cd assets/images/moon && .venv/bin/python -m moon.assets
# It emits out/<target>/moon_assets.bin (albedo MOON_ALB_W x MOON_ALB_H u8
# at 0, then relief DEM MOON_DEM_W x MOON_DEM_H int8).
if(EXISTS ${MOON_ASSETS_BIN})
    esptool_py_flash_to_partition(flash "moon_assets" ${MOON_ASSETS_BIN})
else()
    message(WARNING "moon assets not found at ${MOON_ASSETS_BIN} -- "
        "run 'cd assets/images/moon && .venv/bin/python -m moon.assets' to generate them "
        "(the build succeeds, but the moon_assets partition is not flashed).")
endif()
