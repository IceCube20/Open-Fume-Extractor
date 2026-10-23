#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <malloc.h>
#include "OfeQspiFrameWorker.h"

struct FakeTask {
  std::mutex mutex;
  std::condition_variable cv;
  bool notified = false, stop = false;
  std::thread thread;
};
struct FakeSemaphore { std::mutex mutex; std::condition_variable cv; bool given = false; };
static thread_local FakeTask* currentTask = nullptr;
static std::vector<FakeTask*> tasks;
static std::vector<FakeSemaphore*> semaphores;
static std::map<void*, size_t> allocations;
static size_t freeInternal = 150000;
static bool failAllocation = false, failSemaphore = false, failTask = false;
static std::atomic<uint64_t> clockUs{UINT32_MAX - 5000ULL};
uint32_t micros() { return uint32_t(clockUs.load()); }
uint32_t millis() { return uint32_t(clockUs.load() / 1000); }
static std::atomic<bool> acquired{false};
static int csLevel = HIGH;
void digitalWrite(int pin, int level) { assert(pin == 45 && acquired.load()); csLevel = level; }
void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t caps) {
  assert(alignment == 64 && bytes == 307200);
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (failAllocation) return nullptr;
  void* p = _aligned_malloc(bytes, alignment);
  assert(p);
  allocations[p] = bytes;
  return p;
}
void heap_caps_free(void* p) {
  if (!p) return;
  assert(allocations.erase(p) == 1);
  _aligned_free(p);
}
size_t heap_caps_get_free_size(uint32_t) { return freeInternal; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char*, uint32_t stack, void* arg,
    UBaseType_t priority, TaskHandle_t* handle, BaseType_t core) {
  assert(stack == 4096 && priority == 1 && core == 0);
  if (failTask) return 0;
  auto* task = new FakeTask;
  tasks.push_back(task);
  *handle = task;
  task->thread = std::thread([=] {
    currentTask = task;
    try { entry(arg); } catch (int) {}
  });
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard<std::mutex> lock(task->mutex);
  task->notified = true;
  task->cv.notify_all();
}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t timeout) {
  assert(currentTask && timeout == portMAX_DELAY);
  std::unique_lock<std::mutex> lock(currentTask->mutex);
  currentTask->cv.wait(lock, [] { return currentTask->notified || currentTask->stop; });
  if (currentTask->stop) throw 1;
  currentTask->notified = false;
  return 1;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task); return 2800; }
SemaphoreHandle_t xSemaphoreCreateBinary() {
  if (failSemaphore) return nullptr;
  auto* sem = new FakeSemaphore;
  semaphores.push_back(sem);
  return sem;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
  std::unique_lock<std::mutex> lock(sem->mutex);
  if (timeout == portMAX_DELAY) sem->cv.wait(lock, [&] { return sem->given; });
  if (!sem->given) return 0;
  sem->given = false;
  return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
  std::lock_guard<std::mutex> lock(sem->mutex);
  sem->given = true;
  sem->cv.notify_all();
  return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem) {
  semaphores.erase(std::find(semaphores.begin(), semaphores.end(), sem));
  delete sem;
}

