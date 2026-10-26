// Store-and-forward upload logic (E4): send the oldest buffered records, and remove them from
// the buffer only when NETPIE acks that batch's ackid. Time is passed in, so it runs on the PC too.
//
// Resending after a timeout is safe: NETPIE stores points by timestamp, so a batch that did
// arrive the first time is overwritten with the same values.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "batch_json.h"

constexpr uint32_t BATCH_ACK_TIMEOUT_MS = 5000;
constexpr uint8_t BATCH_MAX_REJECTS = 3;  // then drop the batch so it can't block newer data

class BatchUploader {
 public:
  // How many of the oldest records to send now, or 0. When non-zero, use ackid() in the payload.
  size_t poll(size_t buffered, uint32_t now_ms);
  uint32_t ackid() const { return ackid_; }
  // A response arrived; errcode 0 = accepted. Returns how many oldest records to pop (0 = keep them).
  size_t on_response(uint32_t ackid, int errcode);
  // Forget the in-flight batch; it is sent again after reconnecting.
  void on_disconnect() { in_flight_ = false; }
  uint32_t rejected() const { return rejected_; }

 private:
  bool in_flight_ = false;
  size_t n_ = 0;  // records in the in-flight batch
  uint32_t ackid_ = 0;
  uint32_t sent_ms_ = 0;
  uint8_t rejects_ = 0;  // errors in a row for the current oldest records
  uint32_t rejected_ = 0;
};
