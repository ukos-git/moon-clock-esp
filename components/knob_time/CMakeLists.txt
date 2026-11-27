# Time bridge for the knob (P0 family seam): the K7d esp_sntp client
# retired onto the clock's clock_sntp.c; what remains is the one-time
# legacy-snapshot migration + the bench-Kconfig home (boot-sync window,
# caption TZ, frozen test epoch).
# Gated on KNOB_BOARD (promoted into the early pass by the root CMakeLists):
# the one-time knob legacy-snapshot migration + bench-Kconfig home is knob-
# specific; off the knob the component registers with no SRCS.
if(KNOB_BOARD)
idf_component_register(SRCS "knob_time.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash log)
else()
idf_component_register()
endif()
