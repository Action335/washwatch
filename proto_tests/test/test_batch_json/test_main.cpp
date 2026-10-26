#include <ArduinoJson.h>
#include <unity.h>
#include "batch_json.h"

void setUp() {}
void tearDown() {}

static node_msg_t msg(uint32_t seq, uint8_t phase, uint8_t flags = 0) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = 1;
  m.seq = seq;
  m.phase = phase;
  m.flags = flags;
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
  return r;
}

static char out[6144];

static JsonDocument parse(size_t n) {
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, out, n) == DeserializationError::Ok);
  return doc;
}

void test_logs_every_window_while_active() {
  TEST_ASSERT_TRUE(should_log(msg(1, PHASE_WASH), WasherState::Idle, 0));
  TEST_ASSERT_TRUE(should_log(msg(1, PHASE_IDLE), WasherState::Running, 0));
  TEST_ASSERT_TRUE(should_log(msg(1, PHASE_IDLE), WasherState::Done, 0));
}

void test_idle_logs_one_in_fifteen() {
  TEST_ASSERT_TRUE(should_log(msg(0, PHASE_IDLE), WasherState::Idle, 0));
  TEST_ASSERT_TRUE(should_log(msg(15, PHASE_IDLE), WasherState::Idle, 0));
  TEST_ASSERT_FALSE(should_log(msg(1, PHASE_IDLE), WasherState::Idle, 0));
  TEST_ASSERT_FALSE(should_log(msg(14, PHASE_IDLE), WasherState::Idle, 0));
}

void test_lid_change_is_always_logged() {
  TEST_ASSERT_TRUE(should_log(msg(1, PHASE_IDLE, FLAG_LID_OPEN), WasherState::Idle, 0));
  TEST_ASSERT_TRUE(should_log(msg(1, PHASE_IDLE, 0), WasherState::Idle, FLAG_LID_OPEN));
}

void test_make_record_copies_fields() {
  node_msg_t m = msg(9, PHASE_SPIN, FLAG_LID_OPEN);
  m.std_mg = 210;
  m.p2p_mg = 640;
  LogRecord r = make_record(m, WasherState::Running, 1234);
  TEST_ASSERT_EQUAL_UINT32(1234, r.t_ms);
  TEST_ASSERT_EQUAL(1, r.node_id);
  TEST_ASSERT_EQUAL((uint8_t)WasherState::Running, r.state);
  TEST_ASSERT_EQUAL(PHASE_SPIN, r.phase);
  TEST_ASSERT_EQUAL(FLAG_LID_OPEN, r.flags);
  TEST_ASSERT_EQUAL(210, r.std_mg);
  TEST_ASSERT_EQUAL(640, r.p2p_mg);
}

void test_batch_shape_and_keys() {
  LogRecord r[] = {rec(1, 1000, 0), rec(2, 4000, 1, FLAG_LID_OPEN)};
  JsonDocument doc = parse(build_batch_update(r, 2, 10000, 7, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT32(7, doc["ackid"].as<uint32_t>());
  TEST_ASSERT_TRUE(doc["merged"].as<bool>());
  TEST_ASSERT_EQUAL(2, doc["batch"].size());
  TEST_ASSERT_EQUAL(-9000, doc["batch"][0]["ts"].as<long>());
  TEST_ASSERT_EQUAL(61, doc["batch"][0]["data"]["w1_std"].as<int>());
  TEST_ASSERT_EQUAL(301, doc["batch"][0]["data"]["w1_p2p"].as<int>());
  TEST_ASSERT_EQUAL(PHASE_WASH, doc["batch"][0]["data"]["w1_phase"].as<int>());
  TEST_ASSERT_EQUAL(0, doc["batch"][0]["data"]["w1_lid"].as<int>());
  TEST_ASSERT_EQUAL(-6000, doc["batch"][1]["ts"].as<long>());
  TEST_ASSERT_EQUAL(1, doc["batch"][1]["data"]["w2_state"].as<int>());
  TEST_ASSERT_EQUAL(1, doc["batch"][1]["data"]["w2_lid"].as<int>());
}

void test_imu_fault_omits_vibration_keys() {
  LogRecord r[] = {rec(1, 1000, 1, FLAG_IMU_FAULT)};
  JsonDocument doc = parse(build_batch_update(r, 1, 2000, 1, out, sizeof(out)));
  JsonObject d = doc["batch"][0]["data"];
  TEST_ASSERT_FALSE(d["w1_std"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_p2p"].is<JsonVariant>());
  TEST_ASSERT_FALSE(d["w1_phase"].is<JsonVariant>());
  TEST_ASSERT_EQUAL(1, d["w1_state"].as<int>());
}

void test_relative_ts_across_millis_wrap() {
  LogRecord r[] = {rec(1, 0xFFFFF000u, 0)};
  JsonDocument doc = parse(build_batch_update(r, 1, 0x00000800u, 1, out, sizeof(out)));
  TEST_ASSERT_EQUAL(-6144, doc["batch"][0]["ts"].as<long>());
}

void test_empty_and_too_small() {
  LogRecord r[LOG_BATCH_MAX];
  for (size_t i = 0; i < LOG_BATCH_MAX; i++) r[i] = rec(1, i, 1);
  TEST_ASSERT_EQUAL(0, build_batch_update(r, 0, 100, 1, out, sizeof(out)));
  char tiny[64];
  TEST_ASSERT_EQUAL(0, build_batch_update(r, LOG_BATCH_MAX, 100, 1, tiny, sizeof(tiny)));
}

void test_fifty_records_fit_in_6k() {
  LogRecord r[LOG_BATCH_MAX];
  for (size_t i = 0; i < LOG_BATCH_MAX; i++) r[i] = rec(2, 1000000u + i * 2000, 2, FLAG_LID_OPEN);
  r[0].std_mg = r[0].p2p_mg = 65535;  // widest values
  TEST_ASSERT_TRUE(build_batch_update(r, LOG_BATCH_MAX, 4000000000u, 4000000000u, out, sizeof(out)) > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_logs_every_window_while_active);
  RUN_TEST(test_idle_logs_one_in_fifteen);
  RUN_TEST(test_lid_change_is_always_logged);
  RUN_TEST(test_make_record_copies_fields);
  RUN_TEST(test_batch_shape_and_keys);
  RUN_TEST(test_imu_fault_omits_vibration_keys);
  RUN_TEST(test_relative_ts_across_millis_wrap);
  RUN_TEST(test_empty_and_too_small);
  RUN_TEST(test_fifty_records_fit_in_6k);
  return UNITY_END();
}
