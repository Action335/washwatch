#include <string.h>
#include <unity.h>
#include "pms5003.h"

void setUp() {}
void tearDown() {}

// Builds a valid 32-byte PMS5003 frame with a correct checksum.
static void make_frame(uint8_t f[32], uint16_t pm1, uint16_t pm25, uint16_t pm10,
                       uint16_t n03, uint16_t n05, uint16_t n10, uint16_t n25) {
  memset(f, 0, 32);
  f[0] = 0x42;
  f[1] = 0x4D;
  f[3] = 28;
  const uint16_t words[] = {pm1, pm25, pm10, n03, n05, n10, n25};
  for (int i = 0; i < 7; i++) {  // atmospheric PM at bytes 10-15, counts at 16-23
    f[10 + 2 * i] = words[i] >> 8;
    f[11 + 2 * i] = words[i] & 0xFF;
  }
  uint16_t sum = 0;
  for (int i = 0; i < 30; i++) sum += f[i];
  f[30] = sum >> 8;
  f[31] = sum & 0xFF;
}

static int feed_all(Pms5003Parser& p, const uint8_t* b, size_t n) {
  int completed = 0;
  for (size_t i = 0; i < n; i++)
    if (p.feed(b[i])) completed++;
  return completed;
}

void test_parses_valid_frame() {
  Pms5003Parser p;
  uint8_t f[32];
  make_frame(f, 30, 42, 55, 9000, 2500, 400, 40);
  for (int i = 0; i < 31; i++) TEST_ASSERT_FALSE(p.feed(f[i]));
  TEST_ASSERT_TRUE(p.feed(f[31]));
  TEST_ASSERT_EQUAL(30, p.reading().pm1);
  TEST_ASSERT_EQUAL(42, p.reading().pm25);
  TEST_ASSERT_EQUAL(55, p.reading().pm10);
  TEST_ASSERT_EQUAL(9000, p.reading().n03);
  TEST_ASSERT_EQUAL(40, p.reading().n25);
}

void test_rejects_bad_checksum() {
  Pms5003Parser p;
  uint8_t good[32], bad[32];
  make_frame(good, 1, 11, 21, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL(1, feed_all(p, good, 32));
  make_frame(bad, 30, 42, 55, 0, 0, 0, 0);
  bad[12] ^= 0xFF;
  TEST_ASSERT_EQUAL(0, feed_all(p, bad, 32));
  TEST_ASSERT_EQUAL_UINT32(1, p.checksum_errors());
  TEST_ASSERT_EQUAL(11, p.reading().pm25);
}

void test_resyncs_after_garbage() {
  Pms5003Parser p;
  const uint8_t junk[] = {0x00, 0x42, 0x42, 0x4D, 0x13};
  uint8_t f[32];
  make_frame(f, 0, 77, 0, 0, 0, 0, 0);
  int n = feed_all(p, junk, sizeof(junk)) + feed_all(p, f, 32);
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_EQUAL(77, p.reading().pm25);
}

void test_two_frames_back_to_back() {
  Pms5003Parser p;
  uint8_t a[32], b[32];
  make_frame(a, 0, 10, 0, 0, 0, 0, 0);
  make_frame(b, 0, 20, 0, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL(2, feed_all(p, a, 32) + feed_all(p, b, 32));
  TEST_ASSERT_EQUAL(20, p.reading().pm25);
}

void test_bad_length_field_resyncs() {
  Pms5003Parser p;
  uint8_t bad[32], good[32];
  make_frame(bad, 0, 5, 0, 0, 0, 0, 0);
  bad[2] = 0x00;
  bad[3] = 0x99;
  make_frame(good, 0, 66, 0, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL(1, feed_all(p, bad, 32) + feed_all(p, good, 32));
  TEST_ASSERT_EQUAL(66, p.reading().pm25);
}

void test_partial_frame_then_good_frame() {
  Pms5003Parser p;
  uint8_t a[32], b[32];
  make_frame(a, 0, 11, 0, 0, 0, 0, 0);
  make_frame(b, 0, 88, 0, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL(1, feed_all(p, a, 20) + feed_all(p, b, 32));
  TEST_ASSERT_EQUAL(88, p.reading().pm25);
}

void test_reset_drops_partial_frame() {
  Pms5003Parser p;
  uint8_t a[32], b[32];
  make_frame(a, 0, 11, 0, 0, 0, 0, 0);
  make_frame(b, 0, 99, 0, 0, 0, 0, 0);
  feed_all(p, a, 20);
  p.reset();
  TEST_ASSERT_EQUAL(1, feed_all(p, b, 32));
  TEST_ASSERT_EQUAL_UINT32(0, p.checksum_errors());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_valid_frame);
  RUN_TEST(test_rejects_bad_checksum);
  RUN_TEST(test_resyncs_after_garbage);
  RUN_TEST(test_two_frames_back_to_back);
  RUN_TEST(test_bad_length_field_resyncs);
  RUN_TEST(test_partial_frame_then_good_frame);
  RUN_TEST(test_reset_drops_partial_frame);
  return UNITY_END();
}
