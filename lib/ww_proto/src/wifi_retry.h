#pragma once
#include <stdint.h>

// Which channel the gateway's Nth reconnect attempt (1-based) should use.
// Rejoining on the last known channel is fast, but a router that rebooted onto a new
// channel would never be found that way, so every WIFI_SCAN_EVERY-th attempt (and any
// attempt before a channel is known) passes 0, which makes WiFi.begin scan all channels.
constexpr uint32_t WIFI_SCAN_EVERY = 5;

inline uint8_t wifi_retry_channel(uint8_t last_channel, uint32_t attempt) {
  if (last_channel == 0 || attempt % WIFI_SCAN_EVERY == 0) return 0;
  return last_channel;
}
