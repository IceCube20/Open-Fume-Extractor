#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
constexpr int SPI_DEFAULT_FREQ = 40000000, GFX_NOT_DEFINED = -1, SPI_MODE3 = 3;
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
void pinMode(int, int);
void digitalWrite(int, int);
extern unsigned fallbackBegins;
class Arduino_DataBus {
 public:
  virtual bool begin(int32_t = SPI_DEFAULT_FREQ, int8_t = GFX_NOT_DEFINED) = 0;
  virtual void beginWrite() = 0;
  virtual void endWrite() = 0;
  virtual void writeCommand(uint8_t) = 0;
  virtual void writeCommand16(uint16_t) = 0;
  virtual void writeCommandBytes(uint8_t*, uint32_t) = 0;
  virtual void write(uint8_t) = 0;
  virtual void write16(uint16_t) = 0;
  virtual void writeRepeat(uint16_t, uint32_t) = 0;
  virtual void writeBytes(uint8_t*, uint32_t) = 0;
  virtual void writePixels(uint16_t*, uint32_t) = 0;
};
class Arduino_ESP32SPI : public Arduino_DataBus {
 public:
  Arduino_ESP32SPI(int, int, int, int, int) {}
  bool begin(int32_t, int8_t) override { ++fallbackBegins; return true; }
  void beginWrite() override {}
  void endWrite() override {}
  void writeCommand(uint8_t) override {}
  void writeCommand16(uint16_t) override {}
  void writeCommandBytes(uint8_t*, uint32_t) override {}
  void write(uint8_t) override {}
  void write16(uint16_t) override {}
  void writeRepeat(uint16_t, uint32_t) override {}
  void writeBytes(uint8_t*, uint32_t) override {}
  void writePixels(uint16_t*, uint32_t) override {}
};
