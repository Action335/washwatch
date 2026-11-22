// Training log: which windows to keep, and the NETPIE batch update that uploads them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "log_record.h"
#include "washer_cycle.h"
#include "ww_msg.h"

constexpr uint32_t IDLE_LOG_EVERY = 15;  // one idle window in 15 = one per 30 s
constexpr size_t LOG_BATCH_MAX = 50;      // NETPIE allows 100
constexpr size_t BATCH_MAX_BYTES = 8192;  // LOG_BATCH_MAX records at their widest fit (test_full_batch_fits_the_mqtt_buffer)

// "Moving" for the log: vector std at or above this. Set just above the still-sensor noise floor so
// that gentle wash/rinse counts, since the training data must include it. CALIBRATE from idle data.
constexpr uint16_t LOG_MOVING_VSTD_MG = 6;
// After the last moving window, keep logging every window this long, so the quiet soaks inside a
// cycle (6-8 min on 7 Nov) and the quiet tail after it are kept in full for training.
constexpr uint32_t LOG_HOLD_MS = 10 * 60 * 1000;

// Decides which windows of one washer go to the training log: every window while the washer is
// running, moving or within LOG_HOLD_MS of moving; one quiet window in IDLE_LOG_EVERY otherwise
// (idle or done); and every lid change.
class LogGate {
 public:
  // prev_flags: flags of the node's previous accepted message (0 if none).
  bool should_log(const node_msg_t& m, WasherState state_after, uint8_t prev_flags, uint32_t now_ms);

 private:
  bool moved_ = false;
  uint32_t last_moving_ms_ = 0;
};

LogRecord make_record(const node_msg_t& m, WasherState state_after, uint32_t now_ms);

// {"ackid":A,"merged":true,"batch":[{"data":{"w1_std":..},"ts":-<age ms>}, ...]}
// ts is relative (negative = that many ms ago), so the gateway needs no synced clock.
// Returns bytes written, or 0 if n == 0 or out_len is too small.
size_t build_batch_update(const LogRecord* recs, size_t n, uint32_t now_ms, uint32_t ackid, char* out,
                          size_t out_len);
