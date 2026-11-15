#pragma once
#include <cstdint>
inline uint32_t test_millis = 0;
inline uint32_t millis() { return test_millis; }
inline uint32_t test_us = 0;
inline uint32_t micros() { return test_millis * 1000 + test_us; }
inline void delayMicroseconds(unsigned us) { test_us += us; }
