#pragma once
#include <atomic>
#include <stdint.h>
#include <string.h>

// Opt-in, nonpersistent probe. Only the GUI owner calls service/the panel API.
class OfeRgbOtaProbe {
 public:
  struct Result { uint32_t hz; int32_t error; bool attempted; };
  explicit OfeRgbOtaProbe(uint32_t normal_hz)
      : normal_hz_(normal_hz), requested_hz_(normal_hz) {}
  bool select(const char* argument) {
    if (!argument) return false;
    uint32_t hz;
    if (!strcmp(argument, "normal")) hz=0;
    else if (!strcmp(argument, "15")) hz=15000000U;
    else if (!strcmp(argument, "14")) hz=14000000U;
    else if (!strcmp(argument, "13")) hz=13000000U;
    else return false;
    if (hz && hz >= normal_hz_) return false;
    selected_hz_.store(hz);
    return true;
  }
  bool enabled() const { return selectedHz() != 0; }
  uint32_t selectedHz() const { return selected_hz_.load(); }
  uint32_t requestedHz() const { return requested_hz_.load(); }

  template<class Apply>
  Result service(uint32_t now, bool local_ota, Apply apply) {
    const uint32_t selected = selectedHz();
    const uint32_t desired = local_ota && selected ? selected : normal_hz_;
    if (desired == requestedHz()) return {desired, 0, false};
    if (attempted_ && desired == attempted_hz_ && uint32_t(now - attempted_ms_) < 1000U)
      return {desired, 0, false};
    attempted_ = true;
    attempted_hz_ = desired;
    attempted_ms_ = now;
    const int32_t error = apply(desired);
    if (!error) requested_hz_.store(desired);
    return {desired, error, true};
  }

 private:
  const uint32_t normal_hz_;
  std::atomic<uint32_t> requested_hz_;
  std::atomic<uint32_t> selected_hz_{0};
  uint32_t attempted_hz_=0, attempted_ms_=0;
  bool attempted_=false;
};
