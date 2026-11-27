# The moon display glue for the knob (P0 family seam): the core TUs are no
# longer compiled through relative paths here — the knob links the clock's
# own components/moon (added via the project's EXTRA_COMPONENT_DIRS),
# which selects the knob's generated header (generated/knob/moon_consts.h,
# pipeline truth constants.TARGETS["knob"]) through its CONFIG_BOARD_KNOB
# branch. This component keeps only knob_moon.c: the eager asset load +
# bake glue (the producer/swap frame model lives in knob_ui).
idf_component_register(SRCS "knob_moon.c"
        INCLUDE_DIRS "include"
        REQUIRES moon esp_partition esp_timer log esp_hw_support)
