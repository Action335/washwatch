// Rain detection for the "collect your laundry" alert, from the gateway's rain sensor
// (FC-37/YL-83: the analog output falls when the plate is wet). Time is passed in, so it
// runs on the PC too.
#pragma once
#include <stdint.h>

// ADC 0-4095. Uncalibrated: drip water on the plate and read rain_raw on the dashboard.
// The gap between the two is hysteresis, so a reading near the edge doesn't flip-flop.
constexpr uint16_t RAIN_WET_RAW = 2500;
constexpr uint16_t RAIN_DRY_RAW = 3000;
constexpr uint32_t RAIN_CONFIRM_MS = 30000;  // wet this long before alerting: a splash is not rain
constexpr uint32_t RAIN_REARM_MS = 600000;   // dry this long before a new rain alerts again

class RainWatch {
 public:
  // Call with every reading. Returns true once when rain starts.
  bool update(uint16_t raw, uint32_t now_ms);

 private:
  bool wet_ = false;
  bool armed_ = true;
  uint32_t since_ms_ = 0;  // when the plate last turned wet or dry
};
