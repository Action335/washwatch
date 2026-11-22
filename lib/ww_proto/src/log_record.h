// One training-log record: a node's feature window as the gateway saw it.
#pragma once
#include <stdint.h>

struct LogRecord {
  uint32_t t_ms;     // gateway millis() when the window arrived
  uint8_t node_id;
  uint8_t state;     // (uint8_t)WasherState after this message
  uint8_t phase;     // PHASE_*
  uint8_t flags;     // FLAG_* from the node message
  uint16_t std_mg, p2p_mg;
  uint16_t vstd_mg, lo_mg, hi_mg;
};
