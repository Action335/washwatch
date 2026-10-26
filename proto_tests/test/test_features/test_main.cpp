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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_window_is_zero);
  RUN_TEST(test_still_washer_has_no_spread);
  RUN_TEST(test_square_wave_stats);
  RUN_TEST(test_reset_starts_a_new_window);
  RUN_TEST(test_large_values_saturate);
  RUN_TEST(test_phase_rule_thresholds);
  return UNITY_END();
}
