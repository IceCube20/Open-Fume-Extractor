#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>
#include <malloc.h>
#include "OfeSt7796DmaBus.h"
#include "OfeSmallDisplayProfile.h"

unsigned fallbackBegins = 0;
static unsigned frees = 0, busFrees = 0, waits = 0, ready = 0, allocations = 0;
static bool failAllocation = false, failQueue = false, failSecondQueue = false;
static size_t freeInternal = 250000;
static int initError = ESP_OK, addError = ESP_OK;
static uint64_t nowUs = 100;
static std::map<void*, size_t> memory;
static std::map<int, int> pins;
static spi_device_interface_config_t config;
static spi_transaction_t* flight = nullptr;
static spi_transaction_t* flight2 = nullptr;
static std::vector<uint8_t> wire;
void pinMode(int, int) {}
void digitalWrite(int pin, int level) { pins[pin] = level; }
int64_t esp_timer_get_time() { return nowUs; }
static uint32_t micros() { return uint32_t(nowUs); }
void* heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t caps) {
  ++allocations;
  assert(alignment == 64 && (caps & MALLOC_CAP_8BIT));
  if (failAllocation) return nullptr;
  void* p = _aligned_malloc(bytes, alignment);
  assert(p);
  memory[p] = bytes;
  return p;
}
void heap_caps_free(void* p) {
  if (!p) return;
  assert(!flight || flight->tx_buffer != p);
  assert(memory.erase(p) == 1);
  ++frees;
  _aligned_free(p);
}
size_t heap_caps_get_free_size(uint32_t) { return freeInternal; }
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t* bus, int dma) {
  assert(host == SPI2_HOST && dma == SPI_DMA_CH_AUTO);
  assert(bus->max_transfer_sz == 32768 && bus->miso_io_num == -1);
  return initError;
}
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t* device, spi_device_handle_t* handle) {
  config = *device;
  if (addError != ESP_OK) return addError;
  *handle = &config;
  return ESP_OK;
}
esp_err_t spi_bus_free(int) { ++busFrees; return ESP_OK; }
static void append(const spi_transaction_t* tx) {
  assert(tx->length % 8 == 0);
  const auto* data = tx->flags & SPI_TRANS_USE_TXDATA ? tx->tx_data : static_cast<const uint8_t*>(tx->tx_buffer);
  wire.insert(wire.end(), data, data + tx->length / 8);
}
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t* tx, int timeout) {
  assert(!flight2 && timeout == 0 && pins[12] == LOW && pins[13] == HIGH);
  assert(tx->length <= 262144);
  assert(tx->flags & SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
  if (failQueue || (failSecondQueue && flight)) return ESP_ERR_NO_MEM;
  if (!flight) flight = tx;
  else flight2 = tx;
  return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t** result, int timeout) {
  assert(flight && timeout == portMAX_DELAY);
  ++waits;
  nowUs += 1000;
  append(flight);
  config.post_cb(flight);
  *result = flight;
  flight = flight2;
  flight2 = nullptr;
  return ESP_OK;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t* tx) {
  assert(!flight && pins[12] == LOW);
  append(tx);
  if (config.post_cb) config.post_cb(tx);
  return ESP_OK;
}

#define OFE_ST7796_DMA_PATH 1
#define DISPLAY_DIRECT_PARTIAL_FLUSH 1
#define DISPLAY_FAST_CANVAS_ROTATE1 0
#define DISPLAY_USE_PANEL_HW_ROTATION 1
struct lv_display_t { bool last = false; } display;
struct lv_area_t { int x1, y1, x2, y2; };
static int lv_area_get_width(const lv_area_t* a) { return a->x2 - a->x1 + 1; }
static int lv_area_get_height(const lv_area_t* a) { return a->y2 - a->y1 + 1; }
static bool lv_display_flush_is_last(lv_display_t* d) { return d->last; }
static void lv_display_flush_ready(lv_display_t*) { assert(!flight); ++ready; }
static OfeSt7796DmaBus st7796_dma_bus(13, 12, 10, 11);
static uint8_t* st7796_draw_buf2 = nullptr;
static bool st7796_async_active = true, lvgl_canvas_dirty = false;
static lv_display_t* st7796_pending_display = nullptr;
static bool st7796_pending_last = false;
static uint32_t st7796_pending_bytes = 0, st7796_pending_setup_us = 0;
static uint64_t perf_panel_flush_us = 0, perf_panel_bytes = 0, perf_lvgl_pixels = 0, perf_canvas_draw_us = 0;
static uint32_t perf_panel_flush_max_us = 0, perf_lvgl_frames = 0, perf_panel_frames = 0;
static uint32_t perf_lvgl_tiles = 0, perf_canvas_draw_max_us = 0;
static void debug_println(const char*) {}
struct Panel {
  unsigned synchronous = 0;
  uint32_t width() { return 480; }
  void startWrite() { st7796_dma_bus.beginWrite(); }
  void endWrite() { st7796_dma_bus.endWrite(); }
  void writeAddrWindow(int x, int y, int w, int h) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    st7796_dma_bus.writeCommand(0x2C);
  }
  void draw16bitRGBBitmap(int, int, uint16_t* p, int w, int h) {
    ++synchronous;
    startWrite();
    st7796_dma_bus.writePixels(p, w * h);
    endWrite();
  }
} panel;
static Panel* output_display = &panel;
static Panel* gfx = &panel;
static lv_display_t* lvgl_disp = &display;
static uint8_t* lvgl_draw_buf = nullptr;
constexpr int LV_DISPLAY_RENDER_MODE_PARTIAL = 0;
static uint8_t* installed1 = nullptr;
static uint8_t* installed2 = nullptr;
static void lv_display_set_buffers(lv_display_t*, uint8_t* a, uint8_t* b, uint32_t, int) {
  assert(!flight && !st7796_pending_display);
  installed1 = a;
  installed2 = b;
}
#include "production.inc.h"

