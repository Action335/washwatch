#include "telemetry_json.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>

static double round_to(double x, double step) { return round(x / step) * step; }

static void put_u16(JsonObject o, const char* key, uint16_t v) {
  if (v != NOT_FITTED) o[key] = v;
}

size_t build_shadow_update(const NodeTable& t, const char* gw_id, uint32_t ts, uint32_t now_ms,
                           char* out, size_t out_len) {
  JsonDocument doc;
  JsonObject data = doc["data"].to<JsonObject>();
  data["gw"] = gw_id;
  data["ts"] = ts;
  JsonObject nodes = data["nodes"].to<JsonObject>();

  for (size_t i = 0; i < t.count(); i++) {
    const NodeEntry& e = t.at(i);
    const node_msg_t& m = e.last;
    char key[4];
    snprintf(key, sizeof(key), "%u", (unsigned)m.node_id);
    JsonObject n = nodes[key].to<JsonObject>();

    put_u16(n, "pm1", m.pm1);
    put_u16(n, "pm25", m.pm25);
    put_u16(n, "pm10", m.pm10);
    if (m.temp_c10 != TEMP_NOT_FITTED) n["t"] = round_to(m.temp_c10 / 10.0, 0.1);
    if (m.rh_10 != NOT_FITTED) n["rh"] = round_to(m.rh_10 / 10.0, 0.1);
    put_u16(n, "mq", m.mq135_raw);
    put_u16(n, "lux", m.lux);
    n["flags"] = m.flags;
    n["seq"] = m.seq;
    n["age_s"] = (now_ms - e.last_seen_ms) / 1000;
    n["online"] = t.online(m.node_id, now_ms);
    n["pdr"] = round_to(t.pdr(m.node_id), 0.001);
  }

  size_t needed = measureJson(doc);
  if (needed + 1 > out_len) return 0;  // +1 for the terminator
  return serializeJson(doc, out, out_len);
}
