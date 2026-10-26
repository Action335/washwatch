// ESP-NOW link from a node to the gateway: broadcast send, ACK/retry, channel sweep.
#pragma once
#include <stdint.h>
#include "ww_msg.h"

// Sets up ESP-NOW on the last channel that worked (saved in flash).
void link_begin();
// Sends one message and blocks until it is ACKed or given up (450 ms, plus ~0.4 s when it
// sweeps the other channels to find the gateway). Returns true if ACKed.
bool link_send(const node_msg_t& m);
uint8_t link_channel();
uint32_t link_misses();  // sends in a row without an ACK
