// Node-side ACK/retry state machine. Time is passed in, so it runs on the PC too.
#pragma once
#include <stdint.h>
#include "airbox_msg.h"

enum class SendAction { None, Transmit, GiveUp };

class Sender {
 public:
  // Begins a new packet; the next poll() returns Transmit. max_tx (1..4) is the number of
  // transmissions before GiveUp: 4 for normal sends, 1 for a quick probe during a channel sweep.
  void start(uint32_t seq, uint32_t now_ms, uint8_t max_tx = 4);
  SendAction poll(uint32_t now_ms);
  // Ignores ACKs for another node or another seq.
  void on_ack(const ack_msg_t& ack, uint8_t my_node_id);
  bool busy() const { return busy_; }
  uint32_t consecutive_give_ups() const { return give_ups_; }

 private:
  // Wait after transmission k before the next try; the last one ends in GiveUp.
  // 1 transmit + 3 retries = 450 ms worst case, well inside SEND_PERIOD_MS.
  static constexpr uint32_t WAIT_MS[4] = {30, 60, 120, 240};
  bool busy_ = false;
  uint32_t seq_ = 0;
  uint8_t sent_ = 0;  // transmissions so far for this packet
  uint8_t max_tx_ = 4;
  uint32_t last_ms_ = 0;
  uint32_t give_ups_ = 0;
};
