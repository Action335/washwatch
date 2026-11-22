// MPU6050 over raw I2C: just enough to read the acceleration magnitude.
#pragma once

#include <stdint.h>

// Starts I2C, then imu_wake(). Returns false if the sensor didn't answer.
bool imu_begin();
// Wakes the sensor and sets +-4 g with a 44 Hz low-pass filter. Safe to call again at any time
// (the MPU6050 comes back asleep after a power dip). Returns false if it didn't answer.
bool imu_wake();
// Raw x, y, z counts (8192 per g). Returns false if the read failed.
bool imu_read_raw(int16_t a[3]);
// x, y, z in g (|a| about 1.0 at rest). Returns false if the read failed or gave (0, 0, 0) (sensor asleep).
bool imu_read_g(float a_g[3]);
