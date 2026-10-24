#include "link.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "secrets.h"
#include "sender.h"

constexpr uint8_t FALLBACK_CHANNEL = 1;
constexpr uint32_t RESCAN_AFTER_GIVE_UPS = 5;
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static Sender sender;
static QueueHandle_t q_acks;

// Runs on the Wi-Fi task: only copy the ACK out, never touch the Sender here.
static void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(ack_msg_t)) return;
  ack_msg_t ack;
  memcpy(&ack, data, sizeof(ack));
  xQueueSend(q_acks, &ack, 0);
}

static uint8_t scan_channel() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      uint8_t ch = WiFi.channel(i);
      WiFi.scanDelete();
      return ch;
    }
  }
  WiFi.scanDelete();
  return FALLBACK_CHANNEL;
}

static void set_channel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.printf("link: channel %u\n", ch);
}

void link_begin() {
  q_acks = xQueueCreate(8, sizeof(ack_msg_t));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  set_channel(scan_channel());
  if (esp_now_init() != ESP_OK) {
    Serial.println("link: esp_now_init failed, rebooting");
    ESP.restart();
  }
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;  // 0 = current channel
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

bool link_send(const node_msg_t& m) {
  sender.start(m.seq, millis());
  while (sender.busy()) {
    ack_msg_t ack;
    while (xQueueReceive(q_acks, &ack, 0) == pdTRUE) sender.on_ack(ack, m.node_id);
    SendAction a = sender.poll(millis());
    if (a == SendAction::Transmit) esp_now_send(BROADCAST, (const uint8_t*)&m, sizeof(m));
    if (a == SendAction::GiveUp) break;
    delay(1);
  }
  bool acked = sender.consecutive_give_ups() == 0;
  // The router may have moved channel: look for it again.
  if (sender.consecutive_give_ups() > 0 && sender.consecutive_give_ups() % RESCAN_AFTER_GIVE_UPS == 0) {
    Serial.println("link: no ACKs, rescanning channel");
    set_channel(scan_channel());
  }
  return acked;
}

uint32_t link_give_ups() { return sender.consecutive_give_ups(); }
