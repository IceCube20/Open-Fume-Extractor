#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cassert>
using BaseType_t = int;
using TickType_t = uint32_t;
static constexpr int pdTRUE = 1, pdFALSE = 0, pdPASS = 1;
static constexpr TickType_t portMAX_DELAY = UINT32_MAX;
struct StaticSemaphore_t {
  std::mutex mutex;
  std::condition_variable ready;
  unsigned count = 0;
  bool binary = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
struct FakeTask {
  std::thread thread;
  std::mutex mutex;
  std::condition_variable ready;
  unsigned notifications = 0;
  bool stopping = false;
  int core = -1;
};
using TaskHandle_t = FakeTask*;
inline thread_local TaskHandle_t current_task = nullptr;
inline std::vector<std::unique_ptr<FakeTask>> fake_tasks;
inline std::atomic<unsigned> worker_yields{0}, interrupted_binary_takes{0};
inline bool fail_task_create = false, fail_semaphore_create = false;
struct StopTask {};
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) {
  if (fail_semaphore_create) return nullptr;
  storage->count = 1; storage->binary = false; return storage;
}
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) {
  if (fail_semaphore_create) return nullptr;
  storage->count = 0; storage->binary = true; return storage;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
  assert(semaphore && timeout == portMAX_DELAY);
  std::unique_lock lock(semaphore->mutex);
  if (semaphore->binary && interrupted_binary_takes.exchange(0)) return pdFALSE;
  if (!semaphore->ready.wait_for(lock, std::chrono::seconds(5), [&] { return semaphore->count; })) {
    assert(false && "handoff deadlocked"); return pdFALSE;
  }
  --semaphore->count; return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  std::lock_guard lock(semaphore->mutex);
  assert(semaphore->count == 0);
  ++semaphore->count; semaphore->ready.notify_one(); return pdTRUE;
}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t stack,
    void* context, unsigned priority, TaskHandle_t* result, int core) {
  assert(core == 0 && stack == 8192 && priority == 1);
  if (fail_task_create) return pdFALSE;
  auto task = std::make_unique<FakeTask>();
  task->core = core; *result = task.get();
  task->thread = std::thread([entry, context, task = task.get()] {
    current_task = task;
    try { entry(context); } catch (const StopTask&) {}
  });
  fake_tasks.push_back(std::move(task));
  return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard lock(task->mutex);
  ++task->notifications; task->ready.notify_one();
}
inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) {
  assert(current_task && clear == pdTRUE && timeout == portMAX_DELAY);
  std::unique_lock lock(current_task->mutex);
  current_task->ready.wait(lock, [] { return current_task->notifications || current_task->stopping; });
  if (current_task->stopping) throw StopTask{};
  const auto count = current_task->notifications; current_task->notifications = 0; return count;
}
inline void vTaskDelay(TickType_t ticks) {
  assert(ticks == 1);
  if (current_task) ++worker_yields;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
inline void stop_fake_tasks() {
  for (auto& task : fake_tasks) {
    std::lock_guard lock(task->mutex);
    task->stopping = true; task->ready.notify_one();
  }
  for (auto& task : fake_tasks) task->thread.join();
  fake_tasks.clear();
}
