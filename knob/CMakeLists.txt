# The companion-knob firmware (Waveshare ESP32-S3-Knob-Touch-LCD-1.8, the
# ESP32-S3R8 app side). A SIBLING project of the clock: its own build dir
# (-B build/knob), its own dependency lock, its own sdkconfig — the clock's
# moon core, BSPs and partition guards must not bleed in.
#
# Build (sequential with the clock builds — never parallel):
#   idf.py -C knob -B build/knob build
#   idf.py -C knob -B build/knob flash -p <port>   # esptool chip_id first!

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component manager's lock lives next to this file, board-suffixed and
# gitignored like the P4's dependencies.lock.p4 (the tracked root lock
# belongs to the clock's S3 build).
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.knob")

project(knob)
