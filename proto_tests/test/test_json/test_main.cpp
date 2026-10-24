#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>
#include "telemetry_json.h"

void setUp() {}
void tearDown() {}

static node_msg_t indoor(uint32_t seq) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = 1;
  m.seq = seq;
  m.uptime_ms = 1000 + seq;
  m.pm1 = 30;
  m.pm25 = 42;
  m.pm10 = 55;
  m.temp_c10 = 312;
  m.rh_10 = 680;
  m.mq135_raw = 812;
  m.lux = NOT_FITTED;
  return m;
}

static node_msg_t outdoor(uint32_t seq) {
  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = 2;
  m.seq = seq;
  m.uptime_ms = 1000 + seq;
  m.pm1 = m.pm25 = m.pm10 = NOT_FITTED;
  m.n03 = m.n05 = m.n10 = m.n25 = NOT_FITTED;
  m.temp_c10 = 330;
  m.rh_10 = 720;
  m.mq135_raw = NOT_FITTED;
  m.lux = 800;
  return m;
}

static char out[2048];

static JsonDocument build(const NodeTable& t, uint32_t ts, uint32_t now_ms) {
  size_t n = build_shadow_update(t, "airbox-01", ts, now_ms, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, out, n) == DeserializationError::Ok);
  return doc;
}

void test_indoor_node_fields() {
  NodeTable t;
  t.ingest(indoor(7), 0);
  JsonDocument doc = build(t, 0, 0);
  JsonObject n1 = doc["data"]["nodes"]["1"];
  TEST_ASSERT_EQUAL(42, n1["pm25"].as<int>());
  TEST_ASSERT_EQUAL_FLOAT(31.2f, n1["t"].as<float>());
  TEST_ASSERT_EQUAL_FLOAT(68.0f, n1["rh"].as<float>());
  TEST_ASSERT_EQUAL(812, n1["mq"].as<int>());
  TEST_ASSERT_EQUAL(7, n1["seq"].as<int>());
  TEST_ASSERT_FALSE(n1["lux"].is<JsonVariant>());
}

void test_outdoor_node_omits_not_fitted() {
  NodeTable t;
  t.ingest(outdoor(3), 0);
  JsonDocument doc = build(t, 0, 0);
  JsonObject n2 = doc["data"]["nodes"]["2"];
  TEST_ASSERT_FALSE(n2["pm25"].is<JsonVariant>());
  TEST_ASSERT_FALSE(n2["pm1"].is<JsonVariant>());
  TEST_ASSERT_FALSE(n2["mq"].is<JsonVariant>());
  TEST_ASSERT_EQUAL(800, n2["lux"].as<int>());
  TEST_ASSERT_EQUAL_FLOAT(33.0f, n2["t"].as<float>());
}

void test_temp_not_fitted_omitted() {
  NodeTable t;
  node_msg_t m = indoor(1);
  m.temp_c10 = TEMP_NOT_FITTED;
  m.rh_10 = NOT_FITTED;
  t.ingest(m, 0);
  JsonDocument doc = build(t, 0, 0);
  JsonObject n1 = doc["data"]["nodes"]["1"];
  TEST_ASSERT_FALSE(n1["t"].is<JsonVariant>());
  TEST_ASSERT_FALSE(n1["rh"].is<JsonVariant>());
}

void test_online_flag_in_json() {
  NodeTable t;
  t.ingest(indoor(1), 1000);
  JsonDocument doc = build(t, 0, 32000);
  JsonObject n1 = doc["data"]["nodes"]["1"];
  TEST_ASSERT_FALSE(n1["online"].as<bool>());
  TEST_ASSERT_EQUAL(31, n1["age_s"].as<int>());
}

void test_pdr_in_json() {
  NodeTable t;
  t.ingest(indoor(1), 0);
  t.ingest(indoor(2), 0);
  t.ingest(indoor(4), 0);
  t.ingest(indoor(5), 0);
  JsonDocument doc = build(t, 0, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.8f, doc["data"]["nodes"]["1"]["pdr"].as<float>());
}

void test_gw_and_ts() {
  NodeTable t;
  t.ingest(indoor(1), 0);
  JsonDocument doc = build(t, 1793011200u, 0);
  TEST_ASSERT_EQUAL_STRING("airbox-01", doc["data"]["gw"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(1793011200u, doc["data"]["ts"].as<uint32_t>());
}

void test_small_buffer_returns_zero() {
  NodeTable t;
  t.ingest(indoor(1), 0);
  char small[16];
  TEST_ASSERT_EQUAL(0, build_shadow_update(t, "airbox-01", 0, 0, small, sizeof(small)));
}

void test_full_table_fits_2048() {
  NodeTable t;
  for (uint8_t id = 1; id <= MAX_NODES; id++) {
    node_msg_t m = indoor(4000000000u);
    m.node_id = id;
    m.pm1 = m.pm25 = m.pm10 = 999;
    m.temp_c10 = -399;
    m.rh_10 = 999;
    m.mq135_raw = 4095;
    m.lux = 65000;
    t.ingest(m, 0);
  }
  size_t n = build_shadow_update(t, "airbox-01", 4000000000u, 4000000000u, out, sizeof(out));
  TEST_ASSERT_TRUE(n >= 1 && n <= 2048);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_indoor_node_fields);
  RUN_TEST(test_outdoor_node_omits_not_fitted);
  RUN_TEST(test_temp_not_fitted_omitted);
  RUN_TEST(test_online_flag_in_json);
  RUN_TEST(test_pdr_in_json);
  RUN_TEST(test_gw_and_ts);
  RUN_TEST(test_small_buffer_returns_zero);
  RUN_TEST(test_full_table_fits_2048);
  return UNITY_END();
}
