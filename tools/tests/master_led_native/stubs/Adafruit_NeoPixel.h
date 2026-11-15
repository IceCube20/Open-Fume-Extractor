#pragma once
#include <cstdint>
#include <cstring>
#include <Arduino.h>
constexpr int NEO_GRBW = 1, NEO_KHZ800 = 2;
inline unsigned base_shows = 0;
class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(uint16_t count, int16_t pin, int) : numBytes(count * 4), pin_(pin) {}
  void begin() {}
  void setBrightness(uint8_t) {}
  void clear() { std::memset(pixels, 0, sizeof(pixels)); }
  void show() { ++base_shows; }
  bool canShow() { return uint32_t(micros() - endTime) >= 300; }
  int16_t getPin() const { return pin_; }
  uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) { return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
  void setPixelColor(uint8_t index, uint32_t color) {
    auto* p = pixels + index * 4;
    p[0] = uint8_t(color >> 8); p[1] = uint8_t(color >> 16); p[2] = uint8_t(color); p[3] = uint8_t(color >> 24);
  }
protected:
  uint16_t numBytes;
  uint32_t endTime = 0;
  uint8_t pixels[8]{};
private:
  int16_t pin_;
};
