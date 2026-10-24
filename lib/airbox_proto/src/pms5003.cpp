#include "pms5003.h"

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

// Checks a byte against the fixed header: 42 4D 00 1C.
bool Pms5003Parser::byte_fits(uint8_t b) const {
  switch (len_) {
    case 0: return b == 0x42;
    case 1: return b == 0x4D;
    case 2: return b == 0x00;
    case 3: return b == 0x1C;
    default: return true;
  }
}

bool Pms5003Parser::feed(uint8_t b) {
  if (!byte_fits(b)) {
    // Resync: a stray 0x42 may be the start of the real frame.
    len_ = 0;
    if (b == 0x42) buf_[len_++] = b;
    return false;
  }
  buf_[len_++] = b;
  if (len_ < FRAME_LEN) return false;

  len_ = 0;
  uint16_t sum = 0;
  for (size_t i = 0; i < 30; i++) sum += buf_[i];
  if (sum != be16(&buf_[30])) {
    checksum_errors_++;
    return false;
  }
  decode();
  return true;
}

void Pms5003Parser::decode() {
  reading_.pm1 = be16(&buf_[10]);
  reading_.pm25 = be16(&buf_[12]);
  reading_.pm10 = be16(&buf_[14]);
  reading_.n03 = be16(&buf_[16]);
  reading_.n05 = be16(&buf_[18]);
  reading_.n10 = be16(&buf_[20]);
  reading_.n25 = be16(&buf_[22]);
}
