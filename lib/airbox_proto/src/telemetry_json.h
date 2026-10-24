// Builds the NETPIE shadow update ({"data":{...}}) from the gateway's node table.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "node_table.h"

// Returns the bytes written (no terminator counted), or 0 if out_len is too small.
// Sensor fields that are NOT_FITTED / TEMP_NOT_FITTED are left out.
size_t build_shadow_update(const NodeTable& t, const char* gw_id, uint32_t ts, uint32_t now_ms,
                           char* out, size_t out_len);
