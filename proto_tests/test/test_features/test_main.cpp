#include <math.h>
#include <unity.h>
#include "features.h"

void setUp() {}
void tearDown() {}

void test_empty_window_is_zero() {
  VibrationWindow w;
  node_msg_t m = {};
  w.fill(m);
  TEST_ASSERT_EQUAL(0, w.count());
  TEST_ASSERT_EQUAL(0, m.mean_mg);
  TEST_ASSERT_EQUAL(0, m.std_mg);
  TEST_ASSERT_EQUAL(0, m.p2p_mg);
}

void test_still_washer_has_no_spread() {
  VibrationWindow w;
  for (int i = 0; i < 200; i++) w.add(1.0f);
  node_msg_t m = {};
  w.fill(m);
  TEST_ASSERT_EQUAL(1000, m.mean_mg);
  TEST_ASSERT_EQUAL(0, m.std_mg);
  TEST_ASSERT_EQUAL(0, m.p2p_mg);
}

void test_square_wave_stats() {
  // Alternating 0.9 g / 1.1 g: mean 1 g, std 0.1 g, peak-to-peak 0.2 g.
  VibrationWindow w;
  for (int i = 0; i < 200; i++) w.add(i % 2 ? 1.1f : 0.9f);
  node_msg_t m = {};
  w.fill(m);
  TEST_ASSERT_INT_WITHIN(1, 1000, m.mean_mg);
  TEST_ASSERT_INT_WITHIN(1, 100, m.std_mg);
  TEST_ASSERT_INT_WITHIN(1, 200, m.p2p_mg);
}

void test_reset_starts_a_new_window() {
  VibrationWindow w;
  w.add(3.0f);
  w.reset();
  w.add(1.0f);
  node_msg_t m = {};
  w.fill(m);
  TEST_ASSERT_EQUAL(1, w.count());
  TEST_ASSERT_EQUAL(1000, m.mean_mg);
  TEST_ASSERT_EQUAL(0, m.p2p_mg);
}

void test_large_values_saturate() {
  VibrationWindow w;
  w.add(0.0f);
  w.add(100.0f);  // far above uint16 milli-g
  node_msg_t m = {};
  w.fill(m);
  TEST_ASSERT_EQUAL(65535, m.p2p_mg);
}

void test_phase_rule_thresholds() {
  TEST_ASSERT_EQUAL(PHASE_IDLE, phase_from_std(0));
  TEST_ASSERT_EQUAL(PHASE_IDLE, phase_from_std(STD_WASH_MG - 1));
  TEST_ASSERT_EQUAL(PHASE_WASH, phase_from_std(STD_WASH_MG));
  TEST_ASSERT_EQUAL(PHASE_WASH, phase_from_std(STD_SPIN_MG - 1));
  TEST_ASSERT_EQUAL(PHASE_SPIN, phase_from_std(STD_SPIN_MG));
}

// --- AxisFeatures: vector std and band RMS from x, y, z ---

static const float PI_F = 3.14159265f;

// Feeds `windows` windows of a signal; returns the fill of the last one (earlier ones settle the filters).
template <typename F>
static node_msg_t run_axes(F signal, int windows = 3) {
  AxisFeatures f;
  node_msg_t m = {};
  int i = 0;
  for (int w = 0; w < windows; w++) {
    f.reset_window();
    for (uint32_t k = 0; k < FEATURE_SAMPLES_PER_WINDOW; k++, i++) {
      float t = (float)i / FEATURE_SAMPLE_HZ;
      float a[3];
      signal(t, a);
      f.add(a[0], a[1], a[2]);
    }
  }
  f.fill(m);
  return m;
}

void test_axes_empty_window_is_zero() {
  AxisFeatures f;
  node_msg_t m = {};
  m.vstd_mg = m.lo_mg = m.hi_mg = 99;
  f.fill(m);
  TEST_ASSERT_EQUAL(0, m.vstd_mg);
  TEST_ASSERT_EQUAL(0, m.lo_mg);
  TEST_ASSERT_EQUAL(0, m.hi_mg);
}

void test_axes_gravity_alone_is_quiet() {
  // A tilted but still sensor: gravity split over two axes, no vibration.
  node_msg_t m = run_axes([](float, float a[3]) {
    a[0] = 0.6f;
    a[1] = 0.0f;
    a[2] = 0.8f;
  });
  TEST_ASSERT_EQUAL(0, m.vstd_mg);
  TEST_ASSERT_INT_WITHIN(1, 0, m.lo_mg);
  TEST_ASSERT_INT_WITHIN(1, 0, m.hi_mg);
}

void test_axes_spin_frequency_lands_in_low_band() {
  // 11.7 Hz sideways shake (the measured spin), 0.1 g peak on x, gravity on z.
  node_msg_t m = run_axes([](float t, float a[3]) {
    a[0] = 0.1f * sinf(2 * PI_F * 11.7f * t);
    a[1] = 0.0f;
    a[2] = 1.0f;
  });
  TEST_ASSERT_INT_WITHIN(3, 71, m.vstd_mg);  // RMS of a 100 mg sine
  // 11.7 Hz sits towards the band edge: the 4th-order filter passes |H| ~ 0.78 there (71 * 0.78 = 55).
  TEST_ASSERT_INT_WITHIN(6, 55, m.lo_mg);
  TEST_ASSERT_TRUE(m.hi_mg < m.lo_mg / 2);
}

void test_axes_motor_hum_lands_in_high_band() {
  // 25 Hz hum on y: wash/rinse motor.
  node_msg_t m = run_axes([](float t, float a[3]) {
    a[0] = 0.0f;
    a[1] = 0.1f * sinf(2 * PI_F * 25.0f * t);
    a[2] = 1.0f;
  });
  TEST_ASSERT_INT_WITHIN(10, 71, m.hi_mg);
  TEST_ASSERT_TRUE(m.lo_mg < m.hi_mg / 2);
}

void test_axes_vector_std_adds_axes() {
  // Sideways shake barely moves |a| but the vector std sees it on every axis.
  node_msg_t m = run_axes([](float t, float a[3]) {
    a[0] = 0.1f * sinf(2 * PI_F * 10.0f * t);
    a[1] = 0.1f * sinf(2 * PI_F * 20.0f * t);
    a[2] = 1.0f;
  });
  TEST_ASSERT_INT_WITHIN(3, 100, m.vstd_mg);  // sqrt(71^2 + 71^2)
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_axes_empty_window_is_zero);
  RUN_TEST(test_axes_gravity_alone_is_quiet);
  RUN_TEST(test_axes_spin_frequency_lands_in_low_band);
  RUN_TEST(test_axes_motor_hum_lands_in_high_band);
  RUN_TEST(test_axes_vector_std_adds_axes);
  RUN_TEST(test_empty_window_is_zero);
  RUN_TEST(test_still_washer_has_no_spread);
  RUN_TEST(test_square_wave_stats);
  RUN_TEST(test_reset_starts_a_new_window);
  RUN_TEST(test_large_values_saturate);
  RUN_TEST(test_phase_rule_thresholds);
  return UNITY_END();
}
