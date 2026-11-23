#include "telemetry_json.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>

static double round_to(double x, double step) { return round(x / step) * step; }

static const char* state_name(WasherState s) {
  switch (s) {
    case WasherState::Running: return "running";
    case WasherState::Done: return "done";
    default: return "idle";
  }
}

static const char* phase_name(uint8_t p) {
  switch (p) {
    case PHASE_WASH: return "wash";
    case PHASE_SPIN: return "spin";
    default: return "idle";
  }
}

// A failed reading is sent as null, so the shadow doesn't keep showing its old value.
static void put_float(JsonObject o, const char* key, float v) {
  if (isnan(v)) {
    o[key] = nullptr;
  } else {
    o[key] = round_to(v, 0.1);
  }
}

size_t build_shadow_update(const NodeTable& t, const WasherView* washers, size_t n_washers,
                           const GatewaySensors& env, const LogStats& log, const char* gw_id, uint32_t ts,
                           uint32_t now_ms, char* out, size_t out_len, const GatewayDiag* diag) {
  JsonDocument doc;
  JsonObject data = doc["data"].to<JsonObject>();
  data["gw"] = gw_id;
  data["ts"] = ts;

  JsonObject e = data["env"].to<JsonObject>();
  put_float(e, "t", env.temp_c);
  put_float(e, "rh", env.rh);
  e["rain"] = env.rain_raw;
  e["pir"] = env.pir;

  JsonObject lg = data["log"].to<JsonObject>();
  lg["buffered"] = log.buffered;
  lg["dropped"] = log.dropped;
  lg["rejected"] = log.rejected;

  if (diag != nullptr) {
    JsonObject g = data["diag"].to<JsonObject>();
    g["up_s"] = diag->up_s;
    g["reset"] = diag->reset;
    g["rssi"] = diag->rssi;
    g["wifi_drops"] = diag->wifi_drops;
    g["mqtt_drops"] = diag->mqtt_drops;
    g["off_s"] = diag->off_s;
    g["mqtt_rc"] = diag->mqtt_rc;
  }

  JsonObject ws = data["washers"].to<JsonObject>();
  for (size_t i = 0; i < n_washers; i++) {
    const WasherView& v = washers[i];
    char key[4];
    snprintf(key, sizeof(key), "%u", (unsigned)v.node_id);
    JsonObject w = ws[key].to<JsonObject>();
    w["state"] = state_name(v.cycle->state());
    w["min_left"] = v.cycle->minutes_left(now_ms);
    w["online"] = t.online(v.node_id, now_ms);

    const NodeEntry* n = t.find(v.node_id);
    if (n == nullptr) continue;  // never heard from: state only
    const node_msg_t& m = n->last;
    const bool imu_fault = m.flags & FLAG_IMU_FAULT;
    if (imu_fault) {
      w["phase"] = nullptr;
      w["std_mg"] = nullptr;
      w["p2p_mg"] = nullptr;
    } else {
      w["phase"] = phase_name(m.phase);
      w["std_mg"] = m.std_mg;
      w["p2p_mg"] = m.p2p_mg;
    }
    w["imu_fault"] = imu_fault;
    w["lid_open"] = (bool)(m.flags & FLAG_LID_OPEN);
    w["seq"] = m.seq;
    w["age_s"] = t.age_ms(v.node_id, now_ms) / 1000;
    w["pdr"] = round_to(t.pdr(v.node_id), 0.001);
  }

  size_t needed = measureJson(doc);
  if (needed + 1 > out_len) return 0;  // +1 for the terminator
  return serializeJson(doc, out, out_len);
}
