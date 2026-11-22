#include <unity.h>
#include "imu_health.h"

void setUp() {}
void tearDown() {}

void test_all_zero_sample_is_not_plausible() {
  const int16_t zero[3] = {0, 0, 0};
  TEST_ASSERT_FALSE(imu_sample_plausible(zero));
}

void test_gravity_sample_is_plausible() {
  const int16_t rest[3] = {7735, 3278, 2306};  // node 2 at rest, about 1.06 g
  TEST_ASSERT_TRUE(imu_sample_plausible(rest));
  const int16_t one_axis[3] = {0, 0, -1};
  TEST_ASSERT_TRUE(imu_sample_plausible(one_axis));
}

void test_good_samples_never_ask_for_a_wake() {
  ImuRecovery r;
  for (int i = 0; i < 1000; i++) TEST_ASSERT_FALSE(r.update(true));
}

void test_first_bad_sample_asks_for_a_wake() {
  ImuRecovery r;
  TEST_ASSERT_TRUE(r.update(false));
}

void test_bad_run_retries_every_period() {
  ImuRecovery r;
  int wakes = 0;
  for (uint32_t i = 0; i < 3 * ImuRecovery::RETRY_EVERY; i++) wakes += r.update(false);
  TEST_ASSERT_EQUAL(3, wakes);  // samples 1, 1+N, 1+2N
}

void test_good_sample_resets_the_run() {
  ImuRecovery r;
  TEST_ASSERT_TRUE(r.update(false));
  TEST_ASSERT_FALSE(r.update(false));
  TEST_ASSERT_FALSE(r.update(true));
  TEST_ASSERT_TRUE(r.update(false));  // a new bad run wakes straight away
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_zero_sample_is_not_plausible);
  RUN_TEST(test_gravity_sample_is_plausible);
  RUN_TEST(test_good_samples_never_ask_for_a_wake);
  RUN_TEST(test_first_bad_sample_asks_for_a_wake);
  RUN_TEST(test_bad_run_retries_every_period);
  RUN_TEST(test_good_sample_resets_the_run);
  return UNITY_END();
}
