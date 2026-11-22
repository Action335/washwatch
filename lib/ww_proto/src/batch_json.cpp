#include "batch_json.h"

#include <ArduinoJson.h>
#include <stdio.h>

bool LogGate::should_log(const node_msg_t& m, WasherState state_after, uint8_t prev_flags, uint32_t now_ms) {
  const bool fault = m.flags & FLAG_IMU_FAULT;
  const bool moving = !fault && (m.vstd_mg >= LOG_MOVING_VSTD_MG || m.phase != PHASE_IDLE);
  if (moving) {
    moved_ = true;
    last_moving_ms_ = now_ms;
    return true;
  }
  // Done is quiet and can last for hours (nobody opens the lid), so after the hold it is logged at
  // the idle rate.
  if (state_after == WasherState::Running) return true;
  if (moved_ && now_ms - last_moving_ms_ < LOG_HOLD_MS) return true;  // unsigned: survives millis() wrap
  if ((m.flags ^ prev_flags) & FLAG_LID_OPEN) return true;
  return m.seq % IDLE_LOG_EVERY == 0;
}

LogRecord make_record(const node_msg_t& m, WasherState state_after, uint32_t now_ms) {
  LogRecord r = {};
  r.t_ms = now_ms;
  r.node_id = m.node_id;
  r.state = (uint8_t)state_after;
  r.phase = m.phase;
  r.flags = m.flags;
  r.std_mg = m.std_mg;
  r.p2p_mg = m.p2p_mg;
  r.vstd_mg = m.vstd_mg;
  r.lo_mg = m.lo_mg;
  r.hi_mg = m.hi_mg;
  return r;
}

size_t build_batch_update(const LogRecord* recs, size_t n, uint32_t now_ms, uint32_t ackid, char* out,
                          size_t out_len) {
  if (n == 0) return 0;
  JsonDocument doc;
  doc["ackid"] = ackid;
  doc["merged"] = true;
  JsonArray batch = doc["batch"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    const LogRecord& r = recs[i];
    JsonObject item = batch.add<JsonObject>();
    JsonObject d = item["data"].to<JsonObject>();
    char key[16];
    if (!(r.flags & FLAG_IMU_FAULT)) {
      snprintf(key, sizeof(key), "w%u_std", (unsigned)r.node_id);
      d[key] = r.std_mg;
      snprintf(key, sizeof(key), "w%u_p2p", (unsigned)r.node_id);
      d[key] = r.p2p_mg;
      snprintf(key, sizeof(key), "w%u_vstd", (unsigned)r.node_id);
      d[key] = r.vstd_mg;
      snprintf(key, sizeof(key), "w%u_lo", (unsigned)r.node_id);
      d[key] = r.lo_mg;
      snprintf(key, sizeof(key), "w%u_hi", (unsigned)r.node_id);
      d[key] = r.hi_mg;
      // No w_phase: it is the uncalibrated rule's output, recomputable from w_std, and each key
      // costs Feed quota.
    }
    snprintf(key, sizeof(key), "w%u_state", (unsigned)r.node_id);
    d[key] = r.state;
    snprintf(key, sizeof(key), "w%u_lid", (unsigned)r.node_id);
    d[key] = (r.flags & FLAG_LID_OPEN) ? 1 : 0;
    uint32_t age = now_ms - r.t_ms;  // unsigned: correct across millis() wraparound
    // A record logged just after the caller read now_ms would underflow to ~49.7 days: call it "now".
    if (age > INT32_MAX) age = 0;
    item["ts"] = -(int64_t)age;
  }
  size_t needed = measureJson(doc);
  if (needed + 1 > out_len) return 0;  // +1 for the terminator
  return serializeJson(doc, out, out_len);
}
