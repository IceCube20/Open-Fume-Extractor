#pragma once

#include <atomic>
#include <stdint.h>

// Fixed-size counters only; transport owns sampling, GUI reads atomic snapshots.
class OfeDisplayOtaTiming {
 public:
  struct Snapshot {
    uint32_t packets, duplicates, rejected, ack_failures;
    uint32_t poll_max_us, check_max_us, handle_max_us, ack_max_us;
  };
  void reset() {
    packets_=0; duplicates_=0; rejected_=0; ack_failures_=0;
    poll_max_=0; check_max_=0; handle_max_=0; ack_max_=0;
    have_poll_=false;
  }
  void poll(uint32_t now) {
    if (have_poll_) maximum(poll_max_, uint32_t(now-last_poll_));
    last_poll_=now;
    have_poll_=true;
  }
  void received() { packets_.fetch_add(1, std::memory_order_relaxed); }
  void checked(uint32_t us, bool valid) {
    maximum(check_max_, us);
    if (!valid) rejected_.fetch_add(1, std::memory_order_relaxed);
  }
  void handled(uint32_t us, bool duplicate) {
    maximum(handle_max_, us);
    if (duplicate) duplicates_.fetch_add(1, std::memory_order_relaxed);
  }
  void acknowledged(uint32_t us, bool sent) {
    maximum(ack_max_, us);
    if (!sent) ack_failures_.fetch_add(1, std::memory_order_relaxed);
  }
  Snapshot snapshot() const {
    return {packets_.load(), duplicates_.load(), rejected_.load(), ack_failures_.load(),
            poll_max_.load(), check_max_.load(), handle_max_.load(), ack_max_.load()};
  }

 private:
  static void maximum(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t previous=target.load(std::memory_order_relaxed);
    while (value>previous && !target.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
  }
  std::atomic<uint32_t> packets_{0}, duplicates_{0}, rejected_{0}, ack_failures_{0};
  std::atomic<uint32_t> poll_max_{0}, check_max_{0}, handle_max_{0}, ack_max_{0};
  uint32_t last_poll_=0;
  bool have_poll_=false;
};
