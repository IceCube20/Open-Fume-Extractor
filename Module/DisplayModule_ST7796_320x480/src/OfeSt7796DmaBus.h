#pragma once

#include <Arduino_GFX_Library.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <cstring>

// One owner for SPI2: never mix Arduino's register-driven bus with an IDF
// device on the same peripheral. The original bus is only started on init failure.
class OfeSt7796DmaBus : public Arduino_DataBus {
 public:
  static constexpr size_t maxTileBytes = 480U * 48U * 2U;
  static constexpr size_t maxTransferBytes = 32768U; // S3 SPI length register: 18 bits
  static constexpr size_t internalReserve = 96U * 1024U;

  OfeSt7796DmaBus(int8_t dc, int8_t cs, int8_t sck, int8_t mosi)
      : dc_(dc), cs_(cs), sck_(sck), mosi_(mosi),
        fallback_(dc, cs, sck, mosi, GFX_NOT_DEFINED) {}

  bool begin(int32_t speed = SPI_DEFAULT_FREQ, int8_t mode = GFX_NOT_DEFINED) override {
    if (handle_ || fallbackActive_) return true;
    spi_bus_config_t bus = {};
    bus.sclk_io_num = sck_;
    bus.mosi_io_num = mosi_;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = maxTransferBytes;
    esp_err_t error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (error == ESP_OK) {
      spi_device_interface_config_t device = {};
      device.clock_speed_hz = speed > 0 ? speed : SPI_DEFAULT_FREQ;
      device.mode = mode == GFX_NOT_DEFINED ? SPI_MODE3 : mode;
      device.spics_io_num = -1;
      device.queue_size = 2;
      device.flags = SPI_DEVICE_NO_DUMMY;
      device.post_cb = transferDone;
      error = spi_bus_add_device(SPI2_HOST, &device, &handle_);
      if (error != ESP_OK) {
        handle_ = nullptr;
        // Only release a bus successfully initialized by this object.
        ESP_ERROR_CHECK(spi_bus_free(SPI2_HOST));
      }
    } else if (error == ESP_ERR_INVALID_STATE) {
      // Another owner already has SPI2. Do not reset its registers or pin mux.
      return false;
    }
    if (!handle_) {
      fallbackActive_ = fallback_.begin(speed, mode);
      return fallbackActive_;
    }
    pinMode(dc_, OUTPUT);
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    digitalWrite(dc_, HIGH);
    return true;
  }

  bool dmaAvailable() const { return handle_ != nullptr; }
  bool pending() const { return pending_ != 0; }
  size_t capacity() const { return stageBytes_; }
  uint32_t errors() const { return errors_; }

