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
  void applyAutomationFrame(const uint8_t* payload, uint16_t len, uint32_t now) {
    const bool valid = payload && len >= 14 && payload[len - 14] == 0xA3 &&
      payload[len - 13] == 1 && payload[len - 9] == 0xA5 && payload[len - 7] == 0xA6;
    const uint16_t sequence = valid ? uint16_t(payload[len - 11] | uint16_t(payload[len - 10]) << 8) : 0;
    applyAutomation(valid && (payload[len - 12] & 1), sequence, now);
  }
  void applyAutomation(bool hold, uint16_t sequence, uint32_t now) {
    automation_hold_.store(hold);
    automation_received_ms_.store(now);
    if (automation_seen_ && sequence && sequence != automation_sequence_) {
      requestWake();
      automation_wake_.store(true);
    }
    automation_sequence_ = sequence;
    automation_seen_ = true;
  }
  bool takeAutomationWake() { return automation_wake_.exchange(false); }
  bool automationHeld(uint32_t now) const {
    return automation_hold_.load() && uint32_t(now - automation_received_ms_.load()) < 5000UL;
  }

  // UI task only. Unsigned subtraction also works across the millis rollover.
  bool wake(uint32_t now) {
    last_activity_ms_ = now;
    return off_.exchange(false);
  }
  bool tick(uint32_t now, bool eco, bool inhibit) {
    const bool requested = wake_pending_.exchange(false);
    const uint16_t value = settings();
    const uint8_t mode = uint8_t(value);
    const bool allowed = mode == 2 || (mode == 1 && eco);
    // Re-entering Eco starts a fresh delay, not the pre-extraction idle age.
    const bool entering = allowed && !was_allowed_;
    was_allowed_ = allowed;
    if (requested || inhibit || automationHeld(now) || !allowed || entering) return wake(now);
    const bool next = off_.load() ||
      uint32_t(now - last_activity_ms_) >= uint32_t(value >> 8) * 60000UL;
    return off_.exchange(next) != next;
  }

 private:
  std::atomic<uint16_t> settings_{5U << 8}; // disabled, five minutes
  std::atomic<bool> off_{false};
  std::atomic<bool> wake_pending_{true};
  uint32_t last_activity_ms_ = 0;
  bool was_allowed_ = false;
  std::atomic<bool> automation_hold_{false};
  std::atomic<uint32_t> automation_received_ms_{0};
  std::atomic<bool> automation_wake_{false};
  uint16_t automation_sequence_ = 0;
  bool automation_seen_ = false;
};