// Minimal SDK/GFX surface for compiling the actual installed-bus subclass.
#define ARDUINO_ESP32_MAJOR 3
#define ESP_ARDUINO_VERSION_MAJOR 3
constexpr int GFX_NOT_DEFINED = -1, ESP32QSPI_FREQUENCY = 60000000, ESP32QSPI_SPI_MODE = 0;
constexpr int ESP32QSPI_SPI_HOST = 1, SPI_CLK_SRC_DEFAULT = 0, SPI_DEVICE_HALFDUPLEX = 1;
constexpr uint32_t SPI_TRANS_MODE_QIO = 1, SPI_TRANS_VARIABLE_CMD = 2;
constexpr uint32_t SPI_TRANS_VARIABLE_ADDR = 4, SPI_TRANS_VARIABLE_DUMMY = 8;
using spi_device_handle_t = void*;
struct spi_transaction_t {
  uint32_t flags = 0, cmd = 0, addr = 0;
  const void* tx_buffer = nullptr;
  size_t length = 0;
};
struct spi_transaction_ext_t { spi_transaction_t base; int command_bits = 0, address_bits = 0, dummy_bits = 0; };
struct spi_device_interface_config_t {
  int command_bits, address_bits, dummy_bits, mode, clock_source, duty_cycle_pos;
  int cs_ena_pretrans, cs_ena_posttrans, clock_speed_hz, input_delay_ns, spics_io_num, flags, queue_size;
  void* pre_cb;
  void* post_cb;
};
static std::deque<spi_transaction_t*> dmaQueue;
static std::vector<uint8_t> wire;
static unsigned queueAttempts = 0, failQueueAt = UINT32_MAX, fallbackFrames = 0, windows = 0;
static bool firstChunk = true;
static std::mutex gateMutex;
static std::condition_variable gateCv;
static bool holdDma = false, dmaEntered = false;
static std::vector<uint8_t> physical;
static esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t* cfg, spi_device_handle_t* handle) {
  assert(cfg->clock_speed_hz == 60000000 && cfg->command_bits == 8 && cfg->address_bits == 24);
  assert(cfg->queue_size == 4 && cfg->flags == SPI_DEVICE_HALFDUPLEX);
  *handle = &dmaQueue;
  return ESP_OK;
}
static esp_err_t spi_device_acquire_bus(spi_device_handle_t, TickType_t) {
  assert(!acquired.exchange(true) && dmaQueue.empty());
  queueAttempts = 0;
  firstChunk = true;
  wire.clear();
  return ESP_OK;
}
static void spi_device_release_bus(spi_device_handle_t) {
  assert(acquired.exchange(false) && dmaQueue.empty() && csLevel == HIGH);
  if (wire.size() == 307200) physical = wire;
}
static esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t* tx, TickType_t) {
  assert(acquired && csLevel == LOW && dmaQueue.size() < 4 && tx->length <= 16384 * 8);
  if (queueAttempts++ == failQueueAt) return ESP_ERR_NO_MEM;
  assert(tx->flags & SPI_TRANS_MODE_QIO);
  if (firstChunk) {
    assert(tx->cmd == 0x32 && tx->addr == 0x003C00);
    assert(!(tx->flags & SPI_TRANS_VARIABLE_CMD));
    firstChunk = false;
  } else {
    assert(tx->flags & SPI_TRANS_VARIABLE_CMD);
    assert(tx->flags & SPI_TRANS_VARIABLE_ADDR);
    assert(tx->flags & SPI_TRANS_VARIABLE_DUMMY);
  }
  dmaQueue.push_back(tx);
  return ESP_OK;
}
static esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t** out, TickType_t timeout) {
  assert(timeout == portMAX_DELAY && !dmaQueue.empty());
  {
    std::unique_lock<std::mutex> lock(gateMutex);
    dmaEntered = true;
    gateCv.notify_all();
    gateCv.wait(lock, [] { return !holdDma; });
  }
  auto* tx = dmaQueue.front();
  dmaQueue.pop_front();
  const auto* bytes = static_cast<const uint8_t*>(tx->tx_buffer);
  wire.insert(wire.end(), bytes, bytes + tx->length / 8);
  clockUs += 100;
  *out = tx;
  return ESP_OK;
}
static constexpr int AXS15231B_CASET = 0x2A, AXS15231B_RASET = 0x2B, AXS15231B_RAMWR = 0x2C;
class Arduino_ESP32QSPI {
 public:
  Arduino_ESP32QSPI(int, int, int, int, int, int, bool shared) { assert(shared); }
  virtual bool begin(int32_t, int8_t) { return true; }
  void beginWrite() { assert(!acquired); ++windows; }
  void endWrite() { assert(!acquired); }
  void writeC8D16D16(int command, int start, int last) {
    assert(!acquired && start == 0);
    assert((command == AXS15231B_CASET && last == 319) || (command == AXS15231B_RASET && last == 479));
  }
  void writeCommand(int command) { assert(!acquired && command == AXS15231B_RAMWR); }
};
#include "production_bus.inc.h"
static OfeESP32QSPI bus(45, 47, 21, 48, 40, 39);
static OfeESP32QSPI* ofe_qspi_bus = &bus;
static OfeESP32QSPI* gfx_bus = &bus;
constexpr uint32_t DISPLAY_NATIVE_WIDTH = 320, DISPLAY_NATIVE_HEIGHT = 480, QSPI_FRAME_BYTES = 307200;
constexpr uint32_t LVGL_CANVAS_FLUSH_MIN_INTERVAL_MS = 16;
#define DISPLAY_QSPI_PRESWAPPED_DMA_FB 1
#define DISPLAY_USE_PANEL_HW_ROTATION 0
#define DISPLAY_QSPI_ASYNC_FULL_FRAME 1
static std::vector<uint16_t> render(320 * 480);
static uint16_t* qspi_wire_fb = render.data();
static bool qspi_wire_fb_active = true, qspi_idf_queued_selected = true, qspi_idf_queued_runtime_failed = false;
static OfeQspiFrameWorker qspi_frame_worker;
static bool qspi_async_active = false, lvgl_ready = true, lvgl_canvas_dirty = false;
static uint32_t qspi_snapshot_us = 0, qspi_async_errors = 0, perf_panel_frames = 0;
static uint32_t perf_panel_flush_max_us = 0, lvgl_last_canvas_flush_ms = 0;
static uint64_t perf_panel_flush_us = 0;
static void debug_printf(const char*, ...) {}
static void debug_println(const char*) {}
struct Panel {
  void draw16bitBeRGBBitmap(int x, int y, uint16_t* frame, int w, int h) {
    assert(!acquired && x == 0 && y == 0 && w == 320 && h == 480);
    ++fallbackFrames;
    const auto* bytes = reinterpret_cast<const uint8_t*>(frame);
    physical.assign(bytes, bytes + QSPI_FRAME_BYTES);
    clockUs += 2000;
  }
} panel;
static Panel* output_display = &panel;
struct Canvas { void flush(bool) { assert(false); } } canvas;
static Canvas* gfx = &canvas;
static struct { bool active = false; } display_benchmark;
static bool fw_update_active = false;
static struct { bool update_active = false; } status;
static struct { bool enabled = false; bool active() { return enabled; } } display_ble;
static bool wifiUiOpen = false;
static bool qspi_overlap_paused() {
  return wifiUiOpen || display_ble.active() || fw_update_active || status.update_active;
}
static unsigned resets = 0;
static void display_benchmark_reset_counters(uint32_t) { assert(!acquired); ++resets; }
#include "production_flush.inc.h"

