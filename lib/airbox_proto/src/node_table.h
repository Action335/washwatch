// Gateway-side table of sensor nodes: dedup, reboot detection, PDR, online state.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "airbox_msg.h"

enum class Ingest { Accepted, Duplicate, Rejected };

struct NodeEntry {
  node_msg_t last;        // latest accepted message
  uint32_t last_seen_ms;  // gateway time of that message
  uint32_t first_seq;     // first seq since boot (or since the node rebooted)
  uint32_t received;      // accepted messages since first_seq
};

class NodeTable {
 public:
  Ingest ingest(const node_msg_t& m, uint32_t now_ms);
  const NodeEntry* find(uint8_t node_id) const;
  bool online(uint8_t node_id, uint32_t now_ms) const;
  // Time since the node's last message; 0 if it was stored after now_ms
  // (another task can ingest between the caller reading millis() and calling this).
  uint32_t age_ms(uint8_t node_id, uint32_t now_ms) const;
  // Packet delivery rate: received / (last.seq - first_seq + 1); 0 for an unknown id.
  float pdr(uint8_t node_id) const;
  size_t count() const { return count_; }
  const NodeEntry& at(size_t i) const { return entries_[i]; }

 private:
  NodeEntry entries_[MAX_NODES] = {};
  size_t count_ = 0;

  NodeEntry* find_mut(uint8_t node_id);
};
