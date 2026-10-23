#pragma once
#include <stdint.h>

namespace ofe_wifi_timing {
struct Metric {
  uint64_t sum = 0;
  uint32_t count = 0, last = 0, maximum = 0;
  void add(uint32_t us) {
    last = us;
    if (us > maximum) maximum = us;
    if (count != UINT32_MAX) { ++count; sum += us; }
  }
  uint32_t average() const { return count ? uint32_t(sum / count) : 0; }
};
struct Peak {
  uint32_t token = 0, rtt_us = 0, display_us = 0, transport_us = 0;
  uint32_t master_poll_us = 0, display_poll_us = UINT32_MAX, master_queue_us = UINT32_MAX;
  uint8_t command = 0, sequence = 0;
};
struct Snapshot {
  Metric rtt, display, transport, master_queue, master_poll, display_poll;
  Peak peak;
  uint32_t sent = 0, invalid = 0, last_ms = 0;
  uint8_t command = 0, sequence = 0;
  bool supported = false;
  uint8_t version = 0;
};
class Tracker {
 public:
  void reset() { pending_ = dispatch_ = false; }
  void begin(uint32_t token, uint8_t seq, uint8_t cmd, uint32_t now_us, uint32_t generation) {
    token_ = token; seq_ = seq; cmd_ = cmd; started_ = now_us; generation_ = generation;
    pending_ = true; dispatch_ = false;
    poll_last_ = now_us; poll_max_ = 0;
  }
  void polled(uint32_t now_us) {
    if (!pending_) return;
    const uint32_t gap = now_us - poll_last_;
    if (gap > poll_max_) poll_max_ = gap;
    poll_last_ = now_us;
  }
  bool finish(uint32_t token, uint8_t seq, uint8_t cmd, uint32_t display_us,
              uint32_t received_us, uint32_t now_ms, uint32_t generation, Snapshot& out,
              uint32_t display_poll_us = UINT32_MAX) {
    if (!pending_ || generation != generation_ || token != token_ || seq != seq_ || cmd != uint8_t(cmd_ | 0x80)) return false;
    polled(received_us);
    pending_ = false;
    const uint32_t rtt = received_us - started_;
    if (rtt > 5000000UL || display_us > rtt) { ++out.invalid; return false; }
    const bool peak = !out.rtt.count || rtt >= out.rtt.maximum;
    out.supported = true; out.rtt.add(rtt); out.display.add(display_us);
    // Includes socket receive queues and both network directions, not RF alone.
    out.transport.add(rtt - display_us);
    out.master_poll.add(poll_max_);
    if (display_poll_us != UINT32_MAX) out.display_poll.add(display_poll_us);
    if (peak) {
      out.peak = {token, rtt, display_us, rtt - display_us, poll_max_, display_poll_us,
                  UINT32_MAX, cmd_, seq};
    }
    out.last_ms = now_ms; out.command = cmd_; out.sequence = seq;
    received_ = received_us; dispatch_ = true;
    return true;
  }
  void dispatched(uint8_t seq, uint8_t cmd, uint32_t now_us, uint32_t generation, Snapshot& out) {
    if (!dispatch_ || generation != generation_ || seq != seq_ || cmd != uint8_t(cmd_ | 0x80)) return;
    dispatch_ = false;
    const uint32_t delay = now_us - received_;
    if (delay <= 5000000UL) {
      out.master_queue.add(delay);
      if (out.peak.token == token_) out.peak.master_queue_us = delay;
    }
  }
 private:
  uint32_t token_ = 0, started_ = 0, received_ = 0, generation_ = 0;
  uint32_t poll_last_ = 0, poll_max_ = 0;
  uint8_t seq_ = 0, cmd_ = 0;
  bool pending_ = false, dispatch_ = false;
};
} // namespace ofe_wifi_timing
