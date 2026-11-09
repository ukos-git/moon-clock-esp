# The moon view (K7b): glue over knob_moon (the verbatim ported core).
# The companion mirror views and the clock's wire-contract include went
# with the companion approach (2026-11-08).
idf_component_register(SRCS "knob_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl knob_board knob_moon clock knob_power)
