#pragma once
#include <cstdint>
#include <cstddef>
constexpr int HIGH = 1, LOW = 0;
uint32_t micros();
uint32_t millis();
void digitalWrite(int, int);
#define IRAM_ATTR
