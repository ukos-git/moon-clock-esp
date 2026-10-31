# QMI8658 6-axis IMU (gravity reader for the moon UI's tilt-driven
# "ball-in-hand" rotation). Minimal: I2C probe + WHO_AM_I verify +
# accelerometer reads on the shared BSP bus. See include/imu.h.
idf_component_register(
    SRCS "imu_qmi8658.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos log board
)