  // Called only between GUI passes, after the previous flush was acknowledged.
  // Keep the old allocation on failure, including when growing a live profile.
  bool reserve(size_t bytes) {
    if (!handle_ || pending_ || !bytes || bytes > maxTileBytes) return false;
    if (bytes <= stageBytes_) return true;
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(internal) < bytes + internalReserve) return false;
    auto* next = static_cast<uint16_t*>(heap_caps_aligned_alloc(64, bytes, caps));
    if (!next) return false;
    if (heap_caps_get_free_size(internal) < internalReserve) {
      heap_caps_free(next);
      return false;
    }
    heap_caps_free(stage_);
    stage_ = next;
    stageBytes_ = bytes;
    return true;
  }

  // The caller has opened the panel window with Arduino_ST7796. Copy/swap into
  // a bounded DMA-capable buffer; LVGL may render into its other PSRAM buffer.
  bool queuePixels(const uint16_t* pixels, size_t count) {
    if (!handle_ || pending_ || !count || count > stageBytes_ / 2U) return false;
    for (size_t i = 0; i < count; ++i) stage_[i] = swap(pixels[i]);
    size_t offset = 0;
    size_t remaining = count * 2U;
    tailBytes_ = 0;
    doneUs_.store(0, std::memory_order_relaxed);
    digitalWrite(dc_, HIGH);
    queuedUs_ = static_cast<uint32_t>(esp_timer_get_time());
    for (auto& tx : transactions_) {
      if (!remaining) break;
      const size_t bytes = remaining > maxTransferBytes ? maxTransferBytes : remaining;
      tx = {};
      tx.flags = SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL;
      tx.length = bytes * 8U;
      tx.tx_buffer = reinterpret_cast<uint8_t*>(stage_) + offset;
      tx.user = this;
      if (spi_device_queue_trans(handle_, &tx, 0) != ESP_OK) {
        ++errors_;
        if (!pending_) return false;
        // First half already owns DMA. Complete it, then send only the unsent
        // tail synchronously in waitPixels, never resend the complete tile.
        tailOffset_ = offset;
        tailBytes_ = remaining;
        break;
      }
      ++pending_;
      offset += bytes;
      remaining -= bytes;
    }
    return true;
  }

  uint32_t waitPixels() {
    if (!pending_) return 0;
    // Never release/reuse memory after a timeout with DMA still owning it.
    for (uint8_t i = 0; i < pending_; ++i) {
      spi_transaction_t* result = nullptr;
      ESP_ERROR_CHECK(spi_device_get_trans_result(handle_, &result, portMAX_DELAY));
      if (result != &transactions_[i]) abort();
    }
    pending_ = 0;
    uint32_t duration = doneUs_.load(std::memory_order_acquire) - queuedUs_;
    if (tailBytes_) {
      spi_transaction_t tail = {};
      tail.flags = SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL;
      tail.length = tailBytes_ * 8U;
      tail.tx_buffer = reinterpret_cast<uint8_t*>(stage_) + tailOffset_;
      tail.user = this;
      const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
      ESP_ERROR_CHECK(spi_device_polling_transmit(handle_, &tail));
      duration += doneUs_.load(std::memory_order_acquire) - started;
      tailBytes_ = 0;
    }
    return duration;
  }

  void beginWrite() override {
    if (!handle_) { fallback_.beginWrite(); return; }
    waitPixels();
    digitalWrite(cs_, LOW);
  }
  void endWrite() override {
    if (!handle_) { fallback_.endWrite(); return; }
    waitPixels();
    digitalWrite(cs_, HIGH);
  }
  void writeCommand(uint8_t command) override {
    if (!handle_) { fallback_.writeCommand(command); return; }
    send(&command, 1, false);
  }
  void writeCommand16(uint16_t command) override {
    if (!handle_) { fallback_.writeCommand16(command); return; }
    uint8_t bytes[] = {uint8_t(command >> 8), uint8_t(command)};
    send(bytes, sizeof(bytes), false);
  }
  void writeCommandBytes(uint8_t* data, uint32_t bytes) override {
    if (!handle_) { fallback_.writeCommandBytes(data, bytes); return; }
    send(data, bytes, false);
  }
  void write(uint8_t value) override {
    if (!handle_) { fallback_.write(value); return; }
    send(&value, 1, true);
  }
  void write16(uint16_t value) override {
    if (!handle_) { fallback_.write16(value); return; }
    uint8_t bytes[] = {uint8_t(value >> 8), uint8_t(value)};
    send(bytes, sizeof(bytes), true);
  }
  void writeBytes(uint8_t* data, uint32_t bytes) override {
    if (!handle_) { fallback_.writeBytes(data, bytes); return; }
    send(data, bytes, true);
  }
  void writeRepeat(uint16_t pixel, uint32_t count) override {
    if (!handle_) { fallback_.writeRepeat(pixel, count); return; }
    for (auto& item : sync_) item = swap(pixel);
    while (count) {
      const uint32_t n = count > syncPixels ? uint32_t(syncPixels) : count;
      send(sync_, n * 2U, true);
      count -= n;
    }
  }
  void writePixels(uint16_t* pixels, uint32_t count) override {
    if (!handle_) { fallback_.writePixels(pixels, count); return; }
    while (count) {
      const uint32_t n = count > syncPixels ? uint32_t(syncPixels) : count;
      for (size_t i = 0; i < n; ++i) sync_[i] = swap(pixels[i]);
      send(sync_, n * 2U, true);
      pixels += n;
      count -= n;
    }
  }

 private:
  static constexpr size_t syncPixels = 128;
  static uint16_t swap(uint16_t value) { return uint16_t((value << 8) | (value >> 8)); }
  static void transferDone(spi_transaction_t* transaction) {
    if (!transaction->user) return;
    auto* self = static_cast<OfeSt7796DmaBus*>(transaction->user);
    self->doneUs_.store(static_cast<uint32_t>(esp_timer_get_time()), std::memory_order_release);
  }
  void send(const void* data, size_t bytes, bool isData) {
    waitPixels();
    digitalWrite(dc_, isData ? HIGH : LOW);
    // Polling commands use inline data; larger synchronous writes use a fixed
    // internal bounce buffer, never driver-allocated copies of PSRAM data.
    const auto* source = static_cast<const uint8_t*>(data);
    while (bytes) {
      spi_transaction_t tx = {};
      const size_t n = bytes > sizeof(wire_) ? sizeof(wire_) : bytes;
      tx.length = n * 8U;
      if (n <= sizeof(tx.tx_data)) {
        tx.flags = SPI_TRANS_USE_TXDATA;
        std::memcpy(tx.tx_data, source, n);
      } else {
        std::memcpy(wire_, source, n);
        tx.tx_buffer = wire_;
      }
      ESP_ERROR_CHECK(spi_device_polling_transmit(handle_, &tx));
      source += n;
      bytes -= n;
    }
  }

  int8_t dc_, cs_, sck_, mosi_;
  Arduino_ESP32SPI fallback_;
  spi_device_handle_t handle_ = nullptr;
  uint16_t* stage_ = nullptr;
  size_t stageBytes_ = 0;
  alignas(4) uint16_t sync_[syncPixels] = {};
  alignas(4) uint8_t wire_[256] = {};
  spi_transaction_t transactions_[2] = {};
  std::atomic<uint32_t> doneUs_{0};
  uint32_t queuedUs_ = 0, errors_ = 0;
  size_t tailOffset_ = 0, tailBytes_ = 0;
  uint8_t pending_ = 0;
  bool fallbackActive_ = false;
};
