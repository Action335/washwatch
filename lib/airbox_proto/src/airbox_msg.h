// AirBox node <-> gateway message format (ESP-NOW, packed binary).
// Shared by node firmware, gateway firmware and the native tests.
#pragma once
#include <stdint.h>

constexpr uint8_t MSG_VERSION = 1;

// Any u16 sensor field whose sensor is not fitted on this node.
constexpr uint16_t NOT_FITTED = 0xFFFF;
constexpr int16_t TEMP_NOT_FITTED = INT16_MIN;

enum : uint8_t {
  FLAG_PMS_FAULT = 1,
  FLAG_DHT_FAULT = 2,
  FLAG_MQ_WARMUP = 4,
  FLAG_LUX_FAULT = 8,
};

typedef struct __attribute__((packed)) {
  uint8_t ver;                 // MSG_VERSION
  uint8_t node_id;             // 1 = indoor, 2 = outdoor, 3+ = simulated/future
  uint32_t seq;                // +1 per packet; gaps give packet delivery rate
  uint32_t uptime_ms;          // drops back after a node reboot
  uint16_t pm1, pm25, pm10;    // ug/m3, PMS5003 atmospheric values
  uint16_t n03, n05, n10, n25; // particles per 0.1 L (>0.3, >0.5, >1.0, >2.5 um)
  int16_t temp_c10;            // degC x 10
  uint16_t rh_10;              // %RH x 10
  uint16_t mq135_raw;          // ADC 0-4095
  uint16_t lux;                // BH1750 lux
  uint8_t flags;               // FLAG_* bits
} node_msg_t;
static_assert(sizeof(node_msg_t) == 33, "node_msg_t must stay 33 bytes");

typedef struct __attribute__((packed)) {
  uint8_t ver;
  uint8_t node_id;
  uint32_t seq;
} ack_msg_t;
static_assert(sizeof(ack_msg_t) == 6, "ack_msg_t must stay 6 bytes");

constexpr uint32_t SEND_PERIOD_MS = 2000;
constexpr uint32_t PUBLISH_PERIOD_MS = 10000;
constexpr uint32_t NODE_OFFLINE_MS = 30000;

constexpr uint8_t MAX_NODES = 8;

constexpr bool is_valid_node_id(uint8_t id) { return id >= 1 && id <= 254; }

// Wi-Fi channels used in Thailand. Nodes find the gateway by trying each in turn.
constexpr uint8_t WIFI_CHANNEL_MAX = 13;
constexpr uint8_t next_channel(uint8_t ch) { return (ch >= 1 && ch < WIFI_CHANNEL_MAX) ? ch + 1 : 1; }
