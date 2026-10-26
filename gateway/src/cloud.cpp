#include "cloud.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "batch_json.h"
#include "batch_uploader.h"
#include "certs.h"
#include "secrets.h"
#include "state.h"
#include "telemetry_json.h"
#include "wifi_config.h"

constexpr const char* MQTT_HOST = "mqtt.netpie.io";
constexpr uint16_t MQTT_PORT = 1884;  // MQTT over TLS (E5); 1883 is plain
constexpr const char* SHADOW_TOPIC = "@shadow/data/update";
// Batch topic per docs.netpie.io/mqtt-api.html; confirm with tools/netpie_probe.py (README "NETPIE facts").
constexpr const char* BATCH_TOPIC = "@shadow/batch/update";
constexpr const char* BATCH_RESPONSE_TOPIC = "@private/shadow/batch/update/response";
constexpr size_t BATCH_MAX_BYTES = 6144;  // LOG_BATCH_MAX records fit (test_fifty_records_fit_in_6k)
constexpr size_t PAYLOAD_MAX = 1024;  // two washers + env is about 500 bytes at most
constexpr uint32_t RECONNECT_MS = 5000;
constexpr uint32_t WIFI_FIRST_RETRY_MS = 2000;  // most drops are a short beacon loss
constexpr uint32_t WIFI_RETRY_MS = 30000;       // then back off: each attempt costs ESP-NOW reception
constexpr uint32_t LOOP_MS = 100;
constexpr UBaseType_t CLOUD_TASK_PRIO = 2;  // can wait: below receive and control
constexpr uint32_t CLOUD_TASK_STACK = 8192;

static WiFiClientSecure net;
static PubSubClient mqtt(net);
static char payload[PAYLOAD_MAX];
static char batch_buf[BATCH_MAX_BYTES];
static LogRecord batch_recs[LOG_BATCH_MAX];
static BatchUploader uploader;  // used only by cloud_task (the MQTT callback runs inside mqtt.loop())
static uint32_t batch_end_seq = 0;  // g_log sequence just past the in-flight batch (under g_state_mtx)

static uint8_t wifi_channel = 0;
static uint32_t wifi_attempts = 0;  // reconnect attempts since the link was last up

// Auto-reconnect is off (main.cpp), so rejoin the router ourselves, on its last channel.
static void ensure_wifi(uint32_t now, uint32_t& last_wifi_try) {
  if (WiFi.status() == WL_CONNECTED) {
    wifi_channel = WiFi.channel();
    wifi_attempts = 0;
    last_wifi_try = now;
    return;
  }
  uint32_t wait = wifi_attempts == 0 ? WIFI_FIRST_RETRY_MS : WIFI_RETRY_MS;
  if (now - last_wifi_try < wait) return;
  last_wifi_try = now;
  wifi_attempts++;
  Serial.printf("wifi: reconnecting on channel %u\n", wifi_channel);
  WiFi.disconnect();
  WiFi.begin(WIFI_NET_SSID, WIFI_NET_PASS, wifi_channel);
}

// NETPIE's CA is private and unpublished, so the chain isn't checked; instead the server
// certificate must match one of the pinned fingerprints (certs.h), and its name the host.
static bool open_pinned_tls() {
  net.stop();
  net.setInsecure();
  if (!net.connect(MQTT_HOST, MQTT_PORT)) {
    Serial.println("mqtt: TLS connect failed");
    return false;
  }
  for (const char* fp : NETPIE_FINGERPRINTS)
    if (net.verify(fp, MQTT_HOST)) return true;
  net.stop();
  Serial.println("mqtt: server certificate matches no pin; credentials not sent (see certs.h)");
  return false;
}

static bool ensure_connected(uint32_t now, uint32_t& last_try) {
#ifdef SIM_OUTAGE
  // gateway_sim test: the cloud is unreachable from 20 s to 50 s of uptime.
  if (now >= 20000 && now < 50000) {
    if (mqtt.connected()) {
      mqtt.disconnect();
      Serial.println("mqtt: simulated outage starts");
    }
    return false;
  }
#endif
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED || now - last_try < RECONNECT_MS) return false;
  last_try = now;
  // Open TLS and check the pin first: mqtt.connect() reuses this open connection, so the
  // credentials are only ever sent to a verified NETPIE broker.
  if (!open_pinned_tls()) return false;
  bool ok = mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET);
  if (ok && !mqtt.subscribe(BATCH_RESPONSE_TOPIC)) {
    // Without the ack subscription every batch would time out and be dropped: retry the whole connect.
    mqtt.disconnect();
    ok = false;
  }
  Serial.printf("mqtt: connect %s (state %d)\n", ok ? "ok" : "failed", mqtt.state());
  return ok;
}

