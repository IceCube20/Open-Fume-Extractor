#include <assert.h>
#include <stdint.h>
#include <initializer_list>
#include <stdio.h>
#include "../../../OpenFumeExtractorMaster/src/RequestWaitProbe.h"

struct Stats {
  uint32_t rx_frames = 0;
  uint32_t crc_errors = 0;
  uint32_t bad_length = 0;
  uint32_t bad_version = 0;
  uint32_t escape_errors = 0;
  uint32_t overflow_errors = 0;
  uint32_t short_frames = 0;
};

int main() {
  RequestReceiveWindow window(1000, 60);
  assert(window.pollDue(1000) && !window.draining());
  assert(window.pollDue(1059) && !window.draining());
  for (int i = 0; i < 4; ++i) assert(window.pollDue(1098) && window.draining());
  assert(!window.pollDue(1098));
  assert(!window.pollDue(1000)); // Once draining, a clock wrap must not reopen it.
  RequestReceiveWindow wrapped_window(UINT32_MAX - 29, 60);
  assert(wrapped_window.pollDue(29) && !wrapped_window.draining());
  assert(wrapped_window.pollDue(30) && wrapped_window.draining());
  for (int i = 0; i < 3; ++i) assert(wrapped_window.pollDue(30));
  assert(!wrapped_window.pollDue(30));
  // A queued reply behind an unrelated packet survives a long scheduling gap.
  RequestReceiveWindow queued(0, 70);
  unsigned polls = 0;
  bool matched = false;
  while (queued.pollDue(83)) {
    ++polls;
    if (polls == 2) { matched = true; break; }
  }
  assert(matched && polls == 2);
  RequestReceiveWindow absent(0, 70);
  polls = 0;
  while (absent.pollDue(83)) ++polls;
  assert(polls == 4); // A truly missing or mismatched reply still times out.
  Stats stats;
  stats.rx_frames = 100;
  stats.bad_length = 500;
  RequestWaitProbe quiet(1000, stats);
  quiet.sample(2000);
  quiet.sample(3000);
  auto result = quiet.finish(4000, stats);
  assert(result.elapsed_us == 3000 && result.max_poll_gap_us == 1000);
  assert(result.rx_frames == 0 && result.bad_length == 0);

  // A final preemption can expire the deadline before another poll occurs.
  RequestWaitProbe stalled(1000, stats);
  stalled.sample(1500);
  result = stalled.finish(81000, stats);
  assert(result.elapsed_us == 80000 && result.max_poll_gap_us == 79500);

  RequestWaitProbe damaged(0, stats);
  stats.rx_frames += 2;
  ++stats.crc_errors;
  stats.bad_length += 3;
  ++stats.bad_version;
  ++stats.escape_errors;
  ++stats.overflow_errors;
  ++stats.short_frames;
  result = damaged.finish(70000, stats);
  assert(result.rx_frames == 2 && result.crc_errors == 1 && result.bad_length == 3);
  assert(result.bad_version == 1 && result.escape_errors == 1);
  assert(result.overflow_errors == 1 && result.short_frames == 1);

  stats.rx_frames = UINT32_MAX - 1;
  stats.bad_length = UINT32_MAX;
  RequestWaitProbe wrapped(UINT32_MAX - 499, stats);
  wrapped.sample(0);
  stats.rx_frames = 1;
  stats.bad_length = 2;
  result = wrapped.finish(1000, stats);
  assert(result.elapsed_us == 1500 && result.max_poll_gap_us == 1000);
  assert(result.rx_frames == 3 && result.bad_length == 3);

  // Probe snapshots are independent; historical errors must not leak in.
  RequestWaitProbe next(0, stats);
  result = next.finish(50, stats);
  assert(result.bad_length == 0 && result.rx_frames == 0);
  struct Header { uint8_t dst, src, seq, cmd, len; } frame{1, 0x40, 0x7E, 0xD0, 3};
  RequestUnmatchedFrame unmatched;
  assert(unmatched.count == 0);
  unmatched.record(frame, true, WAIT_FRAME_ASYNC_DISPLAY);
  assert(unmatched.count == 1 && unmatched.network);
  assert(unmatched.reason == WAIT_FRAME_ASYNC_DISPLAY);
  assert(unmatched.dst == 1 && unmatched.src == 0x40 && unmatched.seq == 0x7E);
  assert(unmatched.cmd == 0xD0 && unmatched.len == 3);
  const RequestUnmatchedFrame saved = unmatched;
  frame.src = 0x20;
  unmatched.record(frame, false, WAIT_FRAME_COMMAND);
  assert(unmatched.count == 2 && !unmatched.network);
  assert(unmatched.reason == WAIT_FRAME_COMMAND && unmatched.src == 0x20);
  assert(saved.count == 1 && saved.network && saved.src == 0x40);
  for (auto reason : {WAIT_FRAME_OTHER_ADDRESS, WAIT_FRAME_SEQUENCE, WAIT_FRAME_LATE}) {
    unmatched.record(frame, false, reason);
    assert(unmatched.reason == reason);
  }
  assert(unmatched.count == 5);
  unmatched = RequestUnmatchedFrame();
  assert(unmatched.count == 0);
  puts("PASS: request receive gaps, terminal stalls, parser deltas and wraparound");
}
