// AirBox sensor node: read sensors every 2 s, send to the gateway over ESP-NOW.
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "airbox_msg.h"
#include "link.h"
#include "sensors.h"

#ifndef NODE_ID
#error "Build with -DNODE_ID=<1..254> (see platformio.ini envs)"
#endif

constexpr int WDT_TIMEOUT_S = 10;

static uint32_t seq = 0;
static uint32_t next_ms = 0;

void setup() {
  Serial.begin(115200);
  Serial.printf("AirBox node %d starting\n", NODE_ID);
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

  node_msg_t m = {};
  m.ver = MSG_VERSION;
  m.node_id = NODE_ID;
  m.seq = seq;
  m.uptime_ms = millis();
  sensors_read(m);
  bool acked = link_send(m);
  seq++;

  Serial.printf("seq=%lu pm25=%u t=%d rh=%u mq=%u lux=%u flags=0x%02x %s\n", (unsigned long)m.seq,
                m.pm25, m.temp_c10, m.rh_10, m.mq135_raw, m.lux, m.flags,
                acked ? "acked" : "gave_up");
}
