/**
 * @file imu_stub.c
 * @brief The knob's answer to components/imu: a board without an IMU.
 *
 * The clock's UI treats a missing/failed IMU as a supported posture
 * (soft-fail like the codec): roll correction and the tilt-orbit gate on
 * imu_is_ready(). The knob returns that posture permanently: ready=false,
 * gravity all-zero, roll 0.
 */
#include "imu.h"

esp_err_t imu_init(void) { return ESP_OK; }

bool imu_is_ready(void) { return false; }

bool imu_read_gravity(float g[3]) {
    if (g) {
        g[0] = g[1] = g[2] = 0.0f;
    }
    return false;
}

bool imu_read_roll(float *angle_rad) {
    if (angle_rad) {
        *angle_rad = 0.0f;
    }
    return false;
}