int main() {
  // Init failure cleans only owned IDF state; never touch another bus owner.
  initError = ESP_ERR_INVALID_STATE;
  OfeSt7796DmaBus occupied(13, 12, 10, 11);
  assert(!occupied.begin() && !busFrees && !fallbackBegins);
  initError = ESP_ERR_NO_MEM;
  OfeSt7796DmaBus noMemory(13, 12, 10, 11);
  assert(noMemory.begin() && !noMemory.dmaAvailable() && fallbackBegins == 1);
  initError = ESP_OK;
  addError = ESP_ERR_NO_MEM;
  OfeSt7796DmaBus noDevice(13, 12, 10, 11);
  assert(noDevice.begin() && busFrees == 1 && fallbackBegins == 2);
  addError = ESP_OK;
  assert(st7796_dma_bus.begin(80000000));
  assert(config.clock_speed_hz == 80000000 && config.mode == 3 && config.queue_size == 2);
  assert(st7796_dma_bus.reserve(480 * 32 * 2));
  assert(st7796_dma_bus.capacity() == 30720);

  // Command/pixel endian order, including odd pixel counts, and no source mutation.
  panel.startWrite();
  st7796_dma_bus.writeCommand(0x2A);
  st7796_dma_bus.write16(0x1234);
  const uint16_t pixels[] = {0xF800, 0x07E0, 0x001F};
  uint16_t mutablePixels[3];
  memcpy(mutablePixels, pixels, sizeof(pixels));
  st7796_dma_bus.writePixels(mutablePixels, 3);
  assert((wire == std::vector<uint8_t>{0x2A, 0x12, 0x34, 0xF8, 0, 7, 0xE0, 0, 0x1F}));
  assert(!memcmp(mutablePixels, pixels, sizeof(pixels)));
  panel.endWrite();

  const lv_area_t area{1, 2, 3, 2};
  display.last = false;
  wire.clear();
  nowUs = UINT32_MAX - 500ULL;
  lvgl_flush_cb(&display, &area, reinterpret_cast<uint8_t*>(mutablePixels));
  assert(flight && ready == 0 && perf_lvgl_tiles == 1 && perf_panel_frames == 0);
  assert(!st7796_dma_bus.reserve(46080)); // no buffer replacement during DMA
  assert(!st7796_dma_bus.queuePixels(pixels, 3));
  memset(mutablePixels, 0xEE, sizeof(mutablePixels)); // DMA owns its copy, not PSRAM
  st7796_complete_flush(&display);
  assert(!flight && ready == 1 && perf_panel_flush_us == 1000 && perf_panel_bytes == 6);
  assert((wire == std::vector<uint8_t>{0x2C, 0xF8, 0, 7, 0xE0, 0, 0x1F}));
  st7796_complete_flush(&display);
  assert(ready == 1); // no double acknowledge

  display.last = true;
  memcpy(mutablePixels, pixels, sizeof(pixels));
  failQueue = true;
  lvgl_flush_cb(&display, &area, reinterpret_cast<uint8_t*>(mutablePixels));
  failQueue = false;
  assert(!flight && ready == 2 && perf_lvgl_frames == 1 && st7796_dma_bus.errors() == 1);
  assert(!memcmp(mutablePixels, pixels, sizeof(pixels)));

  // Profile swaps drain/ack first, attach both candidates, then free both old buffers.
  lvgl_draw_buf = ofe_small_display::allocatePsram(480, 32);
  st7796_draw_buf2 = ofe_small_display::allocatePsram(480, 32);
  auto* old = lvgl_draw_buf;
  auto* old2 = st7796_draw_buf2;
  auto* candidate = ofe_small_display::allocatePsram(480, 48);
  failAllocation = true;
  assert(!st7796_replace_profile(candidate, 46080));
  failAllocation = false;
  assert(lvgl_draw_buf == old && st7796_draw_buf2 == old2 && memory.count(old));
  freeInternal = 100000;
  assert(!st7796_replace_profile(candidate, 46080));
  assert(lvgl_draw_buf == old && st7796_dma_bus.capacity() == 30720);
  freeInternal = 250000;
  lvgl_flush_cb(&display, &area, reinterpret_cast<uint8_t*>(mutablePixels));
  assert(flight);
  assert(st7796_replace_profile(candidate, 46080));
  assert(!flight && !memory.count(old) && !memory.count(old2));
  assert(installed1 == candidate && installed2 == st7796_draw_buf2);
  assert(st7796_dma_bus.capacity() == 46080);
  // B48 exceeds the S3's 32-KiB single-transfer limit; two stable descriptors
  // must cover the exact pixels, including when the second queue call fails.
  std::vector<uint16_t> large(480 * 48, 0xF800);
  const lv_area_t largeArea{0, 0, 479, 47};
  wire.clear();
  lvgl_flush_cb(&display, &largeArea, reinterpret_cast<uint8_t*>(large.data()));
  assert(flight && flight2 && flight->length == 262144);
  st7796_complete_flush(&display);
  assert(wire.size() == 1 + large.size() * 2 && !flight);
  for (size_t i = 1; i < wire.size(); i += 2) assert(wire[i] == 0xF8 && wire[i + 1] == 0);
  wire.clear();
  failSecondQueue = true;
  lvgl_flush_cb(&display, &largeArea, reinterpret_cast<uint8_t*>(large.data()));
  assert(flight && !flight2 && st7796_pending_display);
  failSecondQueue = false;
  st7796_complete_flush(&display);
  assert(wire.size() == 1 + large.size() * 2 && !flight);
  for (size_t i = 1; i < wire.size(); i += 2) assert(wire[i] == 0xF8 && wire[i + 1] == 0);
  const auto persistent = memory.size();
  for (int i = 0; i < 60; ++i) {
    const uint16_t lines = i % 2 ? 16 : 32;
    candidate = ofe_small_display::allocatePsram(480, lines);
    assert(st7796_replace_profile(candidate, 480U * lines * 2U));
    assert(memory.size() == persistent);
  }
  st7796_async_active = false;
  candidate = ofe_small_display::allocatePsram(480, 32);
  assert(st7796_replace_profile(candidate, 30720) && !st7796_draw_buf2);
  const unsigned syncBefore = panel.synchronous;
  lvgl_flush_cb(&display, &area, reinterpret_cast<uint8_t*>(mutablePixels));
  assert(panel.synchronous == syncBefore + 1 && !flight);
  puts("PASS: real ST7796 adapter/flush/profile: ownership, init/queue/allocation fallback, RGB565/odd pixels, completion, timer wrap, safe swaps and no profile leaks");
}
