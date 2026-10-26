#include "node_table.h"

NodeEntry* NodeTable::find_mut(uint8_t node_id) {
  for (size_t i = 0; i < count_; i++)
    if (entries_[i].last.node_id == node_id) return &entries_[i];
  return nullptr;
}

const NodeEntry* NodeTable::find(uint8_t node_id) const {
  for (size_t i = 0; i < count_; i++)
    if (entries_[i].last.node_id == node_id) return &entries_[i];
  return nullptr;
}

Ingest NodeTable::ingest(const node_msg_t& m, uint32_t now_ms) {
  if (m.ver != MSG_VERSION || !is_valid_node_id(m.node_id)) return Ingest::Rejected;

  NodeEntry* e = find_mut(m.node_id);
  if (e == nullptr) {
    if (count_ == MAX_NODES) return Ingest::Rejected;
    e = &entries_[count_++];
    e->first_seq = m.seq;
    e->received = 0;
  } else if (m.seq == e->last.seq && m.uptime_ms == e->last.uptime_ms) {
    // A retransmit is byte-identical: the node missed our ACK.
    return Ingest::Duplicate;
  } else if (m.seq <= e->last.seq || m.uptime_ms < e->last.uptime_ms) {
    // The node rebooted, so its seq restarted: start counting again. Checking seq too
    // catches a reboot we only hear after its new uptime has passed the old one.
    e->first_seq = m.seq;
    e->received = 0;
  }

  e->last = m;
  e->last_seen_ms = now_ms;
  e->received++;
  return Ingest::Accepted;
}

uint32_t NodeTable::age_ms(uint8_t node_id, uint32_t now_ms) const {
  const NodeEntry* e = find(node_id);
  if (e == nullptr) return UINT32_MAX;
  uint32_t age = now_ms - e->last_seen_ms;
  return (int32_t)age < 0 ? 0 : age;
}

bool NodeTable::online(uint8_t node_id, uint32_t now_ms) const {
  return age_ms(node_id, now_ms) < NODE_OFFLINE_MS;
}

float NodeTable::pdr(uint8_t node_id) const {
  const NodeEntry* e = find(node_id);
  if (e == nullptr) return 0.0f;
  return (float)e->received / (float)(e->last.seq - e->first_seq + 1);
}
