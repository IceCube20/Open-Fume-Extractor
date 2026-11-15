#pragma once

#include "MasterRuntimeProfile.h"
#include <stddef.h>

enum class HttpPerfPhase : uint8_t { StatusBuild, StatusSend, HaBuild, HaSend, DiagnosticsBuild, DiagnosticsSend, Count };
struct HttpPerfSnapshot {
  uint32_t window_ms = 0;
  ofe_runtime::Metric phases[(uint8_t)HttpPerfPhase::Count] = {};
  uint64_t payload_bytes = 0;
};
class HttpPerfWindow {
public:
  void tick(uint32_t now, bool enabled) {
    if (enabled != active_) { active_ = enabled; start_ms_ = now; current_ = {}; completed_ = {}; }
    if (!active_) return;
    const uint32_t elapsed = uint32_t(now - start_ms_);
    if (elapsed >= 5000U) {
      current_.window_ms = elapsed; completed_ = current_; current_ = {}; start_ms_ = now;
    }
  }
  void record(HttpPerfPhase phase, uint32_t us, size_t bytes = 0) {
    const uint8_t index = uint8_t(phase);
    if (!active_ || index >= uint8_t(HttpPerfPhase::Count)) return;
    current_.phases[index].add(us);
    if (phase == HttpPerfPhase::StatusBuild || phase == HttpPerfPhase::HaBuild || phase == HttpPerfPhase::DiagnosticsBuild)
      current_.payload_bytes += bytes;
  }
  HttpPerfSnapshot snapshot(uint32_t now) const {
    if (!active_) return {};
    if (completed_.window_ms) return completed_;
    HttpPerfSnapshot out = current_; out.window_ms = uint32_t(now - start_ms_); return out;
  }
private:
  bool active_ = false;
  uint32_t start_ms_ = 0;
  HttpPerfSnapshot current_, completed_;
};
