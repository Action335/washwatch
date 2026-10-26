// Fixed-size ring of LogRecords, oldest first. When full, a push overwrites the oldest record
// (the newest data matters most) and counts it as dropped. No heap: the caller owns the array.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "log_record.h"

class RecordBuffer {
 public:
  RecordBuffer(LogRecord* storage, size_t capacity) : buf_(storage), cap_(capacity) {}
  void push(const LogRecord& r);
  size_t size() const { return count_; }
  // Copies up to max records, oldest first, without removing them.
  size_t peek(LogRecord* out, size_t max) const;
  // Removes up to n of the oldest records.
  void pop(size_t n);
  uint32_t dropped() const { return dropped_; }

 private:
  LogRecord* buf_;
  size_t cap_;
  size_t head_ = 0;  // index of the oldest record
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};
