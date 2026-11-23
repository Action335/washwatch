#include "batch_uploader.h"

size_t BatchUploader::poll(size_t buffered, uint32_t now_ms) {
  if (in_flight_) {
    // Unsigned subtraction stays correct across millis() wraparound.
    if (now_ms - sent_ms_ < BATCH_ACK_TIMEOUT_MS) return 0;
    if (count_unanswered()) return 0;
  }
  if (buffered == 0) {
    in_flight_ = false;
    return 0;
  }
  n_ = buffered < LOG_BATCH_MAX ? buffered : LOG_BATCH_MAX;
  ackid_++;  // a new id for every send, so a late ack for an earlier send is ignored
  sent_ms_ = now_ms;
  in_flight_ = true;
  answered_ = false;
  return n_;
}

size_t BatchUploader::on_response(uint32_t ackid, int errcode) {
  if (!in_flight_ || answered_ || ackid != ackid_) return 0;
  timeouts_ = 0;  // NETPIE is answering
  disconnects_ = 0;
  if (errcode == 0) {
    in_flight_ = false;
    rejects_ = 0;
    return n_;
  }
  if (++rejects_ < BATCH_MAX_REJECTS) {
    answered_ = true;  // keep them; poll resends once BATCH_ACK_TIMEOUT_MS has passed (backoff)
    return 0;
  }
  in_flight_ = false;
  rejects_ = 0;
  rejected_ += n_;
  return n_;
}

bool BatchUploader::count_unanswered() {
  if (answered_ || ++timeouts_ < BATCH_MAX_TIMEOUTS) return false;
  // NETPIE never answers this batch (e.g. it can't parse it): drop it so it can't block newer data.
  drop_in_flight();
  return true;
}

void BatchUploader::drop_in_flight() {
  in_flight_ = false;
  timeouts_ = 0;
  disconnects_ = 0;
  rejected_ += n_;
  pending_drop_ += n_;
}

void BatchUploader::on_disconnect() {
  // The link dropped, not necessarily NETPIE refusing the batch: resend it after reconnecting.
  if (in_flight_ && !answered_ && ++disconnects_ >= BATCH_MAX_DISCONNECTS) drop_in_flight();
  in_flight_ = false;
}

size_t BatchUploader::take_dropped() {
  size_t n = pending_drop_;
  pending_drop_ = 0;
  return n;
}
