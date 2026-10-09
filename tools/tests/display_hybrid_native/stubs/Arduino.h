#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
class Stream {
public:
  virtual ~Stream()=default;
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t* data, size_t length) {
    size_t written = 0;
    while (written < length && write(data[written])) ++written;
    return written;
  }
  virtual int available()=0;
  virtual int read()=0;
  virtual void flush()=0;
};
