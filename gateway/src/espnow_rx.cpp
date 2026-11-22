#include "espnow_rx.h"

#include <Arduino.h>
#include <esp_now.h>
#include <string.h>

#include "batch_json.h"
#include "state.h"

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

void handle_node_msg(const uint8_t* mac, const node_msg_t& in) {
  CycleEvent ev = CycleEvent::None;
  node_msg_t m = in;
  bool raw_open = (in.flags & FLAG_LID_OPEN) != 0;
  xSemaphoreTake(g_state_mtx, portMAX_DELAY);
  uint32_t now = millis();
  // Every later use (node table, shadow, cycle, log) sees the filtered lid, not the raw reed.
  if (Washer* lw = washer_for(m.node_id)) {
    bool open = lw->lid.update(raw_open, now);
    m.flags = open ? (m.flags | FLAG_LID_OPEN) : (m.flags & ~FLAG_LID_OPEN);
  }
  const NodeEntry* prev = g_nodes.find(m.node_id);
  uint8_t prev_flags = prev != nullptr ? prev->last.flags : 0;
  Ingest r = g_nodes.ingest(m, now);
  // Only new messages drive the cycle and the log; a duplicate is a retransmit of one already used.
  Washer* w = r == Ingest::Accepted ? washer_for(m.node_id) : nullptr;
  if (w != nullptr) {
    ev = w->cycle.on_message(m, now);
    if (w->log_gate.should_log(m, w->cycle.state(), prev_flags, now)) g_log.push(make_record(m, w->cycle.state(), now));
  }
  xSemaphoreGive(g_state_mtx);
  // ACK duplicates too: a duplicate means the node missed our first ACK.
  if (mac != nullptr && r != Ingest::Rejected) send_ack(mac, m);
  if (ev != CycleEvent::None) {
    Notice n = {m.node_id, ev, false};
    xQueueSend(q_notices, &n, 0);
  }
  counts[(int)r]++;
  Serial.printf("rx node=%u seq=%lu %s phase=%u std=%u vstd=%u lo=%u hi=%u lid=%u (raw %u) ev=%u (acc=%lu dup=%lu rej=%lu)\n",
                m.node_id, (unsigned long)m.seq, name(r), m.phase, m.std_mg, m.vstd_mg, m.lo_mg, m.hi_mg,
                (m.flags & FLAG_LID_OPEN) ? 1 : 0, raw_open ? 1 : 0, (unsigned)ev,
                (unsigned long)counts[0], (unsigned long)counts[1], (unsigned long)counts[2]);
}

static void espnow_rx_task(void*) {
  RxItem item;
  for (;;) {
    if (xQueueReceive(q_node_msgs, &item, portMAX_DELAY) != pdTRUE) continue;
    handle_node_msg(item.mac, item.msg);
  }
}

void espnow_rx_start() {
  q_node_msgs = xQueueCreate(RX_QUEUE_DEPTH, sizeof(RxItem));
  esp_now_register_recv_cb(on_recv);
  xTaskCreate(espnow_rx_task, "espnow_rx", RX_TASK_STACK, nullptr, RX_TASK_PRIO, nullptr);
}
