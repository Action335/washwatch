#include <unity.h>
#include "lid_filter.h"

void setUp() {}
void tearDown() {}

void test_first_reading_is_taken_as_is() {
  LidFilter a, b;
  TEST_ASSERT_TRUE(a.update(true, 100));
  TEST_ASSERT_FALSE(b.update(false, 100));
}

void test_change_counts_once_it_has_held() {
  LidFilter f;
  f.update(false, 0);
  TEST_ASSERT_FALSE(f.update(true, 10000));  // lid event: not yet
  TEST_ASSERT_FALSE(f.update(true, 11900));  // next window, 1.9 s later
  TEST_ASSERT_TRUE(f.update(true, 13900));   // the one after
}

void test_chatter_is_ignored() {
  // On-site pattern: closed, then open for 1-2 s at a time.
  LidFilter f;
  f.update(false, 0);
  uint32_t t = 1000;
  for (int i = 0; i < 20; i++) {
    TEST_ASSERT_FALSE(f.update(true, t));
    TEST_ASSERT_FALSE(f.update(true, t + 1500));
    TEST_ASSERT_FALSE(f.update(false, t + 1800));
    t += 5000;
  }
}

void test_closing_is_held_too() {
  LidFilter f;
  f.update(true, 0);
  TEST_ASSERT_TRUE(f.update(false, 5000));
  TEST_ASSERT_TRUE(f.update(true, 5500));    // bounced back open: closed never held
  TEST_ASSERT_TRUE(f.update(false, 6000));
  TEST_ASSERT_FALSE(f.update(false, 8000));
}

void test_hold_survives_millis_wrap() {
  LidFilter f;
  f.update(false, 0xFFFFFF00u);
  TEST_ASSERT_FALSE(f.update(true, 0xFFFFFFF0u));
  TEST_ASSERT_TRUE(f.update(true, 0x00000800u));  // 2 s + after the wrap
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_reading_is_taken_as_is);
  RUN_TEST(test_change_counts_once_it_has_held);
  RUN_TEST(test_chatter_is_ignored);
  RUN_TEST(test_closing_is_held_too);
  RUN_TEST(test_hold_survives_millis_wrap);
  return UNITY_END();
}
