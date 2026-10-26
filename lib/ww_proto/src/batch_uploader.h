// Store-and-forward upload logic (E4): send the oldest buffered records, and remove them from
// the buffer only when NETPIE acks that batch's ackid. Time is passed in, so it runs on the PC too.
//
// A resend after a timeout may duplicate points if the first send did arrive (relative timestamps
// are anchored to NETPIE's receive time, so they can differ by a few ms). Deduplicate on export.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "batch_json.h"

constexpr uint32_t BATCH_ACK_TIMEOUT_MS = 5000;  // also the wait before resending after an error
constexpr uint8_t BATCH_MAX_REJECTS = 3;   // error replies in a row, then drop the batch
constexpr uint8_t BATCH_MAX_TIMEOUTS = 6;  // unanswered sends in a row (30 s), then drop the batch

class BatchUploader {
 public:
  // How many of the oldest records to send now, or 0. When non-zero, use ackid() in the payload.
  size_t poll(size_t buffered, uint32_t now_ms);
  uint32_t ackid() const { return ackid_; }
  // A response arrived; errcode 0 = accepted. Returns how many oldest records to pop (0 = keep them).
  size_t on_response(uint32_t ackid, int errcode);
  // Records poll() gave up on after BATCH_MAX_TIMEOUTS; the caller pops them. Reported once.
  size_t take_dropped();
  // Forget the in-flight batch; it is sent again after reconnecting.
  void on_disconnect() { in_flight_ = false; }
  uint32_t rejected() const { return rejected_; }

 private:
  bool in_flight_ = false;
  bool answered_ = false;  // the in-flight send got an error reply: wait, then resend
  size_t n_ = 0;           // records in the in-flight batch
  uint32_t ackid_ = 0;
  uint32_t sent_ms_ = 0;
  uint8_t rejects_ = 0;    // error replies in a row for the current oldest records
  uint8_t timeouts_ = 0;   // unanswered sends in a row
  size_t pending_drop_ = 0;
  uint32_t rejected_ = 0;
};
