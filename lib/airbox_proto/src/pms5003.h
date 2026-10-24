// Byte-stream parser for Plantower PMS5003 32-byte frames.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct PmsReading {
  uint16_t pm1, pm25, pm10;     // ug/m3, atmospheric values
  uint16_t n03, n05, n10, n25;  // particles per 0.1 L
};

// Passive mode: the sensor only sends a frame when asked with PMS_CMD_READ.
constexpr uint8_t PMS_CMD_PASSIVE[7] = {0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70};
constexpr uint8_t PMS_CMD_READ[7] = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};

class Pms5003Parser {
 public:
  // Returns true only on the byte that completes a frame with a valid checksum.
  bool feed(uint8_t b);
  // Drops any partly received frame (call before each passive-mode read).
  void reset() { len_ = 0; }
  const PmsReading& reading() const { return reading_; }
  uint32_t checksum_errors() const { return checksum_errors_; }

 private:
  static constexpr size_t FRAME_LEN = 32;
  uint8_t buf_[FRAME_LEN] = {};
  size_t len_ = 0;
  PmsReading reading_ = {};
  uint32_t checksum_errors_ = 0;

  bool byte_fits(uint8_t b) const;
  void decode();
};
