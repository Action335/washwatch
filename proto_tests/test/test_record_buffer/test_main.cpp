#include <unity.h>
#include "record_buffer.h"

void setUp() {}
void tearDown() {}

static LogRecord rec(uint32_t t) {
  LogRecord r = {};
  r.t_ms = t;
  r.node_id = 1;
  return r;
}

void test_empty_buffer() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  TEST_ASSERT_EQUAL(0, b.size());
  TEST_ASSERT_EQUAL(0, b.peek(out, 4));
  TEST_ASSERT_EQUAL_UINT32(0, b.dropped());
}

void test_peek_is_oldest_first_and_non_destructive() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  b.push(rec(1));
  b.push(rec(2));
  b.push(rec(3));
  TEST_ASSERT_EQUAL(2, b.peek(out, 2));
  TEST_ASSERT_EQUAL_UINT32(1, out[0].t_ms);
  TEST_ASSERT_EQUAL_UINT32(2, out[1].t_ms);
  TEST_ASSERT_EQUAL(3, b.size());
}

void test_pop_then_wraparound() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  b.push(rec(1));
  b.push(rec(2));
  b.push(rec(3));
  b.pop(2);
  b.push(rec(4));
  b.push(rec(5));
  b.push(rec(6));
  TEST_ASSERT_EQUAL(4, b.peek(out, 4));
  TEST_ASSERT_EQUAL_UINT32(3, out[0].t_ms);
  TEST_ASSERT_EQUAL_UINT32(4, out[1].t_ms);
  TEST_ASSERT_EQUAL_UINT32(5, out[2].t_ms);
  TEST_ASSERT_EQUAL_UINT32(6, out[3].t_ms);
}

void test_overflow_drops_oldest_and_counts() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  for (uint32_t t = 1; t <= 6; t++) b.push(rec(t));
  TEST_ASSERT_EQUAL(4, b.size());
  TEST_ASSERT_EQUAL(4, b.peek(out, 4));
  TEST_ASSERT_EQUAL_UINT32(3, out[0].t_ms);
  TEST_ASSERT_EQUAL_UINT32(6, out[3].t_ms);
  TEST_ASSERT_EQUAL_UINT32(2, b.dropped());
}

void test_pop_more_than_size_clamps() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  b.push(rec(1));
  b.push(rec(2));
  b.pop(10);
  TEST_ASSERT_EQUAL(0, b.size());
  b.push(rec(7));
  TEST_ASSERT_EQUAL(1, b.peek(out, 4));
  TEST_ASSERT_EQUAL_UINT32(7, out[0].t_ms);
}

void test_pop_through_after_overflow_pops_only_sent_records() {
  // Sent the 2 oldest (seq 0,1), then a push into the full buffer overwrote seq 0 before the ack.
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  for (uint32_t t = 1; t <= 4; t++) b.push(rec(t));
  TEST_ASSERT_EQUAL_UINT32(0, b.oldest_seq());
  uint32_t end_seq = b.oldest_seq() + 2;
  b.push(rec(5));
  TEST_ASSERT_EQUAL_UINT32(1, b.oldest_seq());
  b.pop_through(end_seq);
  TEST_ASSERT_EQUAL(3, b.peek(out, 4));
  TEST_ASSERT_EQUAL_UINT32(3, out[0].t_ms);  // t 3 was never sent: still here
  TEST_ASSERT_EQUAL_UINT32(5, out[2].t_ms);
}

void test_pop_through_already_gone_is_noop() {
  LogRecord storage[4], out[4];
  RecordBuffer b(storage, 4);
  for (uint32_t t = 1; t <= 6; t++) b.push(rec(t));  // oldest_seq is now 2
  b.pop_through(1);
  TEST_ASSERT_EQUAL(4, b.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_buffer);
  RUN_TEST(test_peek_is_oldest_first_and_non_destructive);
  RUN_TEST(test_pop_then_wraparound);
  RUN_TEST(test_overflow_drops_oldest_and_counts);
  RUN_TEST(test_pop_more_than_size_clamps);
  RUN_TEST(test_pop_through_after_overflow_pops_only_sent_records);
  RUN_TEST(test_pop_through_already_gone_is_noop);
  return UNITY_END();
}
