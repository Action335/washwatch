#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include <unity.h>
#include "telemetry_json.h"

void setUp() {}
void tearDown() {}

static node_msg_t washer(uint8_t id, uint32_t seq, uint8_t phase, bool lid_open) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = id;
  m.seq = seq;
  m.uptime_ms = 1000 + seq;
  m.mean_mg = 1003;
  m.std_mg = 180;
  m.p2p_mg = 900;
  m.phase = phase;
  m.flags = lid_open ? FLAG_LID_OPEN : 0;
  return m;
}

static const GatewaySensors ENV = {29.5f, 71.0f, 1200, true};
static char out[2048];

static JsonDocument build(const NodeTable& t, const WasherView* w, size_t n_w, const GatewaySensors& env,
                          uint32_t now_ms) {
  size_t n = build_shadow_update(t, w, n_w, env, "washwatch-01", 1761500000, now_ms, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, out, n) == DeserializationError::Ok);
  return doc;
}

void test_gateway_fields() {
  NodeTable t;
  WasherCycle c1(45);
  WasherView w[] = {{1, &c1}};
  JsonDocument doc = build(t, w, 1, ENV, 0);
  JsonObject d = doc["data"];
  TEST_ASSERT_EQUAL_STRING("washwatch-01", d["gw"]);
  TEST_ASSERT_EQUAL_UINT32(1761500000, d["ts"].as<uint32_t>());
  TEST_ASSERT_EQUAL_FLOAT(29.5f, d["env"]["t"].as<float>());
  TEST_ASSERT_EQUAL_FLOAT(71.0f, d["env"]["rh"].as<float>());
  TEST_ASSERT_EQUAL(1200, d["env"]["rain"].as<int>());
  TEST_ASSERT_TRUE(d["env"]["pir"].as<bool>());
}

void test_failed_dht_is_null() {
  NodeTable t;
  WasherCycle c1(45);
  WasherView w[] = {{1, &c1}};
  GatewaySensors env = {NAN, NAN, 0, false};
  JsonDocument doc = build(t, w, 1, env, 0);
  TEST_ASSERT_TRUE(doc["data"]["env"]["t"].isNull());
  TEST_ASSERT_TRUE(doc["data"]["env"]["rh"].isNull());
}

void test_running_washer_fields() {
  NodeTable t;
  t.ingest(washer(1, 7, PHASE_SPIN, false), 2000);
  WasherCycle c1(45);
  c1.on_message(washer(1, 7, PHASE_SPIN, false), 2000);
  WasherView w[] = {{1, &c1}};
  JsonDocument doc = build(t, w, 1, ENV, 2000 + 5 * 60000);
  JsonObject w1 = doc["data"]["washers"]["1"];
  TEST_ASSERT_EQUAL_STRING("running", w1["state"]);
  TEST_ASSERT_EQUAL(40, w1["min_left"].as<int>());
  TEST_ASSERT_EQUAL_STRING("spin", w1["phase"]);
  TEST_ASSERT_EQUAL(180, w1["std_mg"].as<int>());
  TEST_ASSERT_EQUAL(900, w1["p2p_mg"].as<int>());
  TEST_ASSERT_FALSE(w1["lid_open"].as<bool>());
  TEST_ASSERT_EQUAL(7, w1["seq"].as<int>());
  TEST_ASSERT_EQUAL(300, w1["age_s"].as<int>());
  TEST_ASSERT_FALSE(w1["online"].as<bool>());  // 5 min without a message
  TEST_ASSERT_EQUAL_FLOAT(1.0f, w1["pdr"].as<float>());
}

void test_unheard_washer_shows_state_only() {
  NodeTable t;
  WasherCycle c1(45), c2(30);
  WasherView w[] = {{1, &c1}, {2, &c2}};
  JsonDocument doc = build(t, w, 2, ENV, 0);
  JsonObject w2 = doc["data"]["washers"]["2"];
  TEST_ASSERT_EQUAL_STRING("idle", w2["state"]);
  TEST_ASSERT_FALSE(w2["online"].as<bool>());
  TEST_ASSERT_FALSE(w2["phase"].is<JsonVariant>());
}

void test_imu_fault_hides_vibration() {
  NodeTable t;
  node_msg_t m = washer(1, 1, PHASE_IDLE, false);
  m.flags |= FLAG_IMU_FAULT;
  t.ingest(m, 0);
  WasherCycle c1(45);
  WasherView w[] = {{1, &c1}};
  JsonDocument doc = build(t, w, 1, ENV, 0);
  JsonObject w1 = doc["data"]["washers"]["1"];
  TEST_ASSERT_TRUE(w1["phase"].isNull());
  TEST_ASSERT_TRUE(w1["std_mg"].isNull());
  TEST_ASSERT_TRUE(w1["imu_fault"].as<bool>());
}

void test_too_small_buffer_returns_zero() {
  NodeTable t;
  WasherCycle c1(45);
  WasherView w[] = {{1, &c1}};
  char tiny[16];
  TEST_ASSERT_EQUAL(0, build_shadow_update(t, w, 1, ENV, "washwatch-01", 0, 0, tiny, sizeof(tiny)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_gateway_fields);
  RUN_TEST(test_failed_dht_is_null);
  RUN_TEST(test_running_washer_fields);
  RUN_TEST(test_unheard_washer_shows_state_only);
  RUN_TEST(test_imu_fault_hides_vibration);
  RUN_TEST(test_too_small_buffer_returns_zero);
  return UNITY_END();
}
