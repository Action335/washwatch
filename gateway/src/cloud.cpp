#include "cloud.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "espnow_rx.h"
#include "secrets.h"
#include "telemetry_json.h"

constexpr const char* GW_ID = "airbox-01";
constexpr const char* MQTT_HOST = "mqtt.netpie.io";
constexpr uint16_t MQTT_PORT = 1883;  // TLS (8883) comes with E5 in a later plan
constexpr const char* SHADOW_TOPIC = "@shadow/data/update";
constexpr size_t PAYLOAD_MAX = 2048;  // worst case for MAX_NODES is ~1.2 KB
constexpr uint32_t RECONNECT_MS = 5000;
constexpr uint32_t LOOP_MS = 100;
constexpr UBaseType_t CLOUD_TASK_PRIO = 2;  // can wait: below receive and control
constexpr uint32_t CLOUD_TASK_STACK = 8192;

static WiFiClient net;
static PubSubClient mqtt(net);
static char payload[PAYLOAD_MAX];

static bool ensure_connected(uint32_t now, uint32_t& last_try) {
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED || now - last_try < RECONNECT_MS) return false;
  last_try = now;
  bool ok = mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET);
  Serial.printf("mqtt: connect %s (state %d)\n", ok ? "ok" : "failed", mqtt.state());
  return ok;
}

static void publish_shadow(uint32_t now) {
  xSemaphoreTake(g_nodes_mtx, portMAX_DELAY);
  size_t n = build_shadow_update(g_nodes, GW_ID, (uint32_t)time(nullptr), now, payload, sizeof(payload));
  xSemaphoreGive(g_nodes_mtx);
  if (n == 0) {
    Serial.println("mqtt: payload too large, skipped");
    return;
  }
  bool ok = mqtt.publish(SHADOW_TOPIC, (const uint8_t*)payload, n);
  Serial.printf("mqtt: publish %u bytes %s\n", (unsigned)n, ok ? "ok" : "failed");
}

static void cloud_task(void*) {
  uint32_t last_try = 0, last_pub = 0;
  bool first_try = true;
  for (;;) {
    uint32_t now = millis();
    if (first_try) {
      last_try = now - RECONNECT_MS;  // try the first connect straight away
      first_try = false;
    }
    if (ensure_connected(now, last_try)) {
      mqtt.loop();
      // Not connected = skip; buffering offline data (E4) comes in a later plan.
      if (now - last_pub >= PUBLISH_PERIOD_MS) {
        last_pub = now;
        publish_shadow(now);
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
