#include "record_buffer.h"

void RecordBuffer::push(const LogRecord& r) {
  if (count_ == cap_) {
    head_ = (head_ + 1) % cap_;  // overwrite the oldest
    count_--;
    dropped_++;
  }
  buf_[(head_ + count_) % cap_] = r;
  count_++;
  pushed_++;
}

void RecordBuffer::pop_through(uint32_t end_seq) {
  int32_t n = (int32_t)(end_seq - oldest_seq());  // <= 0: those records are already gone
  if (n > 0) pop((size_t)n);
}

size_t RecordBuffer::peek(LogRecord* out, size_t max) const {
  size_t n = max < count_ ? max : count_;
  for (size_t i = 0; i < n; i++) out[i] = buf_[(head_ + i) % cap_];
  return n;
}

void RecordBuffer::pop(size_t n) {
  if (n > count_) n = count_;
  head_ = (head_ + n) % cap_;
  count_ -= n;
}