// NETPIE's reply to a batch: {"ackid":N, ...} with "errcode" only on failure.
static void on_mqtt_message(char* topic, uint8_t* data, unsigned int len) {
  if (strcmp(topic, BATCH_RESPONSE_TOPIC) != 0) return;
  JsonDocument doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;
  uint32_t ackid = doc["ackid"] | 0u;
  int errcode = doc["errcode"] | 0;
  size_t done = uploader.on_response(ackid, errcode);
  if (done > 0) {
    xSemaphoreTake(g_state_mtx, portMAX_DELAY);
    g_log.pop_through(batch_end_seq);  // not pop(done): the buffer may have overwritten some meanwhile
    xSemaphoreGive(g_state_mtx);
  }
  Serial.printf("batch: ack %lu errcode %d -> %u records done\n", (unsigned long)ackid, errcode, (unsigned)done);
}

// Sends the oldest buffered records when the uploader says so (E4 store-and-forward).
static void upload_log() {
  xSemaphoreTake(g_state_mtx, portMAX_DELAY);
  // Read the time under the lock, so no record in the batch is newer than now.
  uint32_t now = millis();
  size_t n = uploader.poll(g_log.size(), now);
  if (uploader.take_dropped() > 0) {
    g_log.pop_through(batch_end_seq);  // NETPIE never answered that batch: give up on it
    Serial.println("batch: no ack after repeated sends, batch dropped (see log.rejected)");
  }
  if (n > 0) {
    n = g_log.peek(batch_recs, n);
    batch_end_seq = g_log.oldest_seq() + n;
  }
  xSemaphoreGive(g_state_mtx);
  if (n == 0) return;
  size_t len = build_batch_update(batch_recs, n, now, uploader.ackid(), batch_buf, sizeof(batch_buf));
  bool ok = len > 0 && mqtt.publish(BATCH_TOPIC, (const uint8_t*)batch_buf, len);
  Serial.printf("batch: send %u records (ackid %lu, %u bytes) %s\n", (unsigned)n, (unsigned long)uploader.ackid(),
                (unsigned)len, ok ? "ok" : "failed");
}

static void publish_shadow() {
  WasherView views[WASHER_COUNT];
  xSemaphoreTake(g_state_mtx, portMAX_DELAY);
  for (size_t i = 0; i < WASHER_COUNT; i++) views[i] = {g_washers[i].node_id, &g_washers[i].cycle};
  // Read the time under the lock, so no node can be stored "after" it.
  uint32_t now = millis();
  LogStats log = {(uint32_t)g_log.size(), g_log.dropped(), uploader.rejected()};
  size_t n = build_shadow_update(g_nodes, views, WASHER_COUNT, g_env, log, GW_ID, (uint32_t)time(nullptr), now,
                                 payload, sizeof(payload));
  xSemaphoreGive(g_state_mtx);
  if (n == 0) {
    Serial.println("mqtt: payload too large, skipped");
    return;
  }
  bool ok = mqtt.publish(SHADOW_TOPIC, (const uint8_t*)payload, n);
  Serial.printf("mqtt: publish %u bytes %s\n", (unsigned)n, ok ? "ok" : "failed");
}

static void cloud_task(void*) {
  uint32_t last_try = 0, last_pub = 0, last_wifi_try = millis();
  bool first_try = true, was_connected = false;
  for (;;) {
    uint32_t now = millis();
    if (first_try) {
      last_try = now - RECONNECT_MS;  // try the first connect straight away
      first_try = false;
    }
    ensure_wifi(now, last_wifi_try);
    bool connected = ensure_connected(now, last_try);
    if (was_connected && !connected) uploader.on_disconnect();  // its ack can't arrive now: resend later
    was_connected = connected;
    if (connected) {
      mqtt.loop();
      upload_log();  // while offline, records wait in g_log (E4)
      if (now - last_pub >= PUBLISH_PERIOD_MS) {
        last_pub = now;
        publish_shadow();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
  }
}

void cloud_start() {
  configTime(7 * 3600, 0, "pool.ntp.org");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(on_mqtt_message);
  mqtt.setBufferSize(BATCH_MAX_BYTES + 64);  // the batch is the largest message; + MQTT header and topic
  xTaskCreate(cloud_task, "cloud", CLOUD_TASK_STACK, nullptr, CLOUD_TASK_PRIO, nullptr);
}
