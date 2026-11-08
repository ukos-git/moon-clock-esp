# The moon display core for the knob (K7, task #24): the clock's portable
# core TUs compiled VERBATIM from ../../components/moon -- the exact same
# translation units host_sim/ links (its MOON_ASSETS_TARGET=knob sim is the
# gate) and the clock's own moon component builds. No lvgl / esp_lcd here:
# the frame->lv_image glue lives in knob_ui/knob_moon_view.c, so the physics
# stays panel-free and diffable against the host gate.
#
# The per-board truth is the pipeline-generated header
# ../../components/moon/generated/knob/moon_consts.h (albedo 864x432, relief
# DEM 1024x512 -- constants.TARGETS["knob"]); the ESP asset backend reads the
# moon_assets partition (knob/partitions.csv, guarded in knob/CMakeLists.txt).
idf_component_register(SRCS
        "knob_moon.c"
        "../../../components/moon/core/moon_astro.c"
        "../../../components/moon/core/moon_bake.c"
        "../../../components/moon/core/moon_render.c"
        "../../../components/moon/core/moon_fixp.c"
        "../../../components/moon/core/moon_event.c"
        "../../../components/moon/core/erfa/moon98.c"
        "../../../components/moon/core/erfa/epv00.c"
        "../../../components/moon/core/erfa/s2pv.c"
        "../../../components/moon/core/erfa/pfw06.c"
        "../../../components/moon/core/erfa/obl06.c"
        "../../../components/moon/core/erfa/rx.c"
        "../../../components/moon/core/erfa/rz.c"
        "../../../components/moon/core/erfa/cp.c"
        "../../../components/moon/core/erfa/ir.c"
        "../../../components/moon/core/erfa/rxp.c"
        "../../../components/moon/core/erfa/rxpv.c"
        "../../../components/moon/esp/moon_esp_assets.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "../../../components/moon/include"
        "../../../components/moon/core/erfa"
        "../../../components/moon/generated/knob"
    REQUIRES esp_partition esp_timer log esp_hw_support
)
