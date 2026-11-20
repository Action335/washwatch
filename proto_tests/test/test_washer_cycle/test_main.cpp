#include <unity.h>
#include "washer_cycle.h"

void setUp() {}
void tearDown() {}

constexpr uint32_t MIN = 60000;

static node_msg_t msg(uint8_t phase, bool lid_open = false, bool imu_fault = false) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = 1;
  m.phase = phase;
  m.flags = (lid_open ? FLAG_LID_OPEN : 0) | (imu_fault ? FLAG_IMU_FAULT : 0);
  return m;
}

// Feeds one message every 2 s from t0 up to (not including) t1; returns the last event seen.
static CycleEvent feed(WasherCycle& c, uint8_t phase, uint32_t t0, uint32_t t1, bool lid_open = false) {
  CycleEvent last = CycleEvent::None;
  for (uint32_t t = t0; t < t1; t += 2000) {
    CycleEvent e = c.on_message(msg(phase, lid_open), t);
    if (e != CycleEvent::None) last = e;
  }
  return last;
}

void test_starts_idle() {
  WasherCycle c(45);
  TEST_ASSERT_TRUE(c.state() == WasherState::Idle);
  TEST_ASSERT_EQUAL(0, c.minutes_left(0));
}

// Sustained motion: START_WINDOWS moving messages in a row, 2 s apart, from t0.
static CycleEvent start(WasherCycle& c, uint32_t t0) {
  return feed(c, PHASE_WASH, t0, t0 + START_WINDOWS * 2000);
}

void test_motion_starts_a_cycle() {
  WasherCycle c(45);
  TEST_ASSERT_TRUE(start(c, 1000) == CycleEvent::Started);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
  // The cycle started at the first moving window, not when the debounce completed.
  TEST_ASSERT_EQUAL(35, c.minutes_left(1000 + 10 * MIN));
}

void test_isolated_bump_does_not_start_a_cycle() {
  // e.g. the washer next door spinning, or someone leaning on it
  WasherCycle c(45);
  TEST_ASSERT_TRUE(feed(c, PHASE_WASH, 0, (START_WINDOWS - 1) * 2000) == CycleEvent::None);
  c.on_message(msg(PHASE_IDLE), (START_WINDOWS - 1) * 2000);
  TEST_ASSERT_TRUE(feed(c, PHASE_SPIN, START_WINDOWS * 2000, (2 * START_WINDOWS - 1) * 2000) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Idle);
}

void test_minutes_left_never_reaches_zero_while_running() {
  WasherCycle c(45);
  start(c, 0);
  TEST_ASSERT_EQUAL(1, c.minutes_left(60 * MIN));
}

void test_short_pause_is_not_done() {
  WasherCycle c(45);
  feed(c, PHASE_WASH, 0, 10 * MIN);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, 10 * MIN, 10 * MIN + DONE_QUIET_MS - 2000) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
  const uint32_t resume = 10 * MIN + DONE_QUIET_MS - 2000;
  feed(c, PHASE_SPIN, resume, resume + MIN);  // the cycle carries on after a soak
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
}

void test_washer2_mid_cycle_gap_is_not_done() {
  // Washer 2 on 7 Nov 2026: rinse ended 14:39, quiet until the spin at 14:47-14:48 (~8 min).
  WasherCycle c(83);
  feed(c, PHASE_WASH, 0, 22 * MIN);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, 22 * MIN, 30 * MIN) == CycleEvent::None);
  feed(c, PHASE_SPIN, 30 * MIN, 36 * MIN);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
}

void test_long_quiet_after_running_is_done() {
  WasherCycle c(45);
  feed(c, PHASE_SPIN, 0, 40 * MIN);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, 40 * MIN, 40 * MIN + DONE_QUIET_MS + 2000) == CycleEvent::Done);
  TEST_ASSERT_TRUE(c.state() == WasherState::Done);
  TEST_ASSERT_EQUAL(0, c.minutes_left(45 * MIN));
}

