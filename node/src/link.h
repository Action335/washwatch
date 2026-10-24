// ESP-NOW link from a node to the gateway: channel scan, broadcast send, ACK/retry.
#pragma once
#include <stdint.h>
#include "airbox_msg.h"

// Finds the router's Wi-Fi channel (the gateway uses the same one) and sets up ESP-NOW.
void link_begin();
// Sends one message and blocks until it is ACKed or given up (450 ms at most).
// Returns true if ACKed.
bool link_send(const node_msg_t& m);
uint32_t link_give_ups();
