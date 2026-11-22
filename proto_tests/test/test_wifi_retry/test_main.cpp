#include <unity.h>
#include "wifi_retry.h"

void setUp() {}
void tearDown() {}

void test_unknown_channel_always_scans() {
  for (uint32_t a = 1; a <= 12; a++) TEST_ASSERT_EQUAL_UINT8(0, wifi_retry_channel(0, a));
}

void test_first_attempts_reuse_the_last_channel() {
  for (uint32_t a = 1; a < WIFI_SCAN_EVERY; a++) TEST_ASSERT_EQUAL_UINT8(9, wifi_retry_channel(9, a));
}

void test_every_nth_attempt_scans_all_channels() {
  // A router that moved from channel 9 to 10 is found on attempt 5, 10, ...
  int scans = 0;
  for (uint32_t a = 1; a <= 3 * WIFI_SCAN_EVERY; a++) scans += wifi_retry_channel(9, a) == 0;
  TEST_ASSERT_EQUAL(3, scans);
  TEST_ASSERT_EQUAL_UINT8(0, wifi_retry_channel(9, WIFI_SCAN_EVERY));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_channel_always_scans);
  RUN_TEST(test_first_attempts_reuse_the_last_channel);
  RUN_TEST(test_every_nth_attempt_scans_all_channels);
  return UNITY_END();
}
