#pragma once
#include "FreeRTOS.h"
#include <Arduino.h>
#include <cassert>
inline void (*worker)(void*) = nullptr;
inline void* worker_context = nullptr;
inline bool create_ok = true;
inline unsigned task_cycles = 0;
struct StopTask {};
inline int xTaskCreatePinnedToCore(void (*fn)(void*), const char*, unsigned stack, void* context, unsigned priority, TaskHandle_t* handle, int core) {
  assert(stack == 4096 && priority == 2 && core == 1);
  if (!create_ok) return 0;
  worker = fn; worker_context = context; *handle = context; return pdPASS;
}
inline void vTaskDelay(unsigned ms) {
  assert(ms == 20); test_millis += ms;
  if (++task_cycles == 4) throw StopTask{};
}
