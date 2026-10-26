#include "state.h"

#include <math.h>

Washer g_washers[WASHER_COUNT] = {
    {1, WasherCycle(45)},
    {2, WasherCycle(45)},
};

NodeTable g_nodes;
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
