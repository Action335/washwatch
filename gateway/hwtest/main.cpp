// Gateway hardware test (env `hwtest`): same pins as control.cpp, no Wi-Fi or ESP-NOW.
// At boot (and whenever 's' arrives on serial) each servo sweeps 0 -> 90 -> 180 -> 90, one at a time.
// Then every 500 ms it prints: pir, rain raw (wet < 2500), DHT temperature and humidity.

#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>

constexpr int PIN_PIR = 27, PIN_DHT = 4, PIN_RAIN = 34;
constexpr int PIN_SERVO[4] = {13, 14, 26, 32};
const char* SERVO_NAME[4] = {"washer1 dial (D13)", "washer1 flag (D14)", "washer2 dial (D26)", "washer2 flag (D32)"};

static DHT dht(PIN_DHT, DHT11);
static Servo servo[4];

static void sweep() {
  for (int i = 0; i < 4; i++) {
    Serial.printf("# servo: %s\n", SERVO_NAME[i]);
    for (int deg : {0, 90, 180, 90}) {
      servo[i].write(deg);
      delay(600);
    }
  }
  Serial.println("# servo sweep done");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_PIR, INPUT);
  analogReadResolution(12);
  dht.begin();
  for (int i = 0; i < 4; i++) servo[i].attach(PIN_SERVO[i]);
  Serial.println("# WashWatch gateway hwtest");
  sweep();
  Serial.println("pir,rain_raw,temp_c,rh");
}

void loop() {
  if (Serial.available() && Serial.read() == 's') sweep();
  float t = dht.readTemperature(), h = dht.readHumidity();
  Serial.printf("%d,%u,%s,%s\n", digitalRead(PIN_PIR), analogRead(PIN_RAIN),
                isnan(t) ? "none" : String(t, 1).c_str(), isnan(h) ? "none" : String(h, 0).c_str());
  delay(500);
}
