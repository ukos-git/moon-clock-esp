file(GLOB_RECURSE IMAGE_SOURCES "images/*.c")
file(GLOB_RECURSE FONT_SOURCES "fonts/*.c")

idf_component_register(
  SRCS
    "ui.c"
    "screens.c"
    "images.c"
    "styles.c"
    "actions.c"
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
  REQUIRES driver lvgl
  INCLUDE_DIRS "./")
