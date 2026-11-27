# P4 battery gauge: adc_oneshot on GPIO1 (ADC1_CH0) + fuse calibration,
# linear percentage between the Kconfig placeholder points. SRCS and
# REQUIRES stay LITERAL (the component manager's early requirements
# pass sees no CONFIG_ symbols); the KNOB_BATTERY_GAUGE gate is RUNTIME
# inside knob_battery.c. esp_adc carries both adc_oneshot and adc_cali.
idf_component_register(SRCS "knob_battery.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc esp_hw_support)
