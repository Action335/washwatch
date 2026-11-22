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

// Training features from the three axes (the 7 Nov recording showed sideways shake and band
// energy separate the phases far better than std of |a|).
constexpr uint32_t FEATURE_SAMPLE_HZ = 100;
constexpr uint32_t FEATURE_SAMPLES_PER_WINDOW = FEATURE_SAMPLE_HZ * SEND_PERIOD_MS / 1000;

// Second-order section (RBJ band-pass, 0 dB peak), transposed direct form II.
struct Biquad {
  float b0 = 0, b2 = 0, a1 = 0, a2 = 0;  // b1 = 0 for a band-pass
  float z1 = 0, z2 = 0;
  static Biquad band_pass(float f0_hz, float q, float fs_hz);
  float step(float x);
};

// 4th-order band-pass: two identical sections, for a cleaner split between the bands.
struct BandPass {
  Biquad s[2];
  BandPass(float lo_hz, float hi_hz, float fs_hz);
  float step(float x) { return s[1].step(s[0].step(x)); }
};

class AxisFeatures {
 public:
  AxisFeatures();
  // One sample in g. The filters run continuously across windows; reset_window() clears only the sums.
  void add(float x, float y, float z);
  void reset_window();
  // Writes vstd_mg, lo_mg and hi_mg (all 0 for an empty window).
  void fill(node_msg_t& m) const;

 private:
  BandPass lo_[3], hi_[3];
  uint32_t n_ = 0;
  double sum_[3] = {0, 0, 0}, sum_sq_[3] = {0, 0, 0};
  double lo_sq_ = 0, hi_sq_ = 0;
};
