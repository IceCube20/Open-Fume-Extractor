#pragma once

#include <stdint.h>

enum class MqttPerfPhase : uint8_t { Build, Publish, ClientLoop, MasterBuild, ModuleBuild, TransportWrite, Count };

struct MqttPerfMetric {
  uint64_t total_us = 0;
  uint32_t calls = 0;
  uint32_t max_us = 0;
  void add(uint32_t elapsed_us) {
    total_us += elapsed_us;
    ++calls;
    if (elapsed_us > max_us) max_us = elapsed_us;
  }
  uint32_t averageUs() const { return calls ? (uint32_t)(total_us / calls) : 0; }
};

struct MqttPerfSnapshot {
  uint32_t window_ms = 0;
  MqttPerfMetric phases[(uint8_t)MqttPerfPhase::Count] = {};
  uint64_t payload_bytes = 0;
  uint32_t publish_failed = 0;
  uint64_t transport_bytes = 0;
  uint32_t transport_short_writes = 0;
};

// One bounded time window, no allocations. Synchronization belongs to the caller.
class MqttPerfWindow {
public:
  static constexpr uint32_t WINDOW_MS = 5000;
  void reset(uint32_t now, bool enabled) {
    active_ = enabled;
    start_ms_ = now;
    current_ = {};
    completed_ = {};
  }
  void tick(uint32_t now, bool enabled) {
    if (enabled != active_) reset(now, enabled);
    if (!active_) return;
    const uint32_t elapsed = (uint32_t)(now - start_ms_);
    if (elapsed >= WINDOW_MS) {
      current_.window_ms = elapsed;
      completed_ = current_;
      current_ = {};
      start_ms_ = now;
    }
  }
  void record(MqttPerfPhase phase, uint32_t elapsed_us, uint32_t bytes = 0, bool success = true) {
    const uint8_t index = (uint8_t)phase;
    if (!active_ || index >= (uint8_t)MqttPerfPhase::Count) return;
    current_.phases[index].add(elapsed_us);
    if (phase == MqttPerfPhase::MasterBuild || phase == MqttPerfPhase::ModuleBuild)
      current_.phases[(uint8_t)MqttPerfPhase::Build].add(elapsed_us);
    if (phase == MqttPerfPhase::Publish) {
      if (success) current_.payload_bytes += bytes;
      else ++current_.publish_failed;
    }
    if (phase == MqttPerfPhase::TransportWrite) {
      current_.transport_bytes += bytes;
      if (!success) ++current_.transport_short_writes;
    }
  }
  MqttPerfSnapshot snapshot(uint32_t now) const {
    if (!active_) return {};
    if (completed_.window_ms) return completed_;
    MqttPerfSnapshot result = current_;
    result.window_ms = (uint32_t)(now - start_ms_);
    return result;
  }
private:
  bool active_ = false;
  uint32_t start_ms_ = 0;
  MqttPerfSnapshot current_;
  MqttPerfSnapshot completed_;
};
