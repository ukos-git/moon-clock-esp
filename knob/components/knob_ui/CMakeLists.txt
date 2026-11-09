# The moon view (K7b) + the P1 render seam. Glue over knob_moon (the
# verbatim ported core) and, with KNOB_UI_ENABLE (the P1 umbrella), over
# the SHARED components/ui: knob_moon_render.c implements moon_ui.h for
# the shared screens on top of this component's producer/swap core, and
# knob_alarm_audio_stub.c answers alarm_audio.h for the codec-less board.
#
# SRCS and the `ui` requirement are UNCONDITIONAL, deliberately: the
# component manager's early requirements pass evaluates this file with no
# CONFIG_ symbols, so a CONFIG-gated REQUIRES is invisible there and the
# second pass then rejects the seam's alarm_audio.h/moon_ui.h includes
# ("ui is not in the requirements list"). The umbrella stays meaningful
# where it is already enforced — RUNTIME: main.c's #if CONFIG_KNOB_UI_ENABLE
# decides whether moon_ui_init/ui_init run at all. With the switch off the
# seam objects link but nothing ever calls them (legacy own-screen view);
# the shared UI's static lib is never pulled either, exactly as before.
set(KNOB_UI_SRCS "knob_ui.c" "knob_moon_render.c" "knob_alarm_audio_stub.c")
set(KNOB_UI_DEPS lvgl knob_board knob_moon clock knob_power ui)
idf_component_register(SRCS ${KNOB_UI_SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES ${KNOB_UI_DEPS})
