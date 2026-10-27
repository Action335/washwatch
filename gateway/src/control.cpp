#include "control.h"

#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>

#include "rain_watch.h"
#include "state.h"

// Pins as tested in Wokwi (wokwi/gateway/diagram.json).
constexpr int PIN_PIR = 27;
constexpr int PIN_DHT = 4;
constexpr int PIN_RAIN = 34;  // ADC1 only: ADC2 pins stop working when Wi-Fi is on
constexpr int PIN_DIAL[WASHER_COUNT] = {13, 26};
constexpr int PIN_FLAG[WASHER_COUNT] = {14, 32};

constexpr int DIAL_MAX_MIN = 60;  // dial face runs 0-60 minutes
constexpr int FLAG_DOWN = 0, FLAG_UP = 90;
constexpr uint32_t LOOP_MS = 200;
constexpr uint32_t DHT_PERIOD_MS = 2000;  // DHT22 allows one read per 2 s

constexpr UBaseType_t CONTROL_TASK_PRIO = 4;  // below receive, above cloud
constexpr uint32_t CONTROL_TASK_STACK = 4096;

static DHT dht(PIN_DHT, DHT22);
static Servo dial[WASHER_COUNT], flag[WASHER_COUNT];
static RainWatch rain_watch;

static int dial_angle(uint16_t minutes) {
  return map(constrain(minutes, 0, DIAL_MAX_MIN), 0, DIAL_MAX_MIN, 180, 0);
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
      dial_deg[i] = dial_angle(c.minutes_left(now));
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
