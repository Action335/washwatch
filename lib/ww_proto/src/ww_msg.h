// WashWatch node <-> gateway message format (ESP-NOW, packed binary).
// Shared by node firmware, gateway firmware and the native tests.
#pragma once
#include <stdint.h>

constexpr uint8_t MSG_VERSION = 3;  // 1 was AirBox; 3 adds vstd/lo/hi (flash node and gateway together)

// Washer phase for one window. The node fills it from a rule for now; the on-device
// classifier (E7) replaces the rule later without changing this format.
enum : uint8_t {
  PHASE_IDLE = 0,
  PHASE_WASH = 1,
  PHASE_SPIN = 2,
};

enum : uint8_t {
  FLAG_LID_OPEN = 1,
  FLAG_IMU_FAULT = 2,  // the MPU6050 didn't answer; the vibration fields are 0
};

typedef struct __attribute__((packed)) {
  uint8_t ver;          // MSG_VERSION
  uint8_t node_id;      // 1 = washer 1, 2 = washer 2
  uint32_t seq;         // +1 per packet; gaps give packet delivery rate
  uint32_t uptime_ms;   // drops back after a node reboot
  uint16_t mean_mg;     // mean |a| over the window, milli-g (about 1000 at rest)
  uint16_t std_mg;      // standard deviation of |a|, milli-g: how hard the drum shakes
  uint16_t p2p_mg;      // max - min of |a|, milli-g
  uint16_t vstd_mg;     // sqrt(var x + var y + var z), milli-g: also sees sideways shake that |a| misses
  uint16_t lo_mg;       // RMS of the 5-15 Hz band over all axes, milli-g (spin: ~11.7 Hz)
  uint16_t hi_mg;       // RMS of the 15-40 Hz band over all axes, milli-g (wash/rinse motor hum)
  uint8_t phase;        // PHASE_*
  uint8_t flags;        // FLAG_* bits
} node_msg_t;
static_assert(sizeof(node_msg_t) == 24, "node_msg_t must stay 24 bytes");

typedef struct __attribute__((packed)) {
  uint8_t ver;
  uint8_t node_id;
  uint32_t seq;
} ack_msg_t;
static_assert(sizeof(ack_msg_t) == 6, "ack_msg_t must stay 6 bytes");

constexpr uint32_t SEND_PERIOD_MS = 2000;  // one window per message
constexpr uint32_t PUBLISH_PERIOD_MS = 30000;  // live shadow; 30 s keeps NETPIE shadow writes in the free quota
constexpr uint32_t NODE_OFFLINE_MS = 30000;

constexpr uint8_t MAX_NODES = 8;

constexpr bool is_valid_node_id(uint8_t id) { return id >= 1 && id <= 254; }

// Wi-Fi channels used in Thailand. Nodes find the gateway by trying each in turn.
constexpr uint8_t WIFI_CHANNEL_MAX = 13;
constexpr uint8_t next_channel(uint8_t ch) { return (ch >= 1 && ch < WIFI_CHANNEL_MAX) ? ch + 1 : 1; }
