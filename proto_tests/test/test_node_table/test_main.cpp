#include <unity.h>
#include "node_table.h"

void setUp() {}
void tearDown() {}

static node_msg_t msg(uint8_t id, uint32_t seq, uint32_t uptime) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = id;
  m.seq = seq;
  m.uptime_ms = uptime;
  return m;
}

void test_accepts_new_and_tracks_count() {
  NodeTable t;
  TEST_ASSERT_TRUE(t.ingest(msg(1, 0, 100), 0) == Ingest::Accepted);
  TEST_ASSERT_TRUE(t.ingest(msg(2, 0, 100), 0) == Ingest::Accepted);
  TEST_ASSERT_EQUAL(2, t.count());
}

void test_duplicate_dropped() {
  NodeTable t;
  t.ingest(msg(1, 10, 1000), 0);
  TEST_ASSERT_TRUE(t.ingest(msg(1, 10, 1000), 5) == Ingest::Duplicate);
  TEST_ASSERT_EQUAL_UINT32(1, t.find(1)->received);
}

void test_reboot_resets_sequence() {
  NodeTable t;
  t.ingest(msg(1, 500, 1000000), 0);
  TEST_ASSERT_TRUE(t.ingest(msg(1, 0, 2000), 10) == Ingest::Accepted);
  TEST_ASSERT_EQUAL_UINT32(0, t.find(1)->last.seq);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, t.pdr(1));
}

void test_pdr_counts_gaps() {
  NodeTable t;
  const uint32_t seqs[] = {1, 2, 4, 5};
  for (uint32_t i = 0; i < 4; i++) t.ingest(msg(1, seqs[i], 1000 * (i + 1)), i);
  TEST_ASSERT_EQUAL_FLOAT(0.8f, t.pdr(1));
}

void test_offline_after_30s() {
  NodeTable t;
  t.ingest(msg(1, 0, 100), 0);
  TEST_ASSERT_TRUE(t.online(1, 29999));
  TEST_ASSERT_FALSE(t.online(1, 30000));
}

void test_rejects_invalid_ids_and_full_table() {
  NodeTable t;
  TEST_ASSERT_TRUE(t.ingest(msg(0, 0, 100), 0) == Ingest::Rejected);
  TEST_ASSERT_TRUE(t.ingest(msg(255, 0, 100), 0) == Ingest::Rejected);
  for (uint8_t id = 1; id <= 8; id++) {
    node_msg_t m = msg(id, 0, 100);
    m.pm25 = 10 * id;
    TEST_ASSERT_TRUE(t.ingest(m, 0) == Ingest::Accepted);
  }
  TEST_ASSERT_TRUE(t.ingest(msg(9, 0, 100), 0) == Ingest::Rejected);
  TEST_ASSERT_EQUAL(8, t.count());
  TEST_ASSERT_EQUAL(10, t.find(1)->last.pm25);
  TEST_ASSERT_NULL(t.find(9));
}

void test_rejects_wrong_version() {
  NodeTable t;
  node_msg_t m = msg(1, 0, 100);
  m.ver = 2;
  TEST_ASSERT_TRUE(t.ingest(m, 0) == Ingest::Rejected);
  TEST_ASSERT_EQUAL(0, t.count());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_accepts_new_and_tracks_count);
  RUN_TEST(test_duplicate_dropped);
  RUN_TEST(test_reboot_resets_sequence);
  RUN_TEST(test_pdr_counts_gaps);
  RUN_TEST(test_offline_after_30s);
  RUN_TEST(test_rejects_invalid_ids_and_full_table);
  RUN_TEST(test_rejects_wrong_version);
  return UNITY_END();
}
