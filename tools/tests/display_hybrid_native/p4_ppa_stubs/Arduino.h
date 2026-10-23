#pragma once
#include <stdint.h>
extern uint32_t now_us;
inline uint32_t micros() { return now_us; }
