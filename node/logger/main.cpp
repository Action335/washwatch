// Raw MPU6050 logger for recording real wash cycles (env `logger`): streams every 100 Hz sample over
// USB serial as CSV so thresholds and features can be worked out on a PC. Same driver and mounting
// as the washer node, no radio.
//
// Columns: seq (sample period, gaps = dropped lines), ms since boot, x/y/z raw counts (8192 per g,
// empty when the read failed), lid (1 = open; reads 1 when no reed switch is wired).

#include <Arduino.h>

#include "../src/imu.h"

constexpr int PIN_LID = 25;  // same wiring as the node: reed switch to GND, internal pull-up
constexpr uint32_t SAMPLE_PERIOD_MS = 10;  // 100 Hz, as the node

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LID, INPUT_PULLUP);
  delay(500);
  if (!imu_begin()) Serial.println("# imu: MPU6050 not found");
  Serial.println("seq,ms,ax,ay,az,lid");
}

void loop() {
  static uint32_t seq = 0;
  static TickType_t wake = xTaskGetTickCount();
  vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  int16_t a[3];
  int lid = digitalRead(PIN_LID) == HIGH;
  if (imu_read_raw(a)) {
    Serial.printf("%u,%u,%d,%d,%d,%d\n", seq, millis(), a[0], a[1], a[2], lid);
  } else {
    Serial.printf("%u,%u,,,,%d\n", seq, millis(), lid);
  }
  seq++;
}
