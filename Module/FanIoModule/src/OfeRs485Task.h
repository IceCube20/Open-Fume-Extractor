#pragma once

// Dedicated MCU-facing RS485 worker for dual-core ESP32 boards.
// This header is copied into each stand-alone Arduino sketch directory.
// It does not change the OFE on-wire protocol or the existing Link parser.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifndef OFE_RS485_DEDICATED_TASK
#define OFE_RS485_DEDICATED_TASK 1
#endif
#ifndef OFE_RS485_TASK_STACK
// IDF's xTaskCreatePinnedToCore expects a stack size in BYTES.
#define OFE_RS485_TASK_STACK 8192
#endif
#ifndef OFE_RS485_TASK_PRIORITY
#define OFE_RS485_TASK_PRIORITY 2
#endif
#ifndef OFE_RS485_TASK_CORE
// -1: automatically choose the CPU other than Arduino setup()/loop().
#define OFE_RS485_TASK_CORE -1
#endif

class OfeRs485Task {
public:
  using Callback = void (*)();

  // Recursive because command handlers can call helpers that may also need
  // the shared-state guard. Lock state, not the UART parser, on each frame.
  class StateGuard {
  public:
    explicit StateGuard(OfeRs485Task& owner) : owner_(owner) { owner_.lockState(); }
    ~StateGuard() { owner_.unlockState(); }
    StateGuard(const StateGuard&) = delete;
    StateGuard& operator=(const StateGuard&) = delete;
  private:
    OfeRs485Task& owner_;
  };

  bool start(Callback poll, Callback housekeeping) {
#if OFE_RS485_DEDICATED_TASK
#if (defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE) || \
    (defined(portNUM_PROCESSORS) && portNUM_PROCESSORS < 2) || \
    (defined(configNUM_CORES) && configNUM_CORES < 2)
    (void)poll;
    (void)housekeeping;
    return false;
#else
    if (task_) return true;
    if (!poll || !housekeeping) return false;
    state_mutex_ = xSemaphoreCreateRecursiveMutex();
    if (!state_mutex_) return false;
    poll_ = poll;
    housekeeping_ = housekeeping;
    const BaseType_t setup_core = xPortGetCoreID();
    const BaseType_t worker_core = OFE_RS485_TASK_CORE < 0
      ? (setup_core == 0 ? 1 : 0) : OFE_RS485_TASK_CORE;
    if (worker_core < 0 || worker_core > 1 || worker_core == setup_core) {
      vSemaphoreDelete(state_mutex_);
      state_mutex_ = nullptr;
      return false;
    }
    const BaseType_t ok = xTaskCreatePinnedToCore(
      taskEntry, "ofeRs485", OFE_RS485_TASK_STACK, this,
      OFE_RS485_TASK_PRIORITY, &task_, worker_core);
    if (ok == pdPASS && task_) return true;
    task_ = nullptr;
    vSemaphoreDelete(state_mutex_);
    state_mutex_ = nullptr;
    return false;
#endif
#else
    (void)poll;
    (void)housekeeping;
    return false;
#endif
  }

  bool active() const { return task_ != nullptr; }

private:
  static void taskEntry(void* arg) {
    OfeRs485Task* const worker = static_cast<OfeRs485Task*>(arg);
    for (;;) {
      // The RS485 Link and all bus.send() calls have ONE owner: this task.
      // Housekeeping includes discovery, join and FW timeout processing.
      worker->poll_();
      worker->housekeeping_();
      // One tick: quick MCU response without busy-spinning or starving USB.
      vTaskDelay(1);
    }
  }

  void lockState() {
    if (state_mutex_) xSemaphoreTakeRecursive(state_mutex_, portMAX_DELAY);
  }
  void unlockState() {
    if (state_mutex_) xSemaphoreGiveRecursive(state_mutex_);
  }

  Callback poll_ = nullptr;
  Callback housekeeping_ = nullptr;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t state_mutex_ = nullptr;
};
