# Host simulator: builds the DEVICE core (components/moon) + the generated
# host assets and links a PNG-writing CLI. This is the C-vs-NASA gate:
# the exact runtime path (cheap ephemeris, fixed-point rotation,
# gray->RGB565) rendered on the host, image-diffed against NASA with the
# same metrics as the Python prototype.
#
# The albedo source (moon_assets_gen.c) is GENERATED and stays gitignored
# under assets/images/moon/ (see the moon.assets pipeline):
#   assets/images/moon/.venv/bin/python -m moon.assets
cmake_minimum_required(VERSION 3.16)
project(moon_sim C)

set(MOON_ASSETS_GEN ${CMAKE_CURRENT_SOURCE_DIR}/../assets/images/moon/moon_assets_gen.c)
if(NOT EXISTS ${MOON_ASSETS_GEN})
    message(FATAL_ERROR
        "Missing generated asset ${MOON_ASSETS_GEN}.\n"
        "Regenerate it with: assets/images/moon/.venv/bin/python -m moon.assets")
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
    ${MOON_CORE}/core/moon_bake.c
    ${MOON_CORE}/core/moon_render.c
    ${MOON_CORE}/core/moon_fixp.c
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
