# Moon phase display component.
#
# Portable core (pure C + vendored public-domain ERFA ephemeris): the exact
# same translation units are also built into the host simulator
# (host_sim/CMakeLists.txt), which is the C-vs-NASA gate. Device glue:
# esp/moon_esp_assets.c (albedo from the moon_assets raw partition).
idf_component_register(
    SRCS
        core/moon_astro.c
        core/moon_bake.c
        core/moon_render.c
        core/moon_fixp.c
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
        core/erfa
    REQUIRES esp_partition log
)
