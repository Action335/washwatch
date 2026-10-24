// AirBox purifier gateway: receives node data over ESP-NOW, publishes it to NETPIE.
// Control and receive run locally, so they keep working when the internet is down.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include "cloud.h"
#include "espnow_rx.h"
#include "secrets.h"

constexpr uint32_t WIFI_WAIT_MS = 15000;

void setup() {
  Serial.begin(115200);
  Serial.println("AirBox gateway starting");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // with modem sleep on, ESP-NOW misses packets
  // The core's auto-reconnect scans every channel nonstop while the router is off, so
  // ESP-NOW goes deaf. cloud_task reconnects on the last known channel instead.
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_WAIT_MS) delay(250);
  // The nodes scan for this channel, so print it for the bench check.
  Serial.printf("wifi: %s, channel %d\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected",
                WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed, rebooting");
    ESP.restart();
  }
  g_nodes_mtx = xSemaphoreCreateMutex();
  espnow_rx_start();
  cloud_start();
}

// All work happens in espnow_rx_task and cloud_task.
void loop() { vTaskDelay(portMAX_DELAY); }
