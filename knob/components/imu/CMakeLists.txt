# Knob shim of the shared "imu" component (same-name answer, the P0
# board-shim pattern; the shared components REQUIRES "imu" by name and
# IDF resolves it to this stub instead of the QMI8658 driver the clock
# boards compile).
idf_component_register(SRCS "imu_stub.c"
                       INCLUDE_DIRS "include")
