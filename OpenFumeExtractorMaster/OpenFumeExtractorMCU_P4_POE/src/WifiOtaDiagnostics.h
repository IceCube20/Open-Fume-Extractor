#pragma once

#include <array>
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace ofe_wifi_ota {
enum Event : size_t {
  Tx, TxFail, Rx, Invalid, Endpoint, Session, Request, Offset, Status,
  Timeout, Unclaimed, ReadFail, EventCount
};
struct Snapshot {
  std::array<uint32_t, EventCount> events{};
  uint32_t poll_gap_max_us=0;
};
class Diagnostics {
 public:
  void reset() {
    for (auto& event:events_) event.store(0, std::memory_order_relaxed);
    poll_gap_max_.store(0, std::memory_order_relaxed);
  }
  void note(Event event) { events_[event].fetch_add(1, std::memory_order_relaxed); }
  void pollGap(uint32_t gap) {
    uint32_t previous=poll_gap_max_.load(std::memory_order_relaxed);
    while (gap>previous && !poll_gap_max_.compare_exchange_weak(previous,gap,std::memory_order_relaxed)) {}
  }
  Snapshot snapshot() const {
    Snapshot out;
    for (size_t i=0;i<EventCount;++i) out.events[i]=events_[i].load(std::memory_order_relaxed);
    out.poll_gap_max_us=poll_gap_max_.load(std::memory_order_relaxed);
    return out;
  }
 private:
  std::array<std::atomic<uint32_t>, EventCount> events_{};
  std::atomic<uint32_t> poll_gap_max_{0};
};
}
