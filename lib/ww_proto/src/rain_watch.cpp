#include "rain_watch.h"

bool RainWatch::update(uint16_t raw, uint32_t now_ms) {
  // Unsigned subtraction keeps working when millis() wraps.
  if (!wet_ && raw < RAIN_WET_RAW) {
    if (!armed_ && now_ms - since_ms_ >= RAIN_REARM_MS) armed_ = true;  // a new rain after a long dry spell
    wet_ = true;
    since_ms_ = now_ms;
  } else if (wet_ && raw > RAIN_DRY_RAW) {
    wet_ = false;
    since_ms_ = now_ms;
  }
  if (wet_ && armed_ && now_ms - since_ms_ >= RAIN_CONFIRM_MS) {
    armed_ = false;
    return true;
  }
  return false;
}
