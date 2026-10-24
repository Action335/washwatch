// AirBox sensor node: read sensors every 2 s, send to the gateway over ESP-NOW.
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "airbox_msg.h"
#include "link.h"
#include "sensors.h"

#if SIMULATED
// One board plays two nodes: Scalability 2 pts needs 1 physical + at least 2 simulated.
static const uint8_t NODE_IDS[] = {3, 4};
#elif defined(NODE_ID)
static const uint8_t NODE_IDS[] = {NODE_ID};
#else
#error "Build with -DNODE_ID=<1..254> or -DSIMULATED=1 (see platformio.ini envs)"
#endif
constexpr size_t NODE_COUNT = sizeof(NODE_IDS) / sizeof(NODE_IDS[0]);

constexpr int WDT_TIMEOUT_S = 10;

static uint32_t seqs[NODE_COUNT] = {};  // one seq counter per node ID
static uint32_t next_ms = 0;

void setup() {
  Serial.begin(115200);
  Serial.printf("AirBox node starting, ids:");
  for (size_t i = 0; i < NODE_COUNT; i++) Serial.printf(" %u", NODE_IDS[i]);
  Serial.println();
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
  sensors_begin();
  link_begin();
}

void loop() {
  esp_task_wdt_reset();
  if ((int32_t)(millis() - next_ms) < 0) {
    delay(5);
    return;
  }
  next_ms = millis() + SEND_PERIOD_MS;

  for (size_t i = 0; i < NODE_COUNT; i++) {
    node_msg_t m = {};
    m.ver = MSG_VERSION;
    m.node_id = NODE_IDS[i];
    m.seq = seqs[i]++;
    m.uptime_ms = millis();
    sensors_read(m);
    bool acked = link_send(m);

    // The bench checks read these lines: channel and misses show the link state.
    Serial.printf("id=%u seq=%lu pm25=%u t=%d rh=%u mq=%u lux=%u flags=0x%02x ch=%u misses=%lu %s\n",
                  m.node_id, (unsigned long)m.seq, m.pm25, m.temp_c10, m.rh_10, m.mq135_raw, m.lux,
                  m.flags, link_channel(), (unsigned long)link_misses(), acked ? "acked" : "gave_up");
  }
}
