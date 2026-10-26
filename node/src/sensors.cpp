#include "sensors.h"

#include <Arduino.h>
#include <math.h>

#if HAS_DHT
#include <DHT.h>
#endif
#if HAS_LUX
#include <BH1750.h>
#include <Wire.h>
#endif

// Pins. MQ-135 must be on an ADC1 pin: ADC2 stops working with Wi-Fi on.
constexpr int PIN_DHT = 4;
constexpr int PIN_MQ = 34;
constexpr int PIN_SDA = 21, PIN_SCL = 22;

constexpr uint32_t MQ_WARMUP_MS = 180000;  // heater settling after power-on

#if HAS_DHT
static DHT dht(PIN_DHT, DHT22);
#endif
#if HAS_LUX
static BH1750 lux_meter;
static bool lux_ok = false;
#endif

static void clear_fields(node_msg_t& m) {
  m.pm1 = m.pm25 = m.pm10 = NOT_FITTED;
  m.n03 = m.n05 = m.n10 = m.n25 = NOT_FITTED;
  m.temp_c10 = TEMP_NOT_FITTED;
  m.rh_10 = NOT_FITTED;
  m.mq135_raw = NOT_FITTED;
  m.lux = NOT_FITTED;
  m.flags = 0;
}

void sensors_begin() {
#if HAS_DHT
  dht.begin();
#endif
#if HAS_MQ
  analogReadResolution(12);
#endif
#if HAS_LUX
  Wire.begin(PIN_SDA, PIN_SCL);
  lux_ok = lux_meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
#endif
}

#if SIMULATED
// Synthetic readings so the gateway and dashboard can show a third node (Scalability 2 pts).
static void read_simulated(node_msg_t& m) {
  // 10-minute cycle, shifted per node ID so each simulated node shows different values.
  float phase = (millis() % 600000) / 600000.0f * 2 * PI + m.node_id;
  m.pm25 = (uint16_t)(40 + 20 * sinf(phase));
  m.pm1 = m.pm25 * 7 / 10;
  m.pm10 = m.pm25 * 13 / 10;
  m.temp_c10 = 300 + 10 * m.node_id;
  m.rh_10 = 650;
}
#endif

void sensors_read(node_msg_t& m) {
  clear_fields(m);
#if SIMULATED
  read_simulated(m);
#endif
#if HAS_DHT
  float t = dht.readTemperature(), h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    m.flags |= FLAG_DHT_FAULT;
  } else {
    m.temp_c10 = (int16_t)lroundf(t * 10);
    m.rh_10 = (uint16_t)lroundf(h * 10);
  }
#endif
#if HAS_MQ
  m.mq135_raw = analogRead(PIN_MQ);
  if (millis() < MQ_WARMUP_MS) m.flags |= FLAG_MQ_WARMUP;
#endif
#if HAS_LUX
  float lx = lux_ok ? lux_meter.readLightLevel() : -1;
  if (lx < 0) {
    m.flags |= FLAG_LUX_FAULT;
  } else {
    m.lux = (uint16_t)min(lx, 65000.0f);
  }
#endif
}
