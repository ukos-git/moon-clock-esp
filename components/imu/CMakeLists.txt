# IMU: the shared tilt/orbit surface (see include/imu.h). Two implementations
# of one contract, selected by the board:
#   - knob (KNOB_BOARD): a board with NO IMU — imu_stub.c answers "absent"
#     (ready=false, gravity 0) like the clocks' soft-fail, so the shared UI's
#     roll gate and tilt-orbit see a permanently-still device.
#   - clocks (S3/P4): the on-board QMI8658 (imu_qmi8658.c) over the shared
#     BSP I2C bus.
# KNOB_BOARD is promoted into the early requirements pass by the root
# CMakeLists (a CONFIG_ symbol would be invisible there), so the branch is
# safe in both the component manager's first pass and the target pass. The
# stub needs no REQUIRES (imu.h carries stdbool.h + esp_err.h, both on the
# global/default path); the driver keeps its own.
if(KNOB_BOARD)
    idf_component_register(
        SRCS "imu_stub.c"
        INCLUDE_DIRS "include"
    )
else()
    idf_component_register(
        SRCS "imu_qmi8658.c"
        INCLUDE_DIRS "include"
        REQUIRES driver freertos log board
    )
endif()
