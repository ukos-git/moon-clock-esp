# Moon phase display component.
#
# Portable core (pure C + vendored public-domain ERFA ephemeris): the exact
# same translation units are also built into the host simulator
# (host_sim/CMakeLists.txt), which is the C-vs-NASA gate. Device glue:
# esp/moon_esp_assets.c (albedo from the moon_assets raw partition) and
# esp/moon_esp_movie.c (the boot movie's frames from the moon_movie row).
#
# The generated per-board header (generated/{s3,p4}/moon_consts.h, produced
# by the asset pipeline -- see components/moon/Kconfig MOON_ALBEDO_W) is
# selected by INCLUDE_DIRS, so every consumer's `#include "moon_consts.h"`
# resolves to the board's own texture dims without touching the core.
# First-match order (Kconfig defaults are first-match-wins too): the knob
# build (this same project, esp32s3 target with -D KNOB_BOARD=ON;
# CONFIG_BOARD_KNOB from the shared board component's choice) takes the
# generated/knob branch; the clock targets never define the symbol, so
# their TUs compile exactly as before.
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
        core/moon_viewport.c
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
        esp/moon_esp_movie.c
    INCLUDE_DIRS
        include
        ${MOON_GEN_DIR}
        core/erfa
    # heap_caps: both device backends (moon_esp_assets.c / moon_esp_movie.c)
    # read their partition data into PSRAM via heap_caps_malloc.
    REQUIRES esp_partition esp_timer log heap
    # espressif__esp_new_jpeg: the boot-movie codec (esp/moon_esp_movie.c
    # decodes the partition's q90 gray JPEGs). Same name + range the ui
    # component declares for the TF cache, so the solver keeps the single
    # vendored 1.0.2 across every project (declared in idf_component.yml).
    # PRIVATE: only moon_esp_movie.c includes its header.
    PRIV_REQUIRES espressif__esp_new_jpeg
)
