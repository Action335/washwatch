#include "sender.h"

constexpr uint32_t Sender::WAIT_MS[4];

void Sender::start(uint32_t seq, uint32_t now_ms, uint8_t max_tx) {
  busy_ = true;
  max_tx_ = max_tx < 1 ? 1 : (max_tx > 4 ? 4 : max_tx);
  seq_ = seq;
  sent_ = 0;
  last_ms_ = now_ms;
}

SendAction Sender::poll(uint32_t now_ms) {
  if (!busy_) return SendAction::None;
  if (sent_ == 0) {
    sent_ = 1;
    last_ms_ = now_ms;
    return SendAction::Transmit;
  }
  // Unsigned subtraction stays correct across millis() wraparound.
  if (now_ms - last_ms_ < WAIT_MS[sent_ - 1]) return SendAction::None;
  if (sent_ == max_tx_) {
    busy_ = false;
    give_ups_++;
    return SendAction::GiveUp;
  }
  sent_++;
  last_ms_ = now_ms;
  return SendAction::Transmit;
}

void Sender::on_ack(const ack_msg_t& ack, uint8_t my_node_id) {
  if (!busy_ || ack.ver != MSG_VERSION || ack.node_id != my_node_id || ack.seq != seq_) return;
  busy_ = false;
  give_ups_ = 0;
}
