# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set partition table
set(PARTITION_CSV_PATH ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)

project(moon-phase)

# Automatically include SPIFFS data from spiffs/ folder
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
