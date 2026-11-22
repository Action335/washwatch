#include <ArduinoJson.h>
#include <unity.h>
#include "batch_json.h"

void setUp() {}
void tearDown() {}

static node_msg_t msg(uint32_t seq, uint8_t phase, uint8_t flags = 0, uint16_t vstd_mg = 2) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = 1;
  m.seq = seq;
  m.phase = phase;
  m.flags = flags;
  m.vstd_mg = vstd_mg;
  return m;
}

static LogRecord rec(uint8_t id, uint32_t t, uint8_t state, uint8_t flags = 0) {
  LogRecord r = {};
  r.t_ms = t;
  r.node_id = id;
  r.state = state;
  r.phase = PHASE_WASH;
  r.flags = flags;
  r.std_mg = 60 + id;
  r.p2p_mg = 300 + id;
  r.vstd_mg = 70 + id;
  r.lo_mg = 40 + id;
  r.hi_mg = 20 + id;
  return r;
}

static char out[8192];

static JsonDocument parse(size_t n) {
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, out, n) == DeserializationError::Ok);
  return doc;
}

constexpr uint16_t MOVING = LOG_MOVING_VSTD_MG;
constexpr uint16_t QUIET = LOG_MOVING_VSTD_MG - 1;

void test_logs_every_window_while_active() {
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_WASH), WasherState::Idle, 0, 0));
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE), WasherState::Running, 0, 0));
}

void test_logs_every_window_while_moving() {
  // The threshold rule may call a gentle wash "idle"; the vector std still sees it.
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE, 0, MOVING), WasherState::Idle, 0, 0));
}

void test_keeps_logging_through_quiet_gaps() {
  // Soaks inside a cycle are quiet for minutes: keep every window for LOG_HOLD_MS after movement.
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE, 0, MOVING), WasherState::Idle, 0, 1000));
  TEST_ASSERT_TRUE(g.should_log(msg(2, PHASE_IDLE, 0, QUIET), WasherState::Idle, 0, 1000 + LOG_HOLD_MS - 1));
  TEST_ASSERT_FALSE(g.should_log(msg(3, PHASE_IDLE, 0, QUIET), WasherState::Idle, 0, 1000 + LOG_HOLD_MS));
}

void test_hold_survives_millis_wrap() {
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE, 0, MOVING), WasherState::Idle, 0, 0xFFFFFF00u));
  TEST_ASSERT_TRUE(g.should_log(msg(2, PHASE_IDLE, 0, QUIET), WasherState::Idle, 0, 0x00000100u));
}

void test_no_hold_before_any_movement() {
  LogGate g;  // fresh gate at a small millis(): must not count as "moved at t=0"
  TEST_ASSERT_FALSE(g.should_log(msg(1, PHASE_IDLE), WasherState::Idle, 0, 5000));
}

void test_imu_fault_is_not_movement() {
  LogGate g;
  g.should_log(msg(1, PHASE_IDLE, FLAG_IMU_FAULT, 9999), WasherState::Idle, FLAG_IMU_FAULT, 1000);
  TEST_ASSERT_FALSE(g.should_log(msg(2, PHASE_IDLE, FLAG_IMU_FAULT, 0), WasherState::Idle, FLAG_IMU_FAULT, 2000));
}

void test_quiet_done_is_logged_at_idle_rate() {
  // A washer can sit in Done for hours (nobody opens the lid): full-rate logging would blow the quota.
  LogGate g;
  TEST_ASSERT_FALSE(g.should_log(msg(1, PHASE_IDLE), WasherState::Done, 0, 0));
  TEST_ASSERT_TRUE(g.should_log(msg(15, PHASE_IDLE), WasherState::Done, 0, 0));
}

void test_idle_logs_one_in_fifteen() {
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(0, PHASE_IDLE), WasherState::Idle, 0, 0));
  TEST_ASSERT_TRUE(g.should_log(msg(15, PHASE_IDLE), WasherState::Idle, 0, 0));
  TEST_ASSERT_FALSE(g.should_log(msg(1, PHASE_IDLE), WasherState::Idle, 0, 0));
  TEST_ASSERT_FALSE(g.should_log(msg(14, PHASE_IDLE), WasherState::Idle, 0, 0));
}

void test_lid_change_is_always_logged() {
  LogGate g;
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE, FLAG_LID_OPEN), WasherState::Idle, 0, 0));
  TEST_ASSERT_TRUE(g.should_log(msg(1, PHASE_IDLE, 0), WasherState::Idle, FLAG_LID_OPEN, 0));
}

void test_make_record_copies_fields() {
  node_msg_t m = msg(9, PHASE_SPIN, FLAG_LID_OPEN);
  m.std_mg = 210;
  m.p2p_mg = 640;
  m.vstd_mg = 300;
  m.lo_mg = 250;
  m.hi_mg = 30;
  LogRecord r = make_record(m, WasherState::Running, 1234);
  TEST_ASSERT_EQUAL_UINT32(1234, r.t_ms);
  TEST_ASSERT_EQUAL(1, r.node_id);
  TEST_ASSERT_EQUAL((uint8_t)WasherState::Running, r.state);
  TEST_ASSERT_EQUAL(PHASE_SPIN, r.phase);
  TEST_ASSERT_EQUAL(FLAG_LID_OPEN, r.flags);
  TEST_ASSERT_EQUAL(210, r.std_mg);
  TEST_ASSERT_EQUAL(640, r.p2p_mg);
  TEST_ASSERT_EQUAL(300, r.vstd_mg);
  TEST_ASSERT_EQUAL(250, r.lo_mg);
  TEST_ASSERT_EQUAL(30, r.hi_mg);
}

