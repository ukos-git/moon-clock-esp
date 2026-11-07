idf_component_register(SRCS "knob_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl knob_board knob_link)
# The shared wire contract (frame types/fields) comes from the clock's
# companion component — one header, both builds.
target_include_directories(${COMPONENT_LIB} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/companion/include)
