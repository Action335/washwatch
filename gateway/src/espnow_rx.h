// ESP-NOW receive path: recv callback -> q_node_msgs -> espnow_rx_task (ingest, cycle logic, ACK).
#pragma once

// Call after esp_now_init() and state_init(). Creates the queue and espnow_rx_task (priority 5).
void espnow_rx_start();
