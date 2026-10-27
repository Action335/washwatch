#include <unity.h>
#include "rain_watch.h"

void setUp() {}
void tearDown() {}

constexpr uint16_t DRY = 4000, WET = 1500, BETWEEN = 2750;

// Feeds one reading every second from t0 up to (not including) t1; returns how many alerts fired.
static int feed(RainWatch& r, uint16_t raw, uint32_t t0, uint32_t t1) {
  int alerts = 0;
  for (uint32_t t = t0; t != t1; t += 1000) alerts += r.update(raw, t) ? 1 : 0;
  return alerts;
}

void test_dry_never_alerts() {
  RainWatch r;
  TEST_ASSERT_EQUAL(0, feed(r, DRY, 0, 3600000));
}

void test_a_short_splash_does_not_alert() {
  RainWatch r;
  TEST_ASSERT_EQUAL(0, feed(r, WET, 0, RAIN_CONFIRM_MS - 5000));
  TEST_ASSERT_EQUAL(0, feed(r, DRY, RAIN_CONFIRM_MS - 5000, 60000));
  // The splash's wet time does not count toward the next one.
  TEST_ASSERT_EQUAL(0, feed(r, WET, 60000, 60000 + RAIN_CONFIRM_MS - 5000));
}

void test_steady_rain_alerts_once() {
  RainWatch r;
  TEST_ASSERT_EQUAL(1, feed(r, WET, 0, 3600000));
}

void test_alerts_after_the_confirm_time() {
  RainWatch r;
  TEST_ASSERT_EQUAL(0, feed(r, WET, 0, RAIN_CONFIRM_MS));
  TEST_ASSERT_TRUE(r.update(WET, RAIN_CONFIRM_MS));
}

void test_no_new_alert_until_dry_long_enough() {
  RainWatch r;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL(1, feed(r, WET, t, t + 60000));
  t += 60000;
  TEST_ASSERT_EQUAL(0, feed(r, DRY, t, t + RAIN_REARM_MS - 60000));  // a short dry spell
  t += RAIN_REARM_MS - 60000;
  TEST_ASSERT_EQUAL(0, feed(r, WET, t, t + 60000));  // same rain, no second alert
  t += 60000;
  TEST_ASSERT_EQUAL(0, feed(r, DRY, t, t + RAIN_REARM_MS));  // dry long enough: re-armed
  t += RAIN_REARM_MS;
  TEST_ASSERT_EQUAL(1, feed(r, WET, t, t + 60000));  // a new rain alerts again
}

void test_reading_between_thresholds_keeps_the_state() {
  RainWatch r;
  // Wet, then readings in the hysteresis band: still wet, so the alert still comes on time.
  TEST_ASSERT_EQUAL(0, feed(r, WET, 0, 10000));
  TEST_ASSERT_EQUAL(1, feed(r, BETWEEN, 10000, RAIN_CONFIRM_MS + 1000));
  // Dry readings never start rain from the band either.
  RainWatch d;
  TEST_ASSERT_EQUAL(0, feed(d, BETWEEN, 0, 3600000));
}

void test_survives_millis_wraparound() {
  RainWatch r;
  uint32_t t0 = 0xFFFFFFFFu - 10000 + 1;  // wraps 10 s in
  TEST_ASSERT_EQUAL(1, feed(r, WET, t0, t0 + RAIN_CONFIRM_MS + 1000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dry_never_alerts);
  RUN_TEST(test_a_short_splash_does_not_alert);
  RUN_TEST(test_steady_rain_alerts_once);
  RUN_TEST(test_alerts_after_the_confirm_time);
  RUN_TEST(test_no_new_alert_until_dry_long_enough);
  RUN_TEST(test_reading_between_thresholds_keeps_the_state);
  RUN_TEST(test_survives_millis_wraparound);
  return UNITY_END();
}
