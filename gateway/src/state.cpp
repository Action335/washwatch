#include "state.h"

#include <math.h>

Washer g_washers[WASHER_COUNT] = {
    // Nominal cycle length in minutes, used for minutes left until the learned estimate replaces it.
    // The two washers are different models (both top-loaders; washer 1 is smaller).
    {1, WasherCycle(45)},  // NOT MEASURED yet: placeholder until washer 1 is recorded
    {2, WasherCycle(83)},  // measured 7 Nov 2026 (phone): ~83 min real, display said 65
};

NodeTable g_nodes;
static LogRecord log_storage[LOG_CAPACITY];
RecordBuffer g_log(log_storage, LOG_CAPACITY);
GatewaySensors g_env = {NAN, NAN, 0, false};
SemaphoreHandle_t g_state_mtx;
QueueHandle_t q_notices;

constexpr int NOTICE_QUEUE_DEPTH = 8;

void state_init() {
  g_state_mtx = xSemaphoreCreateMutex();
  q_notices = xQueueCreate(NOTICE_QUEUE_DEPTH, sizeof(Notice));
}

Washer* washer_for(uint8_t node_id) {
  for (Washer& w : g_washers)
    if (w.node_id == node_id) return &w;
  return nullptr;
}
