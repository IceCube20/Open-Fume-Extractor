#pragma once

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifndef DISPLAY_GUI_TASK_ENABLED
#define DISPLAY_GUI_TASK_ENABLED 1
#endif
#ifndef DISPLAY_GUI_TASK_CORE
#define DISPLAY_GUI_TASK_CORE 1
#endif
#ifndef DISPLAY_GUI_TASK_PRIORITY
#define DISPLAY_GUI_TASK_PRIORITY 2
#endif
#ifndef DISPLAY_GUI_TASK_STACK
#define DISPLAY_GUI_TASK_STACK (16 * 1024)
#endif

// One owner for the complete UI service, including LVGL, touch and panel flush.
// The mutex is only for explicit CLI diagnostics; transport never waits for it.
class OfeDisplayGuiTask {
 public:
  using Service = void (*)();

  bool begin(bool transport_ready, bool ui_ready, Service service) {
    if (running()) return true;
#if DISPLAY_GUI_TASK_ENABLED
    // Without the transport task, the Arduino loop still handles frames and UI.
    if (!transport_ready || !ui_ready || !service) return false;
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
      Serial.println("GUI mutex allocation failed; using Arduino loop");
      return false;
    }
    service_ = service;
    if (xTaskCreatePinnedToCore(taskEntry, "lvgl_gui", DISPLAY_GUI_TASK_STACK,
        this, DISPLAY_GUI_TASK_PRIORITY, &task_, DISPLAY_GUI_TASK_CORE) != pdPASS) {
      task_ = nullptr;
      service_ = nullptr;
      vSemaphoreDelete(mutex_);
      mutex_ = nullptr;
      Serial.println("GUI task allocation failed; using Arduino loop");
      return false;
    }
    // Publish ownership before the new task can call any UI function.
    running_.store(true, std::memory_order_release);
    xTaskNotifyGive(task_);
    Serial.printf("GUI task started: core=%u priority=%u stack=%u bytes\n",
      (unsigned)DISPLAY_GUI_TASK_CORE, (unsigned)DISPLAY_GUI_TASK_PRIORITY,
      (unsigned)DISPLAY_GUI_TASK_STACK);
    return true;
#else
    (void)transport_ready;
    (void)ui_ready;
    (void)service;
    return false;
#endif
  }

  bool running() const { return running_.load(std::memory_order_acquire); }
  bool needsDefer() const {
    return running() && xTaskGetCurrentTaskHandle() != task_;
  }
  UBaseType_t stackReserve() const {
    return running() ? uxTaskGetStackHighWaterMark(task_) : 0;
  }
  uint32_t takeMaxBusyUs() {
    return max_busy_us_.exchange(0, std::memory_order_relaxed);
  }

  class DiagnosticLock {
   public:
    explicit DiagnosticLock(OfeDisplayGuiTask& gui, bool required = true) {
      if (!required || !gui.needsDefer()) return;
      if (xSemaphoreTake(gui.mutex_, pdMS_TO_TICKS(300)) == pdTRUE) mutex_ = gui.mutex_;
      else valid_ = false;
    }
    ~DiagnosticLock() { if (mutex_) xSemaphoreGive(mutex_); }
    explicit operator bool() const { return valid_; }
    DiagnosticLock(const DiagnosticLock&) = delete;
    DiagnosticLock& operator=(const DiagnosticLock&) = delete;
   private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool valid_ = true;
  };

 private:
  static void taskEntry(void* arg) {
    auto& self = *static_cast<OfeDisplayGuiTask*>(arg);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      xSemaphoreTake(self.mutex_, portMAX_DELAY);
      const uint32_t started = micros();
      self.service_();
      const uint32_t busy = (uint32_t)(micros() - started);
      uint32_t previous = self.max_busy_us_.load(std::memory_order_relaxed);
      while (busy > previous && !self.max_busy_us_.compare_exchange_weak(
          previous, busy, std::memory_order_relaxed)) {}
      xSemaphoreGive(self.mutex_);
      // A real blocked tick lets the idle task, CLI and watchdog run as well.
      vTaskDelay(1);
    }
  }

  Service service_ = nullptr;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> max_busy_us_{0};
};
