#ifdef SIM_NODES
#include "sim_nodes.h"

#include <Arduino.h>

#include "espnow_rx.h"

constexpr uint32_t SIM_RUN_MS = 60000;  // node 1 washes for the first minute
constexpr uint16_t SIM_WASH_STD_MG = 60;

static void sim_nodes_task(void*) {
  uint32_t seq = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SEND_PERIOD_MS));
    uint32_t now = millis();
    for (uint8_t id = 1; id <= 2; id++) {
      node_msg_t m = {};
      m.ver = MSG_VERSION;
      m.node_id = id;
      m.seq = seq;
      m.uptime_ms = now;
      m.mean_mg = 1000;
      bool washing = id == 1 && now < SIM_RUN_MS;
      m.std_mg = washing ? SIM_WASH_STD_MG : 2;
      m.p2p_mg = m.std_mg * 4;
      m.vstd_mg = washing ? SIM_WASH_STD_MG * 2 : 3;  // sideways shake that |a| misses
      m.lo_mg = washing ? SIM_WASH_STD_MG / 4 : 1;
      m.hi_mg = washing ? SIM_WASH_STD_MG : 1;
      m.phase = washing ? PHASE_WASH : PHASE_IDLE;
      handle_node_msg(nullptr, m);
    }
    seq++;
  }
}

void sim_nodes_start() { xTaskCreate(sim_nodes_task, "sim_nodes", 4096, nullptr, 3, nullptr); }
#endif
