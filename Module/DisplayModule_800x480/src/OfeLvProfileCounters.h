#pragma once

#include "../ofe_lv_profiler.h"

// Owned by the LVGL loop task only. Inclusive categories intentionally overlap.
class OfeLvProfileCounters {
 public:
  void begin(unsigned id, uint32_t now) {
    if (id >= OFE_LV_PROFILE_COUNT) return;
    if (depth_[id]++ == 0) start_[id] = now;
  }

  void end(unsigned id, uint32_t now) {
    if (id >= OFE_LV_PROFILE_COUNT) return;
    if (depth_[id] == 0) { ++errors_; return; }
    if (--depth_[id] != 0) return;
    const uint32_t elapsed = now - start_[id];
    auto& sample = samples_[id];
    sample.us += elapsed;
    ++sample.calls;
    if (elapsed > sample.max_us) sample.max_us = elapsed;
  }

  bool take(OfeLvProfileSample* out, uint32_t* errors) {
    for (unsigned i = 0; i < OFE_LV_PROFILE_COUNT; ++i) {
      if (depth_[i] != 0) return false;
    }
    memcpy(out, samples_, sizeof(samples_));
    *errors = errors_;
    memset(samples_, 0, sizeof(samples_));
    errors_ = 0;
    return true;
  }

 private:
  OfeLvProfileSample samples_[OFE_LV_PROFILE_COUNT] = {};
  uint32_t start_[OFE_LV_PROFILE_COUNT] = {};
  uint32_t depth_[OFE_LV_PROFILE_COUNT] = {};
  uint32_t errors_ = 0;
};
