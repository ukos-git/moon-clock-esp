# The moon display glue for the knob (P0 family seam): the core TUs are no
# longer compiled through relative paths here — the knob (built from this
# same project on the esp32s3 target, -D KNOB_BOARD=ON) compiles the
# clock's own components/moon as an ordinary sibling component, which
# selects the knob's generated header (generated/knob/moon_consts.h,
# pipeline truth constants.TARGETS["knob"]) through its CONFIG_BOARD_KNOB
# branch. This component keeps only knob_moon.c: the eager asset load +
# bake glue (the producer/swap frame model lives in knob_ui).
# Gated on KNOB_BOARD (promoted into the early pass by the root CMakeLists):
# the knob's moon glue references the knob's generated constants header
# (generated/knob/moon_consts.h) and its CONFIG_BOARD_KNOB branch, so it is
# compiled only on the knob; off the knob the component registers with no
# SRCS.
if(KNOB_BOARD)
idf_component_register(SRCS "knob_moon.c"
        INCLUDE_DIRS "include"
        REQUIRES moon esp_partition esp_timer log esp_hw_support)
else()
idf_component_register()
endif()
