# P4 battery gauge: adc_oneshot on GPIO1 (ADC1_CH0) + fuse calibration,
# linear percentage between the Kconfig placeholder points. SRCS and
# REQUIRES stay LITERAL (the component manager's early requirements
# pass sees no CONFIG_ symbols); the KNOB_BATTERY_GAUGE gate is RUNTIME
# inside knob_battery.c. esp_adc carries both adc_oneshot and adc_cali.
# Gated on KNOB_BOARD (promoted into the early pass by the root CMakeLists):
# the battery ADC is knob hardware, so off the knob the component registers
# with no SRCS and its object is never built for the clocks.
if(KNOB_BOARD)
idf_component_register(SRCS "knob_battery.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc esp_hw_support)
else()
idf_component_register()
endif()
