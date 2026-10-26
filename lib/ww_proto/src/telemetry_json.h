// Builds the NETPIE shadow update ({"data":{...}}) from the gateway's state.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "node_table.h"
#include "washer_cycle.h"

struct WasherView {
  uint8_t node_id;
  const WasherCycle* cycle;
};

struct GatewaySensors {
  float temp_c;       // NAN if the DHT22 read failed
  float rh;           // NAN if the DHT22 read failed
  uint16_t rain_raw;  // ADC 0-4095 from the rain (or light) sensor
  bool pir;           // someone at the washers right now
};

// Training-log upload state, shown on the dashboard so an outage or data loss is visible.
struct LogStats {
  uint32_t buffered;  // records waiting to upload
  uint32_t dropped;   // overwritten because the buffer was full
  uint32_t rejected;  // dropped after NETPIE refused them BATCH_MAX_REJECTS times
};

// Returns the bytes written (no terminator counted), or 0 if out_len is too small.
size_t build_shadow_update(const NodeTable& t, const WasherView* washers, size_t n_washers,
                           const GatewaySensors& env, const LogStats& log, const char* gw_id, uint32_t ts,
                           uint32_t now_ms, char* out, size_t out_len);
