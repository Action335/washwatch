// MPU6050 health on the node: spot a sensor that answers I2C but returns no data, and pace re-wakes.
#pragma once

#include <stdint.h>

// A real accelerometer always sees gravity, so exactly (0, 0, 0) is never a still washer: it means
// the MPU6050 is asleep (it powers up asleep) or reset. Treat it as a failed read, not as "idle".
inline bool imu_sample_plausible(const int16_t a[3]) { return a[0] != 0 || a[1] != 0 || a[2] != 0; }

// Decides when to re-run the wake sequence while reads keep failing.
class ImuRecovery {
 public:
  static constexpr uint32_t RETRY_EVERY = 200;  // samples: 2 s at 100 Hz

  // Call once per sample with whether the read gave usable data. Returns true when the caller
  // should re-wake the sensor: on the first bad sample of a run, then every RETRY_EVERY samples.
  bool update(bool ok) {
    if (ok) {
      bad_run_ = 0;
      return false;
    }
    return bad_run_++ % RETRY_EVERY == 0;
  }

 private:
  uint32_t bad_run_ = 0;
};
