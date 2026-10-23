#include <cassert>
#include <cstdio>
#include <cstring>
#include "OfeDisplayGuiTask.h"

namespace {
int loop_id, gui_id, mutex_id;
TaskHandle_t current = &loop_id;
TaskFunction_t entry = nullptr;
void* task_arg = nullptr;
bool fail_mutex = false, fail_task = false, fail_take = false, locked = false;
unsigned creates = 0, deletes = 0, gives = 0, notifications = 0, services = 0;
uint32_t now_us = 0;
OfeDisplayGuiTask* active = nullptr;
struct StopAfterOneCycle {};
void service() {
  assert(active && !active->needsDefer());
  assert(locked);
  ++services;
  now_us += 200;
}
}

uint32_t micros() { return now_us; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return current; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  assert(task == &gui_id);
  return 4096;
}
SemaphoreHandle_t xSemaphoreCreateMutex() { return fail_mutex ? nullptr : &mutex_id; }
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(mutex == &mutex_id); ++deletes; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait) {
  assert(mutex == &mutex_id && !locked);
  assert(wait == 300 || wait == portMAX_DELAY);
  if (fail_take) return 0;
  locked = true;
  return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) {
  assert(mutex == &mutex_id && locked);
  locked = false;
  ++gives;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t function, const char* name,
    uint32_t stack, void* arg, UBaseType_t priority, TaskHandle_t* handle, BaseType_t core) {
  ++creates;
  assert(!strcmp(name, "lvgl_gui") && stack == 16384 && priority == 2 && core == 1);
  if (fail_task) return 0;
  entry = function;
  task_arg = arg;
  *handle = &gui_id;
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {
  assert(task == &gui_id && active && active->running() && active->needsDefer());
  ++notifications;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
  assert(clear == pdTRUE && wait == portMAX_DELAY && notifications);
  return 1;
}
void vTaskDelay(TickType_t ticks) {
  assert(ticks == 1 && !locked);
  throw StopAfterOneCycle{};
}

int main() {
  OfeDisplayGuiTask gui;
  active = &gui;
  assert(!gui.running() && !gui.needsDefer() && gui.stackReserve() == 0);
  assert(!gui.begin(false, true, service));
  assert(!gui.begin(true, false, service));
  assert(!gui.begin(true, true, nullptr));
  assert(creates == 0);
  { OfeDisplayGuiTask::DiagnosticLock lock(gui); assert(lock); }
  assert(gives == 0);

#if DISPLAY_GUI_TASK_ENABLED
  fail_mutex = true;
  assert(!gui.begin(true, true, service) && creates == 0);
  fail_mutex = false;
  fail_task = true;
  assert(!gui.begin(true, true, service) && !gui.running() && deletes == 1);
  fail_task = false;
  assert(gui.begin(true, true, service) && gui.running() && notifications == 1);
  assert(gui.begin(true, true, service) && creates == 2 && notifications == 1);
  assert(gui.needsDefer() && gui.stackReserve() == 4096);

  { OfeDisplayGuiTask::DiagnosticLock lock(gui); assert(lock && locked); }
  assert(!locked && gives == 1);
  fail_take = true;
  { OfeDisplayGuiTask::DiagnosticLock lock(gui); assert(!lock && !locked); }
  { OfeDisplayGuiTask::DiagnosticLock lock(gui, false); assert(lock && !locked); }
  assert(gives == 1);
  fail_take = false;

  current = &gui_id;
  { OfeDisplayGuiTask::DiagnosticLock lock(gui); assert(lock && !locked); }
  assert(!gui.needsDefer() && gives == 1);
  now_us = UINT32_MAX - 50; // Timing remains valid across the micros() wrap.
  try { entry(task_arg); assert(false); } catch (const StopAfterOneCycle&) {}
  assert(services == 1 && !locked && gives == 2);
  assert(gui.takeMaxBusyUs() == 200 && gui.takeMaxBusyUs() == 0);
#else
  assert(!gui.begin(true, true, service) && creates == 0 && !gui.running());
#endif
  puts("PASS: GUI ownership, allocation fallback, diagnostics, timing and yield");
}
