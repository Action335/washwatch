#include "link.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "sender.h"

constexpr uint32_t SWEEP_AFTER_MISSES = 5;
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static Sender sender;
static QueueHandle_t q_acks;
static Preferences prefs;
static uint8_t channel = 1;
static bool found = false;      // has the gateway ACKed on `channel` since boot?
static uint32_t misses = 0;     // link_send calls in a row that got no ACK

// Runs on the Wi-Fi task: only copy the ACK out, never touch the Sender here.
static void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(ack_msg_t)) return;
  ack_msg_t ack;
  memcpy(&ack, data, sizeof(ack));
  xQueueSend(q_acks, &ack, 0);
}

static void set_channel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  channel = ch;
}

// One send with ACK/retry on the current channel. max_tx = 1 is a quick probe.
static bool send_once(const node_msg_t& m, uint8_t max_tx) {
  sender.start(m.seq, millis(), max_tx);
  for (;;) {
    ack_msg_t ack;
    while (xQueueReceive(q_acks, &ack, 0) == pdTRUE) sender.on_ack(ack, m.node_id);
    if (!sender.busy()) return true;  // on_ack completed it
    SendAction a = sender.poll(millis());
    if (a == SendAction::Transmit) esp_now_send(BROADCAST, (const uint8_t*)&m, sizeof(m));
    if (a == SendAction::GiveUp) return false;
    delay(1);
  }
}

// Tries the other 12 channels with one probe each (~0.4 s). The gateway answers on
// whatever channel it is on, so this works on any router or phone hotspot.
static bool sweep(const node_msg_t& m) {
  uint8_t start = channel;
  for (uint8_t ch = next_channel(start); ch != start; ch = next_channel(ch)) {
    set_channel(ch);
    if (send_once(m, 1)) return true;
  }
  set_channel(start);
  return false;
}

void link_begin() {
  q_acks = xQueueCreate(8, sizeof(ack_msg_t));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  prefs.begin("washwatch", false);
  set_channel(prefs.getUChar("ch", 1));  // last channel that worked, so a reboot is fast
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
  bool acked = send_once(m, 4);
  if (!acked) {
    misses++;
    // Not found since boot, or the gateway moved (new router, hotspot): look for it.
    if (!found || misses % SWEEP_AFTER_MISSES == 0) {
      Serial.println("link: no ACKs, sweeping channels");
      acked = sweep(m);
    }
  }
  if (acked) {
    misses = 0;
    if (!found || prefs.getUChar("ch", 0) != channel) prefs.putUChar("ch", channel);
    found = true;
  }
  return acked;
}

uint8_t link_channel() { return channel; }
uint32_t link_misses() { return misses; }
