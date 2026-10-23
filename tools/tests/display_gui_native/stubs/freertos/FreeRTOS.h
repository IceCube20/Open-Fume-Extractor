#pragma once
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
constexpr BaseType_t pdTRUE = 1, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
