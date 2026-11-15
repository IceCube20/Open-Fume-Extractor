#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Synchronous handoff, not a deferred publish queue: the caller retains its
// client/configuration lock and the topic/payload until publish() returns.
class MqttPublishWorker {
 public:
  using Publish = bool (*)(const char*, const char*, bool);
  explicit MqttPublishWorker(Publish publish) : publish_(publish) {}
  MqttPublishWorker(const MqttPublishWorker&) = delete;
  MqttPublishWorker& operator=(const MqttPublishWorker&) = delete;

  bool start() {
    if (task_) return true;
    request_mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    completed_ = xSemaphoreCreateBinaryStatic(&completed_storage_);
    if (!request_mutex_ || !completed_) return false;
    return xTaskCreatePinnedToCore(taskEntry, "mqtt-send", 8192, this, 1, &task_, 0) == pdPASS;
  }

  TaskHandle_t taskHandle() const { return task_; }

  bool publish(const char* topic, const char* payload, bool retained) {
    if (!task_ || xTaskGetCurrentTaskHandle() == task_) return publish_(topic, payload, retained);
    if (xSemaphoreTake(request_mutex_, portMAX_DELAY) != pdTRUE) return false;
    request_.topic = topic;
    request_.payload = payload;
    request_.retained = retained;
    request_.success = false;
    xTaskNotifyGive(task_);
    // Do not time out and release borrowed strings while the worker uses them.
    while (xSemaphoreTake(completed_, portMAX_DELAY) != pdTRUE) vTaskDelay(1);
    const bool success = request_.success;
    xSemaphoreGive(request_mutex_);
    return success;
  }

 private:
  struct Request {
    const char* topic = nullptr;
    const char* payload = nullptr;
    bool retained = false;
    bool success = false;
  } request_;
  Publish publish_;
  TaskHandle_t task_ = nullptr;
  StaticSemaphore_t mutex_storage_, completed_storage_;
  SemaphoreHandle_t request_mutex_ = nullptr, completed_ = nullptr;

  static void taskEntry(void* context) {
    auto& self = *static_cast<MqttPublishWorker*>(context);
    for (;;) {
      if (!ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) continue;
      self.request_.success = self.publish_(self.request_.topic, self.request_.payload, self.request_.retained);
      xSemaphoreGive(self.completed_);
      // Even discovery bursts must let IDLE0 run between individual messages.
      vTaskDelay(1);
    }
  }
};
