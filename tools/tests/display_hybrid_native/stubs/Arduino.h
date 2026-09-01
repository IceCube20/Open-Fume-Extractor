#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
class Stream {
public:
  virtual ~Stream()=default;
  virtual size_t write(uint8_t)=0;
  virtual int available()=0;
  virtual int read()=0;
  virtual void flush()=0;
};
