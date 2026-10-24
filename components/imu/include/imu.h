/* QMI8658 6-axis IMU (on-board, shared BSP I2C bus).
 *
 * Minimal gravity reader for the moon phase UI's tilt-driven
 * "ball-in-hand" rotation (components/ui/moon_ui.c): the screen-plane
 * roll of the device from the accelerometer's gravity vector,
 * EMA-smoothed. No gyro, no FIFO, no interrupts -- one 6-byte burst read
 * per call at the UI tick rate (<= 20 Hz on a 50 ms tick).
 *
 * Bus: the shared BSP I2C master (bsp_i2c_get_handle()). The board also
 * has the PCF85063A RTC @0x51, TCA9554 @0x20 and the CST9217 touch @0x15;
 * the QMI8658 sits at 0x6A (SAD low; 0x6B is probed as a fallback), so
 * there is no address conflict.
 */
#ifndef IMU_H
#define IMU_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Find + verify the QMI8658 (WHO_AM_I @0x00 == 0x05), soft-reset it,
 * enable the accelerometer and calibrate "1 g" to the at-rest sample
 * magnitude, logging the calibrated axis vector. Soft-fails (the moon
 * UI stays touch-drag-only) when the chip is absent. */
esp_err_t imu_init(void);

/* True once init verified the chip. */
bool imu_is_ready(void);

/* Screen-plane gravity angle in radians (atan2(gy, gx) in screen coords,
 * x right, y down), EMA-smoothed with shortest-path wrap across the
 * +/-pi seam. Reads the 6 accelerometer bytes (0x35..0x3A); returns
 * false on I2C error or an implausible |g| (< 0.5 g / > 1.5 g). The
 * consumer applies its own orientation offset + sign (the board's IMU
 * mounting is not known at driver level). */
bool imu_read_roll(float *angle_rad);

#ifdef __cplusplus
}
#endif

#endif /* IMU_H */
