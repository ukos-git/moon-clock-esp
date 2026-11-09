# Moon phase display component.
#
# Portable core (pure C + vendored public-domain ERFA ephemeris): the exact
# same translation units are also built into the host simulator
# (host_sim/CMakeLists.txt), which is the C-vs-NASA gate. Device glue:
# esp/moon_esp_assets.c (albedo from the moon_assets raw partition).
#
# The generated per-board header (generated/{s3,p4}/moon_consts.h, produced
# by the asset pipeline -- see components/moon/Kconfig MOON_ALBEDO_W) is
# selected by INCLUDE_DIRS, so every consumer's `#include "moon_consts.h"`
# resolves to the board's own texture dims without touching the core.
# First-match order (Kconfig defaults are first-match-wins too): the knob
# sibling project adds BOARD_KNOB via its board shim and links this
# component through EXTRA_COMPONENT_DIRS; the clock projects never define
# the symbol, so their TUs compile exactly as before.
if(CONFIG_BOARD_KNOB)
    set(MOON_GEN_DIR generated/knob)
elseif(CONFIG_BOARD_P4)
    set(MOON_GEN_DIR generated/p4)
else()
    set(MOON_GEN_DIR generated/s3)
endif()
idf_component_register(
    SRCS
        core/moon_astro.c
        core/moon_bake.c
        core/moon_render.c
        core/moon_fixp.c
        core/moon_event.c
        core/erfa/moon98.c
        core/erfa/epv00.c
        core/erfa/s2pv.c
        core/erfa/pfw06.c
        core/erfa/obl06.c
        core/erfa/rx.c
        core/erfa/rz.c
        core/erfa/cp.c
        core/erfa/ir.c
        core/erfa/rxp.c
        core/erfa/rxpv.c
        esp/moon_esp_assets.c
    INCLUDE_DIRS
        include
        ${MOON_GEN_DIR}
        core/erfa
    REQUIRES esp_partition esp_timer log
)
