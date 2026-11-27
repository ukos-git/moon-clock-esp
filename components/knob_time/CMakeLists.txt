# Time bridge for the knob (P0 family seam): the K7d esp_sntp client
# retired onto the clock's clock_sntp.c; what remains is the one-time
# legacy-snapshot migration + the bench-Kconfig home (boot-sync window,
# caption TZ, frozen test epoch).
idf_component_register(SRCS "knob_time.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash log)
