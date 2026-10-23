#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>

// GUI-owned mailbox, one immutable full-frame snapshot. Only the worker calls
// the existing blocking transport; it never touches LVGL or the render buffer.
class OfeQspiFrameWorker {
 public:
  static constexpr uint32_t frameBytes = 320U * 480U * 2U;
  static constexpr uint32_t stackBytes = 4096U;
  static constexpr uint32_t internalReserve = 96U * 1024U;
  using Send = bool (*)(const uint8_t*, uint32_t, esp_err_t*);
  struct Result {
    bool success = false;
    esp_err_t error = ESP_OK;
    uint32_t transferUs = 0;
    uint32_t snapshotUs = 0;
  };

  bool begin(Send send) {
    if (task_) return true;
    if (!send || heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        internalReserve + stackBytes + 2048U) return false;
    snapshot_ = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      64, frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (!snapshot_) return false;
    done_ = xSemaphoreCreateBinary();
    if (!done_) { releaseStartup(); return false; }
    send_ = send;
    // Lower priority than OFE bus/Wi-Fi tasks. DMA waits block, not spin.
    if (xTaskCreatePinnedToCore(taskEntry, "qspi_frame", stackBytes, this, 1,
        &task_, 0) != pdPASS) {
      task_ = nullptr;
      releaseStartup();
      return false;
    }
    return true;
  }

  bool running() const { return task_ != nullptr; }
  bool busy() const { return busy_; } // GUI/diagnostic owner only
  UBaseType_t stackReserve() const { return task_ ? uxTaskGetStackHighWaterMark(task_) : 0; }

  bool submit(const void* frame, uint32_t bytes) {
    if (!task_ || busy_ || !frame || bytes != frameBytes) return false;
    const uint32_t started = micros();
    std::memcpy(snapshot_, frame, frameBytes);
    snapshotUs_ = uint32_t(micros() - started);
    busy_ = true;
    xTaskNotifyGive(task_); // Publication barrier for snapshot and timing.
    return true;
  }

  bool collect(bool wait, Result& result) {
    if (!busy_ || xSemaphoreTake(done_, wait ? portMAX_DELAY : 0) != pdTRUE) return false;
    // The completion semaphore publishes result and returns snapshot ownership.
    result = result_;
    busy_ = false;
    return true;
  }

 private:
  void releaseStartup() {
    if (done_) vSemaphoreDelete(done_);
    done_ = nullptr;
    heap_caps_free(snapshot_);
    snapshot_ = nullptr;
    send_ = nullptr;
  }
  static void taskEntry(void* arg) {
    auto& self = *static_cast<OfeQspiFrameWorker*>(arg);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      Result result;
      result.snapshotUs = self.snapshotUs_;
      const uint32_t started = micros();
      result.success = self.send_(self.snapshot_, frameBytes, &result.error);
      result.transferUs = uint32_t(micros() - started);
      self.result_ = result;
      xSemaphoreGive(self.done_);
    }
  }

  Send send_ = nullptr;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t done_ = nullptr;
  uint8_t* snapshot_ = nullptr;
  uint32_t snapshotUs_ = 0;
  Result result_;
  bool busy_ = false;
};
