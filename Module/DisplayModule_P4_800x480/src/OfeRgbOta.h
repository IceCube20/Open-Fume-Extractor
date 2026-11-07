#pragma once

#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// One transport-task owner; only the timing counters are read by the GUI task.
class OfeRgbOta {
 public:
  bool begin(size_t size) {
    if (active_) return false;
    writes_.store(0);
    total_us_.store(0);
    max_us_.store(0);
    error_.store(ESP_OK);
    partition_ = esp_ota_get_next_update_partition(nullptr);
    expected_ = size == OTA_SIZE_UNKNOWN ? 0 : size;
    written_ = 0;
    if (!partition_ || partition_ == esp_ota_get_running_partition() ||
        !size || expected_ > partition_->size) {
      error_.store(ESP_ERR_INVALID_SIZE);
      return false;
    }
    // Defer erasing to sequential writes instead of Arduino's 64-KiB blocks.
    const esp_err_t result = esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
    error_.store(result);
    active_ = result == ESP_OK;
    return active_;
  }

  size_t write(uint8_t* data, size_t size) {
    if (!active_ || (!data && size) || size > partition_->size - written_ ||
        (expected_ && size > expected_ - written_)) {
      error_.store(ESP_ERR_INVALID_SIZE);
      return 0;
    }
    if (!size) return 0;
    const int64_t started = esp_timer_get_time();
    const esp_err_t result = esp_ota_write(handle_, data, size);
    const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
    writes_.fetch_add(1, std::memory_order_relaxed);
    total_us_.fetch_add(elapsed, std::memory_order_relaxed);
    uint32_t previous = max_us_.load(std::memory_order_relaxed);
    while (elapsed > previous && !max_us_.compare_exchange_weak(
        previous, elapsed, std::memory_order_relaxed)) {}
    error_.store(result);
    if (result != ESP_OK) {
      abort();
      return 0;
    }
    written_ += size;
    return size;
  }

  bool end(bool = false) {
    if (!active_ || !written_ || (expected_ && written_ != expected_)) {
      error_.store(ESP_ERR_INVALID_SIZE);
      abort();
      return false;
    }
    // esp_ota_end validates the image and consumes the handle even on failure.
    esp_err_t result = esp_ota_end(handle_);
    active_ = false;
    if (result == ESP_OK) result = esp_ota_set_boot_partition(partition_);
    error_.store(result);
    return result == ESP_OK;
  }

  void abort() {
    if (active_) esp_ota_abort(handle_);
    active_ = false;
  }

  struct Timing { uint32_t writes, total_us, max_us; esp_err_t error; };
  Timing timing() const {
    return {writes_.load(std::memory_order_relaxed), total_us_.load(std::memory_order_relaxed),
            max_us_.load(std::memory_order_relaxed), error_.load(std::memory_order_relaxed)};
  }

 private:
  const esp_partition_t* partition_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  size_t expected_ = 0, written_ = 0;
  bool active_ = false;
  std::atomic<uint32_t> writes_{0}, total_us_{0}, max_us_{0};
  std::atomic<esp_err_t> error_{ESP_OK};
};
