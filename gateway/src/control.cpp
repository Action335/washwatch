#include "control.h"

#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>

#include "rain_watch.h"
#include "state.h"

// Pins as tested in Wokwi.
constexpr int PIN_PIR = 27;
constexpr int PIN_DHT = 4;
constexpr int PIN_RAIN = 34;  // ADC1 only: ADC2 pins stop working when Wi-Fi is on
constexpr int PIN_DIAL[WASHER_COUNT] = {13, 26};
constexpr int PIN_FLAG[WASHER_COUNT] = {14, 32};

// Each washer has its own paper dial face, scaled to its cycle (washer 2 runs ~83 min).
constexpr int DIAL_MAX_MIN[WASHER_COUNT] = {60, 90};
constexpr int FLAG_DOWN = 0, FLAG_UP = 90;
constexpr uint32_t LOOP_MS = 200;
// The real gateway uses the course kit's DHT11 (KY-015); Wokwi only has a DHT22, so
// gateway_sim sets -DDHT_TYPE=DHT22. Both are fine with one read every 2 s.
#ifndef DHT_TYPE
#define DHT_TYPE DHT11
#endif
constexpr uint32_t DHT_PERIOD_MS = 2000;

constexpr UBaseType_t CONTROL_TASK_PRIO = 4;  // below receive, above cloud
constexpr uint32_t CONTROL_TASK_STACK = 4096;

static DHT dht(PIN_DHT, DHT_TYPE);
static Servo dial[WASHER_COUNT], flag[WASHER_COUNT];
static RainWatch rain_watch;

static int dial_angle(uint16_t minutes, int max_min) {
  return map(constrain(minutes, 0, max_min), 0, max_min, 180, 0);
}

static void control_task(void*) {
  uint32_t last_dht = millis() - DHT_PERIOD_MS;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(LOOP_MS));
    uint32_t now = millis();

    // Slow sensor reads happen outside the lock.
    bool pir = digitalRead(PIN_PIR) == HIGH;
    uint16_t rain = analogRead(PIN_RAIN);
    if (rain_watch.update(rain, now)) {
      Notice n = {0, CycleEvent::None, true};
      xQueueSend(q_notices, &n, 0);  // a full queue drops it rather than stall the servos
    }
    bool read_dht = now - last_dht >= DHT_PERIOD_MS;
    float t = NAN, h = NAN;
    if (read_dht) {
      last_dht = now;
      t = dht.readTemperature();
      h = dht.readHumidity();
    }

    int dial_deg[WASHER_COUNT], flag_deg[WASHER_COUNT];
    xSemaphoreTake(g_state_mtx, portMAX_DELAY);
    g_env.pir = pir;
    g_env.rain_raw = rain;
    if (read_dht) {
      g_env.temp_c = t;
      g_env.rh = h;
    }
    for (size_t i = 0; i < WASHER_COUNT; i++) {
      const WasherCycle& c = g_washers[i].cycle;
      dial_deg[i] = dial_angle(c.minutes_left(now), DIAL_MAX_MIN[i]);
      flag_deg[i] = c.state() == WasherState::Done ? FLAG_UP : FLAG_DOWN;
    }
    xSemaphoreGive(g_state_mtx);

    for (size_t i = 0; i < WASHER_COUNT; i++) {
      dial[i].write(dial_deg[i]);
      flag[i].write(flag_deg[i]);
    }
  }
}

void control_start() {
  pinMode(PIN_PIR, INPUT);
  analogReadResolution(12);
  dht.begin();
  for (size_t i = 0; i < WASHER_COUNT; i++) {
    dial[i].attach(PIN_DIAL[i]);
    flag[i].attach(PIN_FLAG[i]);
  }
  xTaskCreate(control_task, "control", CONTROL_TASK_STACK, nullptr, CONTROL_TASK_PRIO, nullptr);
}
