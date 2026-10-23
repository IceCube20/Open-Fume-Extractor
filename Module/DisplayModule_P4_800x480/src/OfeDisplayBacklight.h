#pragma once
#include <atomic>
#include <stdint.h>

// Settings cross the bus/UI task boundary; PWM is owned exclusively by the UI.
class OfeDisplayBacklight {
 public:
  bool configure(uint8_t mode, uint8_t minutes) {
    if (mode > 2 || minutes < 1 || minutes > 60) return false;
    const uint16_t value = mode | (uint16_t(minutes) << 8);
    if (settings_.exchange(value) != value) requestWake();
    return true;
  }
  uint16_t settings() const { return settings_.load(); }
  uint8_t mode() const { return uint8_t(settings()); }
  uint8_t minutes() const { return uint8_t(settings() >> 8); }
  bool isOff() const { return off_.load(); }
  void requestWake() { wake_pending_.store(true); }

  // UI task only. Unsigned subtraction also works across the millis rollover.
  bool wake(uint32_t now) {
    last_activity_ms_ = now;
    return off_.exchange(false);
  }
  bool tick(uint32_t now, bool eco, bool inhibit) {
    const bool requested = wake_pending_.exchange(false);
    if (requested || inhibit) return wake(now);
    const uint16_t value = settings();
    const uint8_t mode = uint8_t(value);
    const bool allowed = mode == 2 || (mode == 1 && eco);
    const bool next = allowed && (off_.load() ||
      uint32_t(now - last_activity_ms_) >= uint32_t(value >> 8) * 60000UL);
    return off_.exchange(next) != next;
  }

 private:
  std::atomic<uint16_t> settings_{5U << 8}; // disabled, five minutes
  std::atomic<bool> off_{false};
  std::atomic<bool> wake_pending_{true};
  uint32_t last_activity_ms_ = 0;
};
