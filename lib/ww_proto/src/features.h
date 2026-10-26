// Node-side vibration features: running stats of |a| over one 2 s window, and the
// rule-based phase that stands in for the on-device classifier until it is trained.
#pragma once
#include <stdint.h>
#include "ww_msg.h"

// UNCALIBRATED guesses. Set them from the phyphox recording of a real cycle.
constexpr uint16_t STD_WASH_MG = 20;   // std of |a| at or above this = drum moving
constexpr uint16_t STD_SPIN_MG = 150;  // at or above this = spinning

uint8_t phase_from_std(uint16_t std_mg);

class VibrationWindow {
 public:
  void add(float mag_g);
  void reset();
  uint32_t count() const { return n_; }
  // Writes mean_mg, std_mg and p2p_mg (all 0 for an empty window).
  void fill(node_msg_t& m) const;

 private:
  uint32_t n_ = 0;
  double sum_ = 0, sum_sq_ = 0;
  float min_ = 0, max_ = 0;
};
