#pragma once
#include <stdint.h>

namespace ofe_runtime {
enum Phase : uint8_t { Local, Command, Ota, Scheduler, Logic, Maintenance, PhaseCount };
enum Task : uint8_t { LoopTask, HttpTask, MqttTask, HeapTask, TaskCount };
struct Metric {
  uint64_t total_us = 0;
  uint32_t calls = 0, max_us = 0;
  void add(uint32_t us) {
    total_us += us; ++calls;
    if (us > max_us) max_us = us;
  }
  uint32_t averageUs() const { return calls ? uint32_t(total_us / calls) : 0; }
};
struct TaskLoad { bool valid = false; uint8_t percent = 0; };
struct Snapshot {
  bool active = false;
  uint32_t window_ms = 0, sampled_ms = 0;
  TaskLoad tasks[TaskCount] = {};
  Metric phases[PhaseCount] = {};
};
class PhaseWindow {
 public:
  bool sync(uint32_t now_ms, bool enabled) {
    if (enabled == enabled_) return false;
    enabled_ = enabled; start_ms_ = now_ms;
    for (auto& metric : phases_) metric = {};
    return true;
  }
  void record(Phase phase, uint32_t us) {
    if (enabled_ && phase < PhaseCount) phases_[phase].add(us);
  }
  Snapshot take(uint32_t now_ms) {
    Snapshot out;
    if (!enabled_) return out;
    out.active = true; out.window_ms = now_ms - start_ms_; out.sampled_ms = now_ms;
    for (uint8_t i = 0; i < PhaseCount; ++i) { out.phases[i] = phases_[i]; phases_[i] = {}; }
    start_ms_ = now_ms;
    return out;
  }
 private:
  bool enabled_ = false;
  uint32_t start_ms_ = 0;
  Metric phases_[PhaseCount] = {};
};

// The counter and elapsed interval use the same FreeRTOS runtime timebase.
// A task's 100% means one fully occupied core, not both cores combined.
template <typename Counter>
class TaskCounter {
 public:
  void reset() { identity_ = 0; }
  TaskLoad sample(uintptr_t identity, Counter counter, Counter now) {
    TaskLoad out;
    if (identity && identity_ == identity) {
      const Counter elapsed = Counter(now - previous_time_);
      const Counter busy = Counter(counter - previous_counter_);
      if (elapsed) {
        const uint64_t bounded = busy > elapsed ? elapsed : busy;
        out.valid = true;
        out.percent = uint8_t((bounded * 100ULL + elapsed / 2ULL) / elapsed);
      }
    }
    identity_ = identity; previous_counter_ = counter; previous_time_ = now;
    return out;
  }
 private:
  uintptr_t identity_ = 0;
  Counter previous_counter_ = 0, previous_time_ = 0;
};
} // namespace ofe_runtime
