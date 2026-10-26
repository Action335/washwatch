#include "cloud.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"
#include "state.h"
#include "telemetry_json.h"

constexpr const char* MQTT_HOST = "mqtt.netpie.io";
constexpr uint16_t MQTT_PORT = 1883;  // TLS (8883) comes with E5 in a later plan
constexpr const char* SHADOW_TOPIC = "@shadow/data/update";
constexpr size_t PAYLOAD_MAX = 1024;  // two washers + env is about 500 bytes at most
constexpr uint32_t RECONNECT_MS = 5000;
constexpr uint32_t WIFI_FIRST_RETRY_MS = 2000;  // most drops are a short beacon loss
constexpr uint32_t WIFI_RETRY_MS = 30000;       // then back off: each attempt costs ESP-NOW reception
constexpr uint32_t LOOP_MS = 100;
constexpr UBaseType_t CLOUD_TASK_PRIO = 2;  // can wait: below receive and control
constexpr uint32_t CLOUD_TASK_STACK = 8192;

static WiFiClient net;
static PubSubClient mqtt(net);
static char payload[PAYLOAD_MAX];

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
  WiFi.begin(WIFI_SSID, WIFI_PASS, wifi_channel);
}

static bool ensure_connected(uint32_t now, uint32_t& last_try) {
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED || now - last_try < RECONNECT_MS) return false;
  last_try = now;
  bool ok = mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET);
  Serial.printf("mqtt: connect %s (state %d)\n", ok ? "ok" : "failed", mqtt.state());
  return ok;
}

static void publish_shadow() {
  WasherView views[WASHER_COUNT];
  xSemaphoreTake(g_state_mtx, portMAX_DELAY);
  for (size_t i = 0; i < WASHER_COUNT; i++) views[i] = {g_washers[i].node_id, &g_washers[i].cycle};
  // Read the time under the lock, so no node can be stored "after" it.
  uint32_t now = millis();
  size_t n = build_shadow_update(g_nodes, views, WASHER_COUNT, g_env, GW_ID, (uint32_t)time(nullptr), now,
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
  bool first_try = true;
  for (;;) {
    uint32_t now = millis();
    if (first_try) {
      last_try = now - RECONNECT_MS;  // try the first connect straight away
      first_try = false;
    }
    ensure_wifi(now, last_wifi_try);
    if (ensure_connected(now, last_try)) {
      mqtt.loop();
      // Not connected = skip; buffering offline data (E4) comes in a later plan.
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
  mqtt.setBufferSize(PAYLOAD_MAX + 64);  // + MQTT header and topic
  xTaskCreate(cloud_task, "cloud", CLOUD_TASK_STACK, nullptr, CLOUD_TASK_PRIO, nullptr);
}
