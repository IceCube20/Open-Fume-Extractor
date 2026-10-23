#pragma once
#include <cstdint>
uint32_t micros();
struct FakeSerial {
  void println(const char*) {}
  template<class... Args> void printf(const char*, Args...) {}
};
inline FakeSerial Serial;
