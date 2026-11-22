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

Biquad Biquad::band_pass(float f0_hz, float q, float fs_hz) {
  const float w0 = 2.0f * 3.14159265f * f0_hz / fs_hz;
  const float alpha = sinf(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;
  Biquad b;
  b.b0 = alpha / a0;
  b.b2 = -alpha / a0;
  b.a1 = -2.0f * cosf(w0) / a0;
  b.a2 = (1.0f - alpha) / a0;
  return b;
}

float Biquad::step(float x) {
  float y = b0 * x + z1;
  z1 = -a1 * y + z2;
  z2 = b2 * x - a2 * y;
  return y;
}

BandPass::BandPass(float lo_hz, float hi_hz, float fs_hz) {
  const float f0 = sqrtf(lo_hz * hi_hz);  // geometric centre
  const float q = f0 / (hi_hz - lo_hz);
  s[0] = s[1] = Biquad::band_pass(f0, q, fs_hz);
}

static const float LO_BAND[2] = {5.0f, 15.0f};   // spin (~11.7 Hz on washer 2)
static const float HI_BAND[2] = {15.0f, 40.0f};  // motor hum; 40 Hz stays under the 44 Hz DLPF and 50 Hz Nyquist

AxisFeatures::AxisFeatures()
    : lo_{{LO_BAND[0], LO_BAND[1], FEATURE_SAMPLE_HZ},
          {LO_BAND[0], LO_BAND[1], FEATURE_SAMPLE_HZ},
          {LO_BAND[0], LO_BAND[1], FEATURE_SAMPLE_HZ}},
      hi_{{HI_BAND[0], HI_BAND[1], FEATURE_SAMPLE_HZ},
          {HI_BAND[0], HI_BAND[1], FEATURE_SAMPLE_HZ},
          {HI_BAND[0], HI_BAND[1], FEATURE_SAMPLE_HZ}} {}

void AxisFeatures::add(float x, float y, float z) {
  const float a[3] = {x, y, z};
  n_++;
  for (int i = 0; i < 3; i++) {
    sum_[i] += a[i];
    sum_sq_[i] += (double)a[i] * a[i];
    float l = lo_[i].step(a[i]);
    float h = hi_[i].step(a[i]);
    lo_sq_ += (double)l * l;
    hi_sq_ += (double)h * h;
  }
}

void AxisFeatures::reset_window() {
  n_ = 0;
  for (int i = 0; i < 3; i++) sum_[i] = sum_sq_[i] = 0;
  lo_sq_ = hi_sq_ = 0;
}

void AxisFeatures::fill(node_msg_t& m) const {
  if (n_ == 0) {
    m.vstd_mg = m.lo_mg = m.hi_mg = 0;
    return;
  }
  double var = 0;
  for (int i = 0; i < 3; i++) {
    double mean = sum_[i] / n_;
    double v = sum_sq_[i] / n_ - mean * mean;
    if (v > 0) var += v;
  }
  m.vstd_mg = to_mg(sqrt(var));
  m.lo_mg = to_mg(sqrt(lo_sq_ / n_));
  m.hi_mg = to_mg(sqrt(hi_sq_ / n_));
}
