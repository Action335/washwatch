// Gateway state shared between tasks. Every access to g_nodes, g_washers, g_env and g_log holds g_state_mtx.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "batch_json.h"
#include "lid_filter.h"
#include "node_table.h"
#include "record_buffer.h"
#include "telemetry_json.h"
#include "washer_cycle.h"

constexpr const char* GW_ID = "washwatch-01";

struct Washer {
  uint8_t node_id;
  WasherCycle cycle;
  LogGate log_gate;  // which of its windows go to the training log
  LidFilter lid;     // reed chatter filter, applied before anything reads the lid flag
};

// Nominal cycle lengths (minutes): set them from each washer's own display.
constexpr size_t WASHER_COUNT = 2;
extern Washer g_washers[WASHER_COUNT];

extern NodeTable g_nodes;

// Training-log records waiting to upload (E4 store-and-forward). RAM only: lost on reboot.
constexpr size_t LOG_CAPACITY = 3000;  // 20 bytes each = ~60 KB; about 50 min of both washers running
extern RecordBuffer g_log;
extern GatewaySensors g_env;
extern SemaphoreHandle_t g_state_mtx;

// Alerts for notify_task: a washer's cycle event, or rain at the gateway (node_id and event unused).
struct Notice {
  uint8_t node_id;
  CycleEvent event;
  bool rain;
};
extern QueueHandle_t q_notices;

// Creates the mutex and the notice queue. Call first in setup().
void state_init();
// Returns the washer for a node ID, or nullptr. Caller holds g_state_mtx.
Washer* washer_for(uint8_t node_id);
