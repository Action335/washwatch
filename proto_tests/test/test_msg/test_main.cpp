#include <string.h>
#include <unity.h>
#include "airbox_msg.h"

void setUp() {}
void tearDown() {}

void test_struct_sizes() {
  TEST_ASSERT_EQUAL(33, sizeof(node_msg_t));
  TEST_ASSERT_EQUAL(6, sizeof(ack_msg_t));
  TEST_ASSERT_TRUE(sizeof(node_msg_t) <= 250);
}

void test_node_id_range() {
  TEST_ASSERT_FALSE(is_valid_node_id(0));
  TEST_ASSERT_TRUE(is_valid_node_id(1));
  TEST_ASSERT_TRUE(is_valid_node_id(254));
  TEST_ASSERT_FALSE(is_valid_node_id(255));
}

void test_bytes_roundtrip() {
  node_msg_t a = {};
  a.ver = MSG_VERSION;
  a.node_id = 2;
  a.seq = 70000;
  a.pm25 = 42;
  a.temp_c10 = -55;
  a.lux = 800;
  uint8_t bytes[33];
  memcpy(bytes, &a, sizeof(a));
  node_msg_t b;
  memcpy(&b, bytes, sizeof(b));
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
  TEST_ASSERT_EQUAL(2, b.node_id);
  TEST_ASSERT_EQUAL_UINT32(70000, b.seq);
  TEST_ASSERT_EQUAL(42, b.pm25);
  TEST_ASSERT_EQUAL(-55, b.temp_c10);
  TEST_ASSERT_EQUAL(800, b.lux);
}

void test_next_channel_cycles_1_to_13() {
  TEST_ASSERT_EQUAL(7, next_channel(6));
  TEST_ASSERT_EQUAL(1, next_channel(13));
  TEST_ASSERT_EQUAL(1, next_channel(0));
  TEST_ASSERT_EQUAL(1, next_channel(14));
  uint8_t ch = 6;
  for (int i = 0; i < WIFI_CHANNEL_MAX; i++) ch = next_channel(ch);
  TEST_ASSERT_EQUAL(6, ch);  // a full sweep visits all 13 and returns to the start
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_struct_sizes);
  RUN_TEST(test_node_id_range);
  RUN_TEST(test_bytes_roundtrip);
  RUN_TEST(test_next_channel_cycles_1_to_13);
  return UNITY_END();
}
