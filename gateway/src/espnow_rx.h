// ESP-NOW receive path: recv callback -> q_node_msgs -> espnow_rx_task (ingest, cycle logic, ACK).
#pragma once

#include "ww_msg.h"

// Call after esp_now_init() and state_init(). Creates the queue and espnow_rx_task (priority 5).
void espnow_rx_start();

// Ingest, cycle logic, training log and notices for one message, then ACK it.
// mac == nullptr: no ACK (a simulated node in the gateway_sim build).
void handle_node_msg(const uint8_t* mac, const node_msg_t& m);
