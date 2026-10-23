#pragma once
#include <stdint.h>
inline uint32_t test_millis = 0;
inline uint32_t millis() { return test_millis; }
