#include "sensors.h"

#include <Arduino.h>
#include <math.h>

#if HAS_PMS
#include "pms5003.h"
#endif
#if HAS_DHT
#include <DHT.h>
#endif
#if HAS_LUX
#include <BH1750.h>
#include <Wire.h>
#endif

// Pins (AIRBOX_DESIGN.md §4). MQ-135 must be on an ADC1 pin: ADC2 stops working with Wi-Fi on.
constexpr int PIN_PMS_RX = 16, PIN_PMS_TX = 17;
constexpr int PIN_DHT = 4;
constexpr int PIN_MQ = 34;
constexpr int PIN_SDA = 21, PIN_SCL = 22;

constexpr uint32_t MQ_WARMUP_MS = 180000;  // heater settling after power-on
constexpr uint32_t PMS_READ_TIMEOUT_MS = 200;

#if HAS_PMS
static Pms5003Parser pms;
#endif
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
#if HAS_PMS
  Serial2.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
  Serial2.write(PMS_CMD_PASSIVE, sizeof(PMS_CMD_PASSIVE));
#endif
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

#if HAS_PMS
static void read_pms(node_msg_t& m) {
  while (Serial2.available()) Serial2.read();  // drop stale bytes
  Serial2.write(PMS_CMD_READ, sizeof(PMS_CMD_READ));
  uint32_t t0 = millis();
  while (millis() - t0 < PMS_READ_TIMEOUT_MS) {
    if (Serial2.available() && pms.feed(Serial2.read())) {
      const PmsReading& r = pms.reading();
      m.pm1 = r.pm1;
      m.pm25 = r.pm25;
      m.pm10 = r.pm10;
      m.n03 = r.n03;
      m.n05 = r.n05;
      m.n10 = r.n10;
      m.n25 = r.n25;
      return;
    }
  }
  m.flags |= FLAG_PMS_FAULT;
}
#endif

#if SIMULATED
// Synthetic readings so the gateway and dashboard can show a third node (Scalability 2 pts).
static void read_simulated(node_msg_t& m) {
  float phase = (millis() % 600000) / 600000.0f * 2 * PI;  // 10-minute cycle
  m.pm25 = (uint16_t)(40 + 20 * sinf(phase));
  m.pm1 = m.pm25 * 7 / 10;
  m.pm10 = m.pm25 * 13 / 10;
  m.temp_c10 = 300;
  m.rh_10 = 650;
}
#endif

void sensors_read(node_msg_t& m) {
  clear_fields(m);
#if SIMULATED
  read_simulated(m);
#endif
#if HAS_PMS
  read_pms(m);
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
