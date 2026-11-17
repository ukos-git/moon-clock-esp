# Host simulator: builds the DEVICE core (components/moon) + the generated
# host assets and links a PNG-writing CLI. This is the C-vs-NASA gate:
# the exact runtime path (cheap ephemeris, fixed-point rotation,
# gray->RGB565) rendered on the host, image-diffed against NASA with the
# same metrics as the Python prototype.
#
# The asset blobs (moon_assets_gen.c) are GENERATED and stay gitignored
# under assets/images/moon/out/<target>/ (see the moon.assets pipeline).
# One simulator per board target: -DMOON_ASSETS_TARGET=s3 (default; build
# dir build/), p4 (build dir build-p4/) or knob (build dir build-knob/ --
# the K7 standalone-display sibling project knob/) -- the sim links the
# target's texture and reads its per-board moon_consts.h, so the gate can
# diff the C core at each board's asset size:
#   assets/images/moon/.venv/bin/python -m moon.assets --target s3 p4 knob
#   cmake -DMOON_ASSETS_TARGET=p4 -B build-p4 && cmake --build build-p4
cmake_minimum_required(VERSION 3.16)
project(moon_sim C)

set(MOON_ASSETS_TARGET "s3" CACHE STRING "Board target for the host assets: s3 | p4 | knob")
if(NOT MOON_ASSETS_TARGET MATCHES "^(s3|p4|knob)$")
    message(FATAL_ERROR "MOON_ASSETS_TARGET must be s3, p4 or knob (got '${MOON_ASSETS_TARGET}')")
endif()
set(MOON_ASSETS_GEN ${CMAKE_CURRENT_SOURCE_DIR}/../assets/images/moon/out/${MOON_ASSETS_TARGET}/moon_assets_gen.c)
set(MOON_CONSTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/moon/generated/${MOON_ASSETS_TARGET})
if(NOT EXISTS ${MOON_ASSETS_GEN} OR NOT IS_DIRECTORY ${MOON_CONSTS_DIR})
    message(FATAL_ERROR
        "Missing generated assets for target '${MOON_ASSETS_TARGET}'\n"
        "(need ${MOON_ASSETS_GEN} and ${MOON_CONSTS_DIR}/).\n"
        "Regenerate with: assets/images/moon/.venv/bin/python -m moon.assets --target ${MOON_ASSETS_TARGET}\n"
        "(fresh clone: python3 assets/images/moon/get_assets.py --target ${MOON_ASSETS_TARGET})")
endif()

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2)

set(MOON_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../components/moon)
set(MOON_HOST ${CMAKE_CURRENT_SOURCE_DIR})

# The portable core's source set (moon_sim and moon_bench both link it):
# the exact device runtime path (cheap ephemeris, fixed-point rotation,
# gray->RGB565) plus the vendored public-domain ERFA and the generated
# host asset table.
set(MOON_CORE_SRCS
    ${MOON_CORE}/core/moon_astro.c
    ${MOON_CORE}/core/moon_viewport.c
    ${MOON_CORE}/core/moon_bake.c
    ${MOON_CORE}/core/moon_render.c
    ${MOON_CORE}/core/moon_fixp.c
    ${MOON_CORE}/core/moon_event.c
    ${MOON_CORE}/core/erfa/moon98.c
    ${MOON_CORE}/core/erfa/epv00.c
    ${MOON_CORE}/core/erfa/s2pv.c
    ${MOON_CORE}/core/erfa/pfw06.c
    ${MOON_CORE}/core/erfa/obl06.c
    ${MOON_CORE}/core/erfa/rx.c
    ${MOON_CORE}/core/erfa/rz.c
    ${MOON_CORE}/core/erfa/cp.c
    ${MOON_CORE}/core/erfa/ir.c
    ${MOON_CORE}/core/erfa/rxp.c
    ${MOON_CORE}/core/erfa/rxpv.c
    ${MOON_HOST}/../components/moon/host/moon_host_assets.c
    ${MOON_ASSETS_GEN}
)

set(MOON_CORE_INCS
    ${MOON_CORE}/include
    ${MOON_CONSTS_DIR}
    ${MOON_CORE}/core/erfa
    ${MOON_HOST}
    ${MOON_CORE}/../..
)

add_executable(moon_sim main.c ${MOON_CORE_SRCS})
target_include_directories(moon_sim PRIVATE ${MOON_CORE_INCS})
target_link_libraries(moon_sim PRIVATE m)

# The performance bench (book chapter 19): times the device path's
# steps (geometry, relief build, bake, rotate, shade map, re-project)
# across disk sizes on the host.
add_executable(moon_bench bench.c ${MOON_CORE_SRCS})
target_include_directories(moon_bench PRIVATE ${MOON_CORE_INCS})
target_link_libraries(moon_bench PRIVATE m)
