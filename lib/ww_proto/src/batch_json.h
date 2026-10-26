// Training log: which windows to keep, and the NETPIE batch update that uploads them.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "log_record.h"
#include "washer_cycle.h"
#include "ww_msg.h"

constexpr uint32_t IDLE_LOG_EVERY = 15;  // one idle window in 15 = one per 30 s
constexpr size_t LOG_BATCH_MAX = 50;      // NETPIE allows 100; 50 keeps the MQTT buffer at 6 KB

// Every window while the washer is running or moving, one quiet window in IDLE_LOG_EVERY (idle or
// done), and every lid change.
// prev_flags: flags of the node's previous accepted message (0 if none).
bool should_log(const node_msg_t& m, WasherState state_after, uint8_t prev_flags);
LogRecord make_record(const node_msg_t& m, WasherState state_after, uint32_t now_ms);

// {"ackid":A,"merged":true,"batch":[{"data":{"w1_std":..},"ts":-<age ms>}, ...]}
// ts is relative (negative = that many ms ago), so the gateway needs no synced clock.
// Returns bytes written, or 0 if n == 0 or out_len is too small.
size_t build_batch_update(const LogRecord* recs, size_t n, uint32_t now_ms, uint32_t ackid, char* out,
                          size_t out_len);
