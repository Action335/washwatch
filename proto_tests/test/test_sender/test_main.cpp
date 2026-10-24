#include <unity.h>
#include "sender.h"

void setUp() {}
void tearDown() {}

static ack_msg_t ack(uint8_t node_id, uint32_t seq) { return ack_msg_t{MSG_VERSION, node_id, seq}; }

// Polls every 1 ms until GiveUp; returns the number of Transmits seen.
static int run_to_give_up(Sender& s, uint32_t t0) {
  int tx = 0;
  for (uint32_t t = t0; t != t0 + 1000; t++) {
    SendAction a = s.poll(t);
    if (a == SendAction::Transmit) tx++;
    if (a == SendAction::GiveUp) return tx;
  }
  return -1;
}

void test_first_poll_transmits_once() {
  Sender s;
  s.start(5, 1000);
  TEST_ASSERT_TRUE(s.poll(1000) == SendAction::Transmit);
  TEST_ASSERT_TRUE(s.poll(1000) == SendAction::None);
}

void test_matching_ack_completes() {
  Sender s;
  s.start(5, 1000);
  s.poll(1000);
  s.on_ack(ack(3, 5), 3);
  TEST_ASSERT_FALSE(s.busy());
  TEST_ASSERT_TRUE(s.poll(5000) == SendAction::None);
  TEST_ASSERT_EQUAL_UINT32(0, s.consecutive_give_ups());
}

void test_wrong_seq_or_node_ignored() {
  Sender s;
  s.start(5, 1000);
  s.poll(1000);
  s.on_ack(ack(3, 4), 3);
  s.on_ack(ack(9, 5), 3);
  TEST_ASSERT_TRUE(s.busy());
}

void test_retry_schedule_then_give_up() {
  Sender s;
  s.start(1, 0);
  uint32_t tx_times[8];
  int tx = 0;
  uint32_t give_up_at = 0;
  for (uint32_t t = 0; t < 1000; t++) {
    SendAction a = s.poll(t);
    if (a == SendAction::Transmit && tx < 8) tx_times[tx++] = t;
    if (a == SendAction::GiveUp) {
      give_up_at = t;
      break;
    }
  }
  TEST_ASSERT_EQUAL(4, tx);
  TEST_ASSERT_EQUAL_UINT32(0, tx_times[0]);
  TEST_ASSERT_EQUAL_UINT32(30, tx_times[1]);
  TEST_ASSERT_EQUAL_UINT32(90, tx_times[2]);
  TEST_ASSERT_EQUAL_UINT32(210, tx_times[3]);
  TEST_ASSERT_EQUAL_UINT32(450, give_up_at);
  TEST_ASSERT_TRUE(s.poll(451) == SendAction::None);
  TEST_ASSERT_FALSE(s.busy());
}

void test_consecutive_give_ups_counts_and_resets() {
  Sender s;
  s.start(1, 0);
  run_to_give_up(s, 0);
  s.start(2, 2000);
  run_to_give_up(s, 2000);
  TEST_ASSERT_EQUAL_UINT32(2, s.consecutive_give_ups());
  s.start(3, 4000);
  s.poll(4000);
  s.on_ack(ack(1, 3), 1);
  TEST_ASSERT_EQUAL_UINT32(0, s.consecutive_give_ups());
}

void test_millis_wraparound() {
  Sender s;
  const uint32_t t0 = 0xFFFFFFF0u;
  s.start(1, t0);
  TEST_ASSERT_TRUE(s.poll(t0) == SendAction::Transmit);
  TEST_ASSERT_TRUE(s.poll(t0 + 29) == SendAction::None);
  TEST_ASSERT_TRUE(s.poll(t0 + 30) == SendAction::Transmit);  // wrapped past 0
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_poll_transmits_once);
  RUN_TEST(test_matching_ack_completes);
  RUN_TEST(test_wrong_seq_or_node_ignored);
  RUN_TEST(test_retry_schedule_then_give_up);
  RUN_TEST(test_consecutive_give_ups_counts_and_resets);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
