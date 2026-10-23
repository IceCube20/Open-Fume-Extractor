#pragma once
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
struct FakeTask;
struct FakeSemaphore;
using TaskHandle_t = FakeTask*;
using SemaphoreHandle_t = FakeSemaphore*;
constexpr BaseType_t pdTRUE = 1, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
