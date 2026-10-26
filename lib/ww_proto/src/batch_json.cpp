#include "batch_json.h"

#include <ArduinoJson.h>
#include <stdio.h>

bool should_log(const node_msg_t& m, WasherState state_after, uint8_t prev_flags) {
  if (state_after != WasherState::Idle || m.phase != PHASE_IDLE) return true;
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
      snprintf(key, sizeof(key), "w%u_phase", (unsigned)r.node_id);
      d[key] = r.phase;
    }
    snprintf(key, sizeof(key), "w%u_state", (unsigned)r.node_id);
    d[key] = r.state;
    snprintf(key, sizeof(key), "w%u_lid", (unsigned)r.node_id);
    d[key] = (r.flags & FLAG_LID_OPEN) ? 1 : 0;
    uint32_t age = now_ms - r.t_ms;  // unsigned: correct across millis() wraparound
    item["ts"] = -(int64_t)age;
  }
  size_t needed = measureJson(doc);
  if (needed + 1 > out_len) return 0;  // +1 for the terminator
  return serializeJson(doc, out, out_len);
}
