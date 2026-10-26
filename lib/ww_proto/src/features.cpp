#include "features.h"

#include <math.h>

uint8_t phase_from_std(uint16_t std_mg) {
  if (std_mg >= STD_SPIN_MG) return PHASE_SPIN;
  if (std_mg >= STD_WASH_MG) return PHASE_WASH;
  return PHASE_IDLE;
}

void VibrationWindow::add(float mag_g) {
  if (n_ == 0) {
    min_ = max_ = mag_g;
  } else {
    if (mag_g < min_) min_ = mag_g;
    if (mag_g > max_) max_ = mag_g;
  }
  n_++;
  sum_ += mag_g;
  sum_sq_ += (double)mag_g * mag_g;
}

void VibrationWindow::reset() { *this = VibrationWindow(); }

static uint16_t to_mg(double g) {
  double mg = round(g * 1000.0);
  if (mg < 0) return 0;
  return mg > 65535 ? 65535 : (uint16_t)mg;
}

void VibrationWindow::fill(node_msg_t& m) const {
  if (n_ == 0) {
    m.mean_mg = m.std_mg = m.p2p_mg = 0;
    return;
  }
  double mean = sum_ / n_;
  double var = sum_sq_ / n_ - mean * mean;  // population variance
  m.mean_mg = to_mg(mean);
  m.std_mg = to_mg(var > 0 ? sqrt(var) : 0);
  m.p2p_mg = to_mg(max_ - min_);
}
