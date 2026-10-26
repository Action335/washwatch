#include "batch_uploader.h"

size_t BatchUploader::poll(size_t buffered, uint32_t now_ms) {
  // Unsigned subtraction stays correct across millis() wraparound.
  if (in_flight_ && now_ms - sent_ms_ < BATCH_ACK_TIMEOUT_MS) return 0;
  if (buffered == 0) {
    in_flight_ = false;
    return 0;
  }
  n_ = buffered < LOG_BATCH_MAX ? buffered : LOG_BATCH_MAX;
  ackid_++;  // a new id for every send, so a late ack for an earlier send is ignored
  sent_ms_ = now_ms;
  in_flight_ = true;
  return n_;
}

size_t BatchUploader::on_response(uint32_t ackid, int errcode) {
  if (!in_flight_ || ackid != ackid_) return 0;
  in_flight_ = false;
  if (errcode == 0) {
    rejects_ = 0;
    return n_;
  }
  if (++rejects_ < BATCH_MAX_REJECTS) return 0;  // keep them; the next poll resends
  rejects_ = 0;
  rejected_ += n_;
  return n_;
}
