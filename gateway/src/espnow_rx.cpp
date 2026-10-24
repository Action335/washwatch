#include "espnow_rx.h"

#include <Arduino.h>
#include <esp_now.h>
#include <string.h>

NodeTable g_nodes;
SemaphoreHandle_t g_nodes_mtx;

struct RxItem {
  uint8_t mac[6];
  node_msg_t msg;
};

constexpr int RX_QUEUE_DEPTH = 16;
constexpr UBaseType_t RX_TASK_PRIO = 5;  // time-critical: highest of the gateway tasks
constexpr uint32_t RX_TASK_STACK = 4096;

static QueueHandle_t q_node_msgs;
static uint32_t counts[3];  // Accepted, Duplicate, Rejected since boot (for the bench check)

// Runs on the Wi-Fi task: copy the frame out and return, nothing else.
static void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(node_msg_t)) return;
  RxItem item;
  memcpy(item.mac, mac, 6);
  memcpy(&item.msg, data, sizeof(node_msg_t));
  xQueueSend(q_node_msgs, &item, 0);
}

static void send_ack(const uint8_t mac[6], const node_msg_t& m) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;  // current channel
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  ack_msg_t ack = {MSG_VERSION, m.node_id, m.seq};
  esp_now_send(mac, (const uint8_t*)&ack, sizeof(ack));
}

static const char* name(Ingest r) {
  switch (r) {
    case Ingest::Accepted: return "Accepted";
    case Ingest::Duplicate: return "Duplicate";
    default: return "Rejected";
  }
}

static void espnow_rx_task(void*) {
  RxItem item;
  for (;;) {
    if (xQueueReceive(q_node_msgs, &item, portMAX_DELAY) != pdTRUE) continue;
    xSemaphoreTake(g_nodes_mtx, portMAX_DELAY);
    Ingest r = g_nodes.ingest(item.msg, millis());
    xSemaphoreGive(g_nodes_mtx);
    // ACK duplicates too: a duplicate means the node missed our first ACK.
    if (r != Ingest::Rejected) send_ack(item.mac, item.msg);
    counts[(int)r]++;
    Serial.printf("rx node=%u seq=%lu %s (acc=%lu dup=%lu rej=%lu)\n", item.msg.node_id,
                  (unsigned long)item.msg.seq, name(r), (unsigned long)counts[0],
                  (unsigned long)counts[1], (unsigned long)counts[2]);
  }
}

void espnow_rx_start() {
  q_node_msgs = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxItem));
  esp_now_register_recv_cb(on_recv);
  xTaskCreate(espnow_rx_task, "espnow_rx", RX_TASK_STACK, nullptr, RX_TASK_PRIO, nullptr);
}