void test_batch_shape_and_keys() {
  LogRecord r[] = {rec(1, 1000, 0), rec(2, 4000, 1, FLAG_LID_OPEN)};
  JsonDocument doc = parse(build_batch_update(r, 2, 10000, 7, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT32(7, doc["ackid"].as<uint32_t>());
  TEST_ASSERT_TRUE(doc["merged"].as<bool>());
  TEST_ASSERT_EQUAL(2, doc["batch"].size());
  TEST_ASSERT_TRUE(doc["batch"][0]["ts"].as<int64_t>() == -9000);
  JsonObject d0 = doc["batch"][0]["data"];
  TEST_ASSERT_EQUAL(61, d0["w1_std"].as<int>());
  TEST_ASSERT_EQUAL(301, d0["w1_p2p"].as<int>());
  TEST_ASSERT_EQUAL(71, d0["w1_vstd"].as<int>());
  TEST_ASSERT_EQUAL(41, d0["w1_lo"].as<int>());
  TEST_ASSERT_EQUAL(21, d0["w1_hi"].as<int>());
  TEST_ASSERT_EQUAL(0, d0["w1_lid"].as<int>());
  // The rule's phase is not stored: it can be recomputed from w_std, and every key costs Feed quota.
  TEST_ASSERT_FALSE(d0["w1_phase"].is<JsonVariant>());
  TEST_ASSERT_TRUE(doc["batch"][1]["ts"].as<int64_t>() == -6000);
  TEST_ASSERT_EQUAL(1, doc["batch"][1]["data"]["w2_state"].as<int>());
  TEST_ASSERT_EQUAL(1, doc["batch"][1]["data"]["w2_lid"].as<int>());
}

void test_imu_fault_omits_vibration_keys() {
  LogRecord r[] = {rec(1, 1000, 1, FLAG_IMU_FAULT)};
  JsonDocument doc = parse(build_batch_update(r, 1, 2000, 1, out, sizeof(out)));
  JsonObject d = doc["batch"][0]["data"];
  TEST_ASSERT_FALSE(d["w1_std"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_p2p"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_vstd"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_lo"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_hi"].is<JsonVariant>());
  TEST_ASSERT_EQUAL(1, d["w1_state"].as<int>());
}

void test_relative_ts_across_millis_wrap() {
  LogRecord r[] = {rec(1, 0xFFFFF000u, 0)};
  JsonDocument doc = parse(build_batch_update(r, 1, 0x00000800u, 1, out, sizeof(out)));
  TEST_ASSERT_TRUE(doc["batch"][0]["ts"].as<int64_t>() == -6144);
}

void test_record_newer_than_now_is_ts_zero() {
  // Logged by a higher-priority task after the caller read now: must not become -49.7 days.
  LogRecord r[] = {rec(1, 10005, 0)};
  JsonDocument doc = parse(build_batch_update(r, 1, 10000, 1, out, sizeof(out)));
  TEST_ASSERT_TRUE(doc["batch"][0]["ts"].as<int64_t>() == 0);
}

void test_empty_and_too_small() {
  LogRecord r[LOG_BATCH_MAX];
  for (size_t i = 0; i < LOG_BATCH_MAX; i++) r[i] = rec(1, i, 1);
  TEST_ASSERT_EQUAL(0, build_batch_update(r, 0, 100, 1, out, sizeof(out)));
  char tiny[64];
  TEST_ASSERT_EQUAL(0, build_batch_update(r, LOG_BATCH_MAX, 100, 1, tiny, sizeof(tiny)));
}

void test_full_batch_fits_the_mqtt_buffer() {
  LogRecord r[LOG_BATCH_MAX];
  for (size_t i = 0; i < LOG_BATCH_MAX; i++) r[i] = rec(2, 1000000u + i * 2000, 2, FLAG_LID_OPEN);
  for (size_t i = 0; i < LOG_BATCH_MAX; i++)  // widest values everywhere
    r[i].vstd_mg = r[i].lo_mg = r[i].hi_mg = r[i].std_mg = r[i].p2p_mg = 65535;
  size_t n = build_batch_update(r, LOG_BATCH_MAX, 4000000000u, 4000000000u, out, BATCH_MAX_BYTES);
  TEST_ASSERT_TRUE(n > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_logs_every_window_while_active);
  RUN_TEST(test_logs_every_window_while_moving);
  RUN_TEST(test_keeps_logging_through_quiet_gaps);
  RUN_TEST(test_hold_survives_millis_wrap);
  RUN_TEST(test_no_hold_before_any_movement);
  RUN_TEST(test_imu_fault_is_not_movement);
  RUN_TEST(test_quiet_done_is_logged_at_idle_rate);
  RUN_TEST(test_idle_logs_one_in_fifteen);
  RUN_TEST(test_lid_change_is_always_logged);
  RUN_TEST(test_make_record_copies_fields);
  RUN_TEST(test_batch_shape_and_keys);
  RUN_TEST(test_imu_fault_omits_vibration_keys);
  RUN_TEST(test_relative_ts_across_millis_wrap);
  RUN_TEST(test_record_newer_than_now_is_ts_zero);
  RUN_TEST(test_empty_and_too_small);
  RUN_TEST(test_full_batch_fits_the_mqtt_buffer);
  return UNITY_END();
}
