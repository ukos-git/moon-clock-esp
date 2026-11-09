/*
 * Knob shim of components/imu/include/imu.h (same contract, copied):
 * the knob board carries no IMU (bench-verified hardware list), so the
 * shared UI's tilt/orbit surface answers "absent" like the clocks' IMU
 * soft-fail. The retained shared TUs call imu_is_ready()/
 * imu_read_gravity() only (actions.c roll gate); all four names exist so
 * any future shared caller links.
 */
#ifndef IMU_H
#define IMU_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_init(void);
bool imu_is_ready(void);
bool imu_read_gravity(float g[3]);
bool imu_read_roll(float *angle_rad);

#ifdef __cplusplus
}
#endif

#endif /* IMU_H */
