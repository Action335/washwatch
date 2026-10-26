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
  TEST_ASSERT_EQUAL(0, u.poll(10, 1));  // back off: a transient error must not burn all retries at once
  TEST_ASSERT_EQUAL(10, u.poll(10, BATCH_ACK_TIMEOUT_MS));
}

void test_second_response_for_same_ackid_is_ignored() {
  BatchUploader u;
  u.poll(10, 0);
  uint32_t id = u.ackid();
  TEST_ASSERT_EQUAL(0, u.on_response(id, 429));
  TEST_ASSERT_EQUAL(0, u.on_response(id, 0));  // already answered
}

void test_repeated_timeouts_eventually_drop() {
  // NETPIE never acks (e.g. it can't parse the payload): give up so newer data isn't blocked forever.
  BatchUploader u;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL(10, u.poll(10, t));
  for (int i = 1; i < BATCH_MAX_TIMEOUTS; i++) {
    t += BATCH_ACK_TIMEOUT_MS;
    TEST_ASSERT_EQUAL(10, u.poll(10, t));  // resend
    TEST_ASSERT_EQUAL(0, u.take_dropped());
  }
  t += BATCH_ACK_TIMEOUT_MS;
  TEST_ASSERT_EQUAL(0, u.poll(10, t));  // gave up instead of resending
  TEST_ASSERT_EQUAL(10, u.take_dropped());
  TEST_ASSERT_EQUAL(0, u.take_dropped());  // reported once
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
  TEST_ASSERT_EQUAL(10, u.poll(10, t + 1));  // the next records go out normally
}

void test_ack_resets_timeout_count() {
  BatchUploader u;
  uint32_t t = 0;
  u.poll(10, t);
  for (int i = 1; i < BATCH_MAX_TIMEOUTS; i++) u.poll(10, t += BATCH_ACK_TIMEOUT_MS);
  TEST_ASSERT_EQUAL(10, u.on_response(u.ackid(), 0));
  u.poll(10, t += 1);
  for (int i = 1; i < BATCH_MAX_TIMEOUTS; i++) TEST_ASSERT_EQUAL(10, u.poll(10, t += BATCH_ACK_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(0, u.take_dropped());
}

void test_rejected_three_times_is_dropped() {
  BatchUploader u;
  uint32_t t = 0;
  for (int i = 0; i < BATCH_MAX_REJECTS - 1; i++) {
    TEST_ASSERT_EQUAL(10, u.poll(10, t));
    TEST_ASSERT_EQUAL(0, u.on_response(u.ackid(), 400));
    t += BATCH_ACK_TIMEOUT_MS;  // each retry waits out the backoff
  }
  TEST_ASSERT_EQUAL(10, u.poll(10, t));
  TEST_ASSERT_EQUAL(10, u.on_response(u.ackid(), 400));  // give up: pop them
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
  // The reject count starts again for the next batch.
  TEST_ASSERT_EQUAL(4, u.poll(4, t += 1));
  TEST_ASSERT_EQUAL(0, u.on_response(u.ackid(), 400));
  TEST_ASSERT_EQUAL(4, u.poll(4, t += BATCH_ACK_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(4, u.on_response(u.ackid(), 0));
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
}

void test_disconnects_count_toward_drop() {
  // A batch that makes NETPIE close the connection must not be resent forever.
  BatchUploader u;
  for (int i = 1; i < BATCH_MAX_TIMEOUTS; i++) {
    TEST_ASSERT_EQUAL(10, u.poll(10, i));
    u.on_disconnect();
    TEST_ASSERT_EQUAL(0, u.take_dropped());
  }
  TEST_ASSERT_EQUAL(10, u.poll(10, 100));
  u.on_disconnect();
  TEST_ASSERT_EQUAL(10, u.take_dropped());
  TEST_ASSERT_EQUAL_UINT32(10, u.rejected());
}

void test_disconnect_after_error_reply_is_not_a_timeout() {
  BatchUploader u;
  for (int i = 0; i < BATCH_MAX_TIMEOUTS; i++) {
    u.poll(10, i * BATCH_ACK_TIMEOUT_MS);
    if (i < BATCH_MAX_REJECTS - 1) u.on_response(u.ackid(), 0);  // keep the reject count low
    u.on_disconnect();
  }
  TEST_ASSERT_EQUAL(0, u.take_dropped());
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
  RUN_TEST(test_second_response_for_same_ackid_is_ignored);
  RUN_TEST(test_repeated_timeouts_eventually_drop);
  RUN_TEST(test_ack_resets_timeout_count);
  RUN_TEST(test_rejected_three_times_is_dropped);
  RUN_TEST(test_disconnects_count_toward_drop);
  RUN_TEST(test_disconnect_after_error_reply_is_not_a_timeout);
  RUN_TEST(test_disconnect_clears_in_flight);
  RUN_TEST(test_response_while_idle_is_ignored);
  return UNITY_END();
}
