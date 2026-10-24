// ESP-NOW receive path: recv callback -> q_node_msgs -> espnow_rx_task (ingest + ACK).
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "node_table.h"

// Shared with cloud_task. Every access holds g_nodes_mtx.
extern NodeTable g_nodes;
extern SemaphoreHandle_t g_nodes_mtx;

// Call after esp_now_init(). Creates the queue and espnow_rx_task (priority 5).
void espnow_rx_start();
