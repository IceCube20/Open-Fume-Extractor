#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

static uint32_t clock_ms;
static unsigned delays;
static unsigned max_frames_per_tick;
static unsigned frames_this_tick;
uint32_t millis() { return clock_ms; }
void delay(unsigned ms) {
  if (frames_this_tick > max_frames_per_tick) max_frames_per_tick = frames_this_tick;
  frames_this_tick = 0;
  ++delays;
  clock_ms += ms;
}

struct Frame { bool async = false; };
struct FakeLink {
  std::vector<Frame> incoming;
  unsigned position = 0;
  bool flooded = false;
  bool lastRxWasNetwork() const { return true; }
  bool poll(Frame& frame) {
    if (position < incoming.size()) { frame = incoming[position++]; return true; }
    frame = Frame{};
    return flooded;
  }
};

class MasterScheduler {
public:
  FakeLink link_;
  uint32_t hotplug_discovery_window_until_ms_ = 0;
  unsigned received = 0, async = 0, discovery = 0, services = 0;
  unsigned service_cost_ms = 0;
  void reserveDiscoveryWindow();
  void waitForDiscoveryWindow();
  void busDiagRecordRx(const Frame&) { ++received; ++frames_this_tick; }
  bool handleAsyncDisplayFrame(const Frame& frame, bool) {
    if (!frame.async) return false;
    ++async;
    return true;
  }
  void noticeDiscoveryResponse(const Frame&) { ++discovery; }
  void serviceWhileWaiting() { ++services; clock_ms += service_cost_ms; }
};

#include "generated_discovery_guard.inc.h"

static void resetClock(uint32_t now) {
  clock_ms = now;
  delays = max_frames_per_tick = frames_this_tick = 0;
}

int main() {
  resetClock(100);
  MasterScheduler scheduler;
  scheduler.waitForDiscoveryWindow();
  assert(clock_ms == 100 && delays == 0 && scheduler.services == 0);
  scheduler.reserveDiscoveryWindow();
  assert(scheduler.hotplug_discovery_window_until_ms_ == 550);
  scheduler.link_.incoming = {{false}, {true}, {false}};
  scheduler.waitForDiscoveryWindow();
  assert(clock_ms == 550 && delays == 450 && scheduler.services == 450);
  assert(scheduler.received == 3 && scheduler.discovery == 2 && scheduler.async == 1);
  assert(scheduler.hotplug_discovery_window_until_ms_ == 0);
  scheduler.hotplug_discovery_window_until_ms_ = 549;
  scheduler.waitForDiscoveryWindow();
  assert(clock_ms == 550 && delays == 450);

  for (uint32_t now : {0xFFFFFF00u, 0xFFFFFFFFu - 449u}) {
    resetClock(now);
    MasterScheduler wrap;
    wrap.reserveDiscoveryWindow();
    assert(wrap.hotplug_discovery_window_until_ms_ != 0);
    wrap.waitForDiscoveryWindow();
    const uint32_t elapsed = clock_ms - now;
    assert(elapsed >= 450 && elapsed <= 451);
    assert(wrap.hotplug_discovery_window_until_ms_ == 0);
  }

  resetClock(200);
  MasterScheduler flood;
  flood.link_.flooded = true;
  flood.reserveDiscoveryWindow();
  flood.waitForDiscoveryWindow();
  assert(clock_ms == 650 && delays == 450 && max_frames_per_tick == 6);
  assert(flood.received == 2700 && flood.services == 450);

  resetClock(1000);
  MasterScheduler expensive_service;
  expensive_service.service_cost_ms = 7;
  expensive_service.reserveDiscoveryWindow();
  expensive_service.waitForDiscoveryWindow();
  assert(clock_ms >= 1450 && clock_ms < 1458);
  assert(expensive_service.services < 60 && expensive_service.services == delays);
  assert(expensive_service.hotplug_discovery_window_until_ms_ == 0);
  std::puts("PASS: production discovery wait loop, RX/async handling, bounded flood drain, cooperative service and millis wrap");
}
