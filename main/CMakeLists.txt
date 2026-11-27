cmake_minimum_required(VERSION 3.16)

# The app entry point is board-selected: the knob (an esp32s3 target, so the
# S3 and knob share IDF_TARGET) compiles main_knob.c (its own boot arm: dial /
# battery / real deep sleep / EXT0-EXT1-RTC-TIMER wakes); the S3/P4 clocks
# compile main.cpp. KNOB_BOARD is promoted into the early requirements pass by
# the root CMakeLists, so the branch is visible here in both passes. No
# REQUIRES: the `main` component depends on every component in the build by
# default (project.cmake's main special case), so main_knob.c reaches the
# knob_* + shared component headers with no explicit list.
if(KNOB_BOARD)
idf_component_register(
    SRCS "main_knob.c"
    INCLUDE_DIRS "."
)
else()
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
)
endif()