void test_offline_gap_does_not_fake_done() {
  WasherCycle c(45);
  feed(c, PHASE_WASH, 0, 5 * MIN);
  // Node silent for 10 min, then one quiet message: no evidence of any quiet time yet.
  TEST_ASSERT_TRUE(c.on_message(msg(PHASE_IDLE), 15 * MIN) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
}

void test_opening_the_lid_collects_finished_laundry() {
  WasherCycle c(45);
  const uint32_t after_done = 30 * MIN + DONE_QUIET_MS + MIN;
  feed(c, PHASE_WASH, 0, 30 * MIN);
  feed(c, PHASE_IDLE, 30 * MIN, after_done);
  TEST_ASSERT_TRUE(c.state() == WasherState::Done);
  TEST_ASSERT_TRUE(c.on_message(msg(PHASE_IDLE, true), after_done + MIN) == CycleEvent::Collected);
  TEST_ASSERT_TRUE(c.state() == WasherState::Idle);
}

void test_lid_open_while_running_is_ignored() {
  WasherCycle c(45);
  start(c, 0);
  TEST_ASSERT_TRUE(c.on_message(msg(PHASE_WASH, true), START_WINDOWS * 2000) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
}

void test_one_reminder_when_left_uncollected() {
  WasherCycle c(45);
  const uint32_t done_at = 30 * MIN + DONE_QUIET_MS;  // quiet from 30 min
  feed(c, PHASE_WASH, 0, 30 * MIN);
  feed(c, PHASE_IDLE, 30 * MIN, done_at + MIN);
  TEST_ASSERT_TRUE(c.state() == WasherState::Done);
  const uint32_t remind_at = done_at + REMIND_AFTER_MS;
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, done_at + MIN, remind_at - MIN) == CycleEvent::None);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, remind_at - MIN, remind_at + 3 * MIN) == CycleEvent::Reminder);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, remind_at + 3 * MIN, remind_at + 40 * MIN) == CycleEvent::None);  // only once
}

void test_motion_after_done_resumes_running() {
  // A long soak looked like the end; the final spin proves it wasn't.
  WasherCycle c(45);
  const uint32_t t = 20 * MIN + DONE_QUIET_MS + MIN;
  feed(c, PHASE_WASH, 0, 20 * MIN);
  feed(c, PHASE_IDLE, 20 * MIN, t);
  TEST_ASSERT_TRUE(c.state() == WasherState::Done);
  const uint32_t t_end = t + START_WINDOWS * 2000;
  TEST_ASSERT_TRUE(feed(c, PHASE_SPIN, t, t_end) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Running);
  TEST_ASSERT_EQUAL(45 - t_end / MIN, c.minutes_left(t_end));  // keeps the original start time (0)
}

void test_bump_while_done_stays_done() {
  // A neighbour's spin must not reopen a finished cycle (and later send a second "done").
  WasherCycle c(45);
  const uint32_t t = 20 * MIN + DONE_QUIET_MS + MIN;
  feed(c, PHASE_WASH, 0, 20 * MIN);
  feed(c, PHASE_IDLE, 20 * MIN, t);
  TEST_ASSERT_TRUE(feed(c, PHASE_SPIN, t, t + 6000) == CycleEvent::None);
  TEST_ASSERT_TRUE(feed(c, PHASE_IDLE, t + 6000, t + DONE_QUIET_MS + 2 * MIN) != CycleEvent::Done);
  TEST_ASSERT_TRUE(c.state() == WasherState::Done);
}

void test_imu_fault_neither_starts_nor_finishes() {
  WasherCycle c(45);
  TEST_ASSERT_TRUE(c.on_message(msg(PHASE_SPIN, false, true), 0) == CycleEvent::None);
  TEST_ASSERT_TRUE(c.state() == WasherState::Idle);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle);
  RUN_TEST(test_motion_starts_a_cycle);
  RUN_TEST(test_isolated_bump_does_not_start_a_cycle);
  RUN_TEST(test_minutes_left_never_reaches_zero_while_running);
  RUN_TEST(test_short_pause_is_not_done);
  RUN_TEST(test_washer2_mid_cycle_gap_is_not_done);
  RUN_TEST(test_long_quiet_after_running_is_done);
  RUN_TEST(test_offline_gap_does_not_fake_done);
  RUN_TEST(test_opening_the_lid_collects_finished_laundry);
  RUN_TEST(test_lid_open_while_running_is_ignored);
  RUN_TEST(test_one_reminder_when_left_uncollected);
  RUN_TEST(test_motion_after_done_resumes_running);
  RUN_TEST(test_bump_while_done_stays_done);
  RUN_TEST(test_imu_fault_neither_starts_nor_finishes);
  return UNITY_END();
}
