#include "imu.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

constexpr int PIN_SDA = 21, PIN_SCL = 22;
constexpr uint8_t MPU_ADDR = 0x68;  // AD0 low
constexpr uint8_t REG_CONFIG = 0x1A, REG_ACCEL_CONFIG = 0x1C, REG_ACCEL_XOUT_H = 0x3B, REG_PWR_MGMT_1 = 0x6B;
constexpr float LSB_PER_G = 8192.0f;  // +-4 g range: spin can pass 2 g

static bool write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool imu_begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  return write_reg(REG_PWR_MGMT_1, 0x00)       // wake up, internal clock
         && write_reg(REG_CONFIG, 0x03)        // DLPF 44 Hz: below Nyquist for 100 Hz sampling
         && write_reg(REG_ACCEL_CONFIG, 0x08);  // +-4 g
}

bool imu_read_magnitude(float& mag_g) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  float a[3];
  for (int i = 0; i < 3; i++) a[i] = (int16_t)((Wire.read() << 8) | Wire.read()) / LSB_PER_G;
  mag_g = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  return true;
}
