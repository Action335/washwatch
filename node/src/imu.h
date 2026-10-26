// MPU6050 over raw I2C: just enough to read the acceleration magnitude.
#pragma once

// Wakes the sensor and sets +-4 g with a 44 Hz low-pass filter. Returns false if it didn't answer.
bool imu_begin();
// |a| in g (about 1.0 at rest). Returns false if the read failed.
bool imu_read_magnitude(float& mag_g);