static void openGate() {
  std::lock_guard<std::mutex> lock(gateMutex);
  holdDma = false;
  gateCv.notify_all();
}
static void closeGate() {
  std::lock_guard<std::mutex> lock(gateMutex);
  holdDma = true;
  dmaEntered = false;
}
static void waitForDma() {
  std::unique_lock<std::mutex> lock(gateMutex);
  assert(gateCv.wait_for(lock, std::chrono::seconds(5), [] { return dmaEntered; }));
}
static void paint(uint16_t value) {
  std::fill(render.begin(), render.end(), value);
  lvgl_canvas_dirty = true;
  clockUs += 20000;
}
static void expectPhysical(uint16_t value) {
  assert(physical.size() == QSPI_FRAME_BYTES);
  uint16_t pixel;
  for (size_t i = 0; i < physical.size(); i += 2) {
    memcpy(&pixel, physical.data() + i, 2);
    assert(pixel == value);
  }
}
int main() {
  assert(bus.begin(60000000) && bus.fastReady());
  esp_err_t error = ESP_OK;
  assert(!qspi_transmit_full_frame(reinterpret_cast<const uint8_t*>(render.data()), 100, &error));
  assert(error == ESP_ERR_INVALID_STATE && windows == 0);
  OfeQspiFrameWorker failed;
  assert(!failed.begin(nullptr));
  freeInternal = 50000;
  assert(!failed.begin(qspi_transmit_full_frame) && allocations.empty());
  freeInternal = 150000;
  failAllocation = true;
  assert(!failed.begin(qspi_transmit_full_frame));
  failAllocation = false;
  failSemaphore = true;
  assert(!failed.begin(qspi_transmit_full_frame) && allocations.empty());
  failSemaphore = false;
  failTask = true;
  assert(!failed.begin(qspi_transmit_full_frame) && allocations.empty() && semaphores.empty());
  failTask = false;
  assert(qspi_frame_worker.begin(qspi_transmit_full_frame));
  assert(qspi_frame_worker.begin(qspi_transmit_full_frame) && allocations.size() == 1 && tasks.size() == 1);
  qspi_async_active = true;
  assert(!qspi_frame_worker.submit(nullptr, QSPI_FRAME_BYTES));
  assert(!qspi_frame_worker.submit(render.data(), 100)); // no partial frame API

  // Hold a real worker at DMA completion, change render RAM repeatedly on the
  // GUI thread, and verify the in-flight physical frame remains byte-identical.
  closeGate();
  paint(0x1122);
  clockUs = UINT32_MAX - 500ULL;
  lvgl_flush_canvas_if_dirty();
  waitForDma();
  assert(qspi_frame_worker.busy() && perf_panel_frames == 0 && !lvgl_canvas_dirty);
  paint(0x3344);
  lvgl_flush_canvas_if_dirty();
  paint(0x5566);
  lvgl_flush_canvas_if_dirty();
  assert(lvgl_canvas_dirty && qspi_frame_worker.busy() && perf_panel_frames == 0);
  assert(!qspi_frame_worker.submit(render.data(), QSPI_FRAME_BYTES));
  openGate();
  qspi_collect_full_frame(true);
  expectPhysical(0x1122);
  assert(perf_panel_frames == 1 && !qspi_frame_worker.busy());
  assert(perf_panel_flush_us == 41900); // 40 ms GUI progress + 19 DMA chunks, across micros wrap
  lvgl_flush_canvas_if_dirty(true);
  expectPhysical(0x5566); // coalesced latest state, no lost dirty flag
  assert(perf_panel_frames == 2 && !qspi_frame_worker.busy() && !lvgl_canvas_dirty);
  const auto frames = perf_panel_frames;
  lvgl_flush_canvas_if_dirty();
  assert(perf_panel_frames == frames); // no repeated idle transmission

  // A sparse last update gets collected even when no further pixels change.
  paint(0x7788);
  lvgl_flush_canvas_if_dirty();
  qspi_collect_full_frame(true);
  assert(!lvgl_canvas_dirty && perf_panel_frames == frames + 1);
  expectPhysical(0x7788);

  qspi_mode_command(false);
  assert(!qspi_async_active && !qspi_frame_worker.busy() && resets == 1);
  paint(0x1234);
  lvgl_flush_canvas_if_dirty(true);
  expectPhysical(0x1234);
  display_benchmark.active = true;
  qspi_mode_command(true);
  assert(!qspi_async_active && resets == 1);
  display_benchmark.active = false;
  qspi_mode_command(true);
  assert(qspi_async_active && resets == 2 && !qspi_frame_worker.busy());

  paint(0x2345);
  lvgl_flush_canvas_if_dirty();
  wifiUiOpen = true;
  lvgl_flush_canvas_if_dirty(); // drain before BLE UI may start the radio
  assert(!qspi_frame_worker.busy());
  paint(0x3456);
  lvgl_flush_canvas_if_dirty();
  assert(!qspi_frame_worker.busy() && qspi_async_active);
  expectPhysical(0x3456);
  wifiUiOpen = false;

  // First-queue and mid-frame failures drain all descriptors, release CS/bus,
  // and redraw the complete latest image synchronously, with no stale busy state.
  for (unsigned failure : {0U, 5U}) {
    qspi_async_active = true;
    qspi_idf_queued_selected = true;
    failQueueAt = failure;
    paint(uint16_t(0x99AA + failure));
    const auto before = perf_panel_frames;
    lvgl_flush_canvas_if_dirty(true);
    // The failure is collected at forced completion; retry preserves dirty.
    assert(lvgl_canvas_dirty && !qspi_async_active && !qspi_frame_worker.busy() && !acquired);
    failQueueAt = UINT32_MAX;
    lvgl_flush_canvas_if_dirty(true);
    assert(perf_panel_frames == before + 1 && !lvgl_canvas_dirty);
    expectPhysical(uint16_t(0x99AA + failure));
  }
  assert(qspi_async_errors == 2 && fallbackFrames == 2 && qspi_idf_queued_runtime_failed);
  const auto resetsBeforeFailure = resets;
  qspi_mode_command(true);
  assert(!qspi_async_active && resets == resetsBeforeFailure);

  // A diagnostic/synchronous failure must also disable async selection.
  qspi_async_active = true;
  qspi_idf_queued_selected = true;
  qspi_idf_queued_runtime_failed = false;
  failQueueAt = 0;
  assert(!qspi_idf_send_full_frame());
  assert(!qspi_async_active && !qspi_idf_queued_selected && !acquired);
  failQueueAt = UINT32_MAX;

  for (unsigned i = 0; i < 30; ++i) {
    qspi_async_active = true;
    qspi_idf_queued_selected = true;
    paint(uint16_t(i));
    lvgl_flush_canvas_if_dirty(true);
    expectPhysical(uint16_t(i));
    assert(allocations.size() == 1 && !acquired && dmaQueue.empty());
  }
  for (auto* task : tasks) {
    { std::lock_guard<std::mutex> lock(task->mutex); task->stop = true; task->cv.notify_all(); }
    task->thread.join();
    delete task;
  }
  for (auto* sem : semaphores) delete sem;
  while (!allocations.empty()) heap_caps_free(allocations.begin()->first);
  puts("PASS: real QSPI worker/bus/flush: immutable full-frame DMA snapshot, concurrent rendering, coalescing, sparse completion, force wait, exact window/QIO continuation, first/mid-queue fallback, allocation cleanup, wrap and repeated frames");
}
