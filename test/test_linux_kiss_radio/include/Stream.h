#pragma once

#include <cstddef>
#include <cstdint>

class Stream {
public:
  virtual ~Stream() = default;
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual void flush() {}
  virtual size_t write(const uint8_t*, size_t len) { return len; }
  virtual size_t write(uint8_t) { return 1; }
};