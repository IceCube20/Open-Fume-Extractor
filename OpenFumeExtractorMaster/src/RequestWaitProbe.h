#pragma once

#include <stdint.h>

// Drain a few already buffered packets after a preemption crossed the deadline.
// Never sleep/retransmit in this phase; even continuous traffic is bounded.
class RequestReceiveWindow {
public:
  RequestReceiveWindow(uint32_t started_ms, uint32_t timeout_ms)
      : started_ms_(started_ms), timeout_ms_(timeout_ms) {}
  bool pollDue(uint32_t now_ms) {
    if (!draining_ && uint32_t(now_ms - started_ms_) < timeout_ms_) return true;
    draining_ = true;
    if (!final_polls_) return false;
    --final_polls_;
    return true;
  }
  bool draining() const { return draining_; }
private:
  uint32_t started_ms_, timeout_ms_;
  uint8_t final_polls_ = 4;
  bool draining_ = false;
};

enum RequestFrameDisposition : uint8_t {
  WAIT_FRAME_OTHER_ADDRESS = 1,
  WAIT_FRAME_SEQUENCE = 2,
  WAIT_FRAME_COMMAND = 3,
  WAIT_FRAME_LATE = 4,
  WAIT_FRAME_ASYNC_DISPLAY = 5,
};

// Last valid frame returned to this wait but not used as its response. This
// can be WLAN traffic while a physical request waits; keep transport explicit.
struct RequestUnmatchedFrame {
  uint32_t count = 0;
  uint8_t dst = 0;
  uint8_t src = 0;
  uint8_t seq = 0;
  uint8_t cmd = 0;
  uint8_t len = 0;
  RequestFrameDisposition reason = WAIT_FRAME_OTHER_ADDRESS;
  bool network = false;

  template <typename Frame>
  void record(const Frame& frame, bool via_network, RequestFrameDisposition disposition) {
    ++count;
    dst = frame.dst;
    src = frame.src;
    seq = frame.seq;
    cmd = frame.cmd;
    len = frame.len;
    network = via_network;
    reason = disposition;
  }
};

struct RequestWaitObservation {
  uint32_t elapsed_us = 0;
  uint32_t max_poll_gap_us = 0;
  uint32_t rx_frames = 0;
  uint32_t crc_errors = 0;
  uint32_t bad_length = 0;
  uint32_t bad_version = 0;
  uint32_t escape_errors = 0;
  uint32_t overflow_errors = 0;
  uint32_t short_frames = 0;
  // Filled by the request owner; these matching counters are target-specific.
  uint32_t bad_seq = 0;
  uint32_t bad_cmd = 0;
  uint32_t late_responses = 0;
};

// Samples receive-service gaps, not UART arrival times or module execution time.
// Unsigned deltas cover micros() and parser-counter wraparound.
class RequestWaitProbe {
public:
  template <typename Stats>
  RequestWaitProbe(uint32_t now_us, const Stats& stats)
      : started_us_(now_us), last_poll_us_(now_us) {
    initial_.rx_frames = stats.rx_frames;
    initial_.crc_errors = stats.crc_errors;
    initial_.bad_length = stats.bad_length;
    initial_.bad_version = stats.bad_version;
    initial_.escape_errors = stats.escape_errors;
    initial_.overflow_errors = stats.overflow_errors;
    initial_.short_frames = stats.short_frames;
  }

  void sample(uint32_t now_us) {
    const uint32_t gap = now_us - last_poll_us_;
    if (gap > max_poll_gap_us_) max_poll_gap_us_ = gap;
    last_poll_us_ = now_us;
  }

  template <typename Stats>
  RequestWaitObservation finish(uint32_t now_us, const Stats& stats) {
    // Include a final stall that made the caller leave its deadline loop
    // before it could call poll() again.
    sample(now_us);
    RequestWaitObservation result;
    result.elapsed_us = now_us - started_us_;
    result.max_poll_gap_us = max_poll_gap_us_;
    result.rx_frames = stats.rx_frames - initial_.rx_frames;
    result.crc_errors = stats.crc_errors - initial_.crc_errors;
    result.bad_length = stats.bad_length - initial_.bad_length;
    result.bad_version = stats.bad_version - initial_.bad_version;
    result.escape_errors = stats.escape_errors - initial_.escape_errors;
    result.overflow_errors = stats.overflow_errors - initial_.overflow_errors;
    result.short_frames = stats.short_frames - initial_.short_frames;
    return result;
  }

private:
  uint32_t started_us_;
  uint32_t last_poll_us_;
  uint32_t max_poll_gap_us_ = 0;
  RequestWaitObservation initial_;
};
