#pragma once
#include <cstdint>
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
using TaskHandle_t = void*;
