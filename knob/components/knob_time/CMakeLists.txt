# Time truth for the knob (K7d): one-shot esp_sntp over the knob_link
# join. The legacy esp_sntp API lives in lwip; freertos/log are common.
# NVS carries the last-trusted-epoch snapshot (no RTC on the board --
# deep-sleep wakes arm the static moon from it). No knob_link dependency
# HERE -- the boot waiter in main gates the start on the join (keeps
# this component a pure time service).
idf_component_register(SRCS "knob_time.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lwip nvs_flash)
