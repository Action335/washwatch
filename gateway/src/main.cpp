// WashWatch gateway: receives washer node data over ESP-NOW, runs the cycle logic, drives the
// dial and flag servos, publishes to NETPIE and sends Discord alerts.
// Receive and control run locally, so they keep working when the internet is down.
//
// Tasks: espnow_rx (5) > control (4) > cloud (2) > notify (1), sharing state under g_state_mtx.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "cloud.h"
#include "control.h"
#include "espnow_rx.h"
#include "notify.h"
#include "sim_nodes.h"
#include "state.h"
#include "wifi_config.h"

constexpr uint32_t WIFI_WAIT_MS = 15000;

void setup() {
  Serial.begin(115200);
  Serial.println("WashWatch gateway starting");
  state_init();
  control_start();  // servos to their rest position before anything else

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // with modem sleep on, ESP-NOW misses packets
  // The core's auto-reconnect scans every channel nonstop while the router is off, so
  // ESP-NOW goes deaf. cloud_task reconnects on the last known channel instead.
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_NET_SSID, WIFI_NET_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_WAIT_MS) delay(250);
  // The nodes scan for this channel, so print it for the bench check.
  Serial.printf("wifi: %s, channel %d\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected",
                WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed, rebooting");
    ESP.restart();
  }
  espnow_rx_start();
  cloud_start();
  notify_start();
#ifdef SIM_NODES
  sim_nodes_start();
#endif
}

// All work happens in the tasks.
void loop() { vTaskDelay(portMAX_DELAY); }
