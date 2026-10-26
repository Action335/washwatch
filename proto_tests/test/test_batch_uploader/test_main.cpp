#include <unity.h>
#include "batch_uploader.h"

void setUp() {}
void tearDown() {}

void test_nothing_buffered_sends_nothing() {
  BatchUploader u;
  TEST_ASSERT_EQUAL(0, u.poll(0, 0));
}

void test_sends_up_to_batch_max() {
  BatchUploader u;
  TEST_ASSERT_EQUAL(LOG_BATCH_MAX, u.poll(120, 0));
  TEST_ASSERT_EQUAL(0, u.poll(120, 1000));  // waiting for the ack
}

void test_ack_pops_and_frees() {
  BatchUploader u;
  TEST_ASSERT_EQUAL(10, u.poll(10, 0));
  TEST_ASSERT_EQUAL(10, u.on_response(u.ackid(), 0));
  TEST_ASSERT_EQUAL(0, u.poll(0, 100));
  TEST_ASSERT_EQUAL(5, u.poll(5, 200));  // free to send the next batch at once
}

void test_timeout_resends_with_new_ackid() {
  BatchUploader u;
  u.poll(10, 0);
  uint32_t id1 = u.ackid();
  TEST_ASSERT_EQUAL(0, u.poll(10, BATCH_ACK_TIMEOUT_MS - 1));
  TEST_ASSERT_EQUAL(10, u.poll(10, BATCH_ACK_TIMEOUT_MS));
  TEST_ASSERT_NOT_EQUAL(id1, u.ackid());
}

void test_stale_ackid_is_ignored() {
  BatchUploader u;
  u.poll(10, 0);
  uint32_t id1 = u.ackid();
  u.poll(10, BATCH_ACK_TIMEOUT_MS);
  TEST_ASSERT_EQUAL(0, u.on_response(id1, 0));  // late ack for the first send
  TEST_ASSERT_EQUAL(10, u.on_response(u.ackid(), 0));
}

void test_error_keeps_records_then_retries() {
  BatchUploader u;
  u.poll(10, 0);
  TEST_ASSERT_EQUAL(0, u.on_response(u.ackid(), 429));
  TEST_ASSERT_EQUAL(10, u.poll(10, 1));  // no need to wait for the timeout
}

void test_rejected_three_times_is_dropped() {
  BatchUploader u;
  uint32_t t = 0;
  for (int i = 0; i < BATCH_MAX_REJECTS - 1; i++) {
    TEST_ASSERT_EQUAL(10, u.poll(10, t++));
    TEST_ASSERT_EQUAL(0, u.on_response(u.ackid(), 400));
  }
  TEST_ASSERT_EQUAL(10, u.poll(10, t++));
  TEST_ASSERT_EQUAL(10, u.on_response(u.ackid(), 400));  // give up: pop them
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
  // The reject count starts again for the next batch.
  TEST_ASSERT_EQUAL(4, u.poll(4, t++));
  TEST_ASSERT_EQUAL(0, u.on_response(u.ackid(), 400));
  TEST_ASSERT_EQUAL(4, u.poll(4, t++));
  TEST_ASSERT_EQUAL(4, u.on_response(u.ackid(), 0));
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
}

void test_disconnect_clears_in_flight() {
  BatchUploader u;
  u.poll(10, 0);
  u.on_disconnect();
  TEST_ASSERT_EQUAL(10, u.poll(10, 1));
}

void test_response_while_idle_is_ignored() {
  BatchUploader u;
  TEST_ASSERT_EQUAL(0, u.on_response(1, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_buffered_sends_nothing);
  RUN_TEST(test_sends_up_to_batch_max);
  RUN_TEST(test_ack_pops_and_frees);
  RUN_TEST(test_timeout_resends_with_new_ackid);
  RUN_TEST(test_stale_ackid_is_ignored);
  RUN_TEST(test_error_keeps_records_then_retries);
  RUN_TEST(test_rejected_three_times_is_dropped);
  RUN_TEST(test_disconnect_clears_in_flight);
  RUN_TEST(test_response_while_idle_is_ignored);
  return UNITY_END();
}
