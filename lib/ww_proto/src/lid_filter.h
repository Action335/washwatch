#pragma once
#include <stdint.h>

// The gateway's view of one washer's lid. A reed switch at the edge of its magnet's reach chatters
// (open/closed every 1-5 s, seen on site 22 Nov 2026), and a spinning drum shakes the lid, so a new
// reading only counts once it has held for HOLD_MS. Updated on every message from the node: lid
// events arrive at once and windows every 2 s, so a real change shows within one or two windows.
class LidFilter {
 public:
  static constexpr uint32_t HOLD_MS = 2000;

  bool update(bool raw_open, uint32_t now_ms) {
    if (!init_) {
      init_ = true;
      stable_ = candidate_ = raw_open;
      since_ms_ = now_ms;
      return stable_;
    }
    if (raw_open != candidate_) {
      candidate_ = raw_open;
      since_ms_ = now_ms;
    }
    // Unsigned subtraction stays correct across millis() wraparound.
    if (candidate_ != stable_ && now_ms - since_ms_ >= HOLD_MS) stable_ = candidate_;
    return stable_;
  }

 private:
  bool init_ = false;
  bool stable_ = false;
  bool candidate_ = false;
  uint32_t since_ms_ = 0;
};
