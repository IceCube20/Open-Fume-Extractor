#include "Rs485PeripheralBus.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using namespace jbc_rs485;
static uint32_t now_ms = 1000;
static uint32_t millis() { return now_ms; }
struct ModuleRecord {
  uint8_t type = MODULE_DISPLAY, display_async_seq = 0, display_async_cmd = 0;
  bool display_async_pending = false;
};
struct BusModuleDiag {
  uint32_t last_timeout_ms = 0, last_maintenance_timeout_ms = 0;
  uint32_t late_responses = 0, bad_seq = 0;
  uint8_t last_timeout_seq = 0, last_timeout_cmd = 0;
  uint8_t last_maintenance_timeout_seq = 0, last_maintenance_timeout_cmd = 0;
};
class MasterScheduler {
 public:
  struct {
    ModuleRecord records[2];
    ModuleRecord* find(uint8_t addr) { return addr >= 0x40 && addr <= 0x41 ? &records[addr - 0x40] : nullptr; }
  } registry_;
  BusModuleDiag bus_module_diag_[2];
  Frame display_async_response_[16];
  bool display_async_response_ready_[16] = {};
  uint32_t request_bad_seq_total_ = 0, request_late_response_total_ = 0;
  struct { uint32_t bad_seq = 0; } trace_stats_;
  enum { TRACE_INFO = 0 };
  int16_t busDiagIndex(uint8_t addr) { return addr >= 0x40 && addr <= 0x41 ? addr - 0x40 : -1; }
  bool traceMatches(uint8_t) { return true; }
  template<typename... Args> void traceLog(Args...) {}
  bool handleAsyncDisplayFrame(const Frame&, bool, const Frame* = nullptr);
};
#include "generated_display_reply.inc.h"

static Frame reply(uint8_t addr, uint8_t command, uint8_t sequence) {
  Frame frame;
  frame.dst = ADDR_MASTER; frame.src = addr; frame.seq = sequence;
  frame.cmd = command | 0x80; frame.len = 12; frame.payload[0] = STATUS_OK;
  return frame;
}
int main() {
  MasterScheduler master;
  auto& wired = master.registry_.records[0];
  // Regression: the valid D0 reply with exactly the request's sequence was
  // consumed as "unexpected async"; reproduce all affected reply commands.
  for (uint8_t command : {CMD_DISPLAY_STATUS, CMD_GET_TELEMETRY, CMD_DISPLAY_ALARMS,
                         CMD_DISPLAY_MODULE_LIST, CMD_DISPLAY_MODULE_DETAIL, CMD_DISPLAY_DETAIL_PAGE}) {
    for (unsigned sequence = 0; sequence < 600; ++sequence) {
      Frame request; request.src = ADDR_MASTER; request.dst = 0x40;
      request.cmd = command; request.seq = uint8_t(sequence);
      const Frame response = reply(0x40, command, request.seq);
      assert(!master.handleAsyncDisplayFrame(response, false, &request));
      assert(!master.request_bad_seq_total_ && !master.display_async_response_ready_[0]);
      // A stale WLAN slot after switching to cable must not steal physical RX.
      wired.display_async_pending = true; wired.display_async_seq = request.seq;
      assert(!master.handleAsyncDisplayFrame(response, false));
      wired.display_async_pending = false;
      // A synchronous WLAN request owns its response too.
      assert(!master.handleAsyncDisplayFrame(response, true, &request));
    }
  }
  auto& wifi = master.registry_.records[1];
  wifi.display_async_pending = true; wifi.display_async_seq = 0xA2;
  const Frame response = reply(0x41, CMD_DISPLAY_STATUS, 0xA2);
  Frame wired_request; wired_request.src = ADDR_MASTER; wired_request.dst = 0x40;
  wired_request.cmd = CMD_DISPLAY_STATUS; wired_request.seq = 0xA2;
  assert(master.handleAsyncDisplayFrame(response, true, &wired_request));
  assert(master.display_async_response_ready_[1] && master.display_async_response_[1].seq == 0xA2);
  // Duplicate UDP delivery does not overwrite an already captured response.
  Frame duplicate = response; duplicate.payload[0] = STATUS_BAD_VALUE;
  assert(master.handleAsyncDisplayFrame(duplicate, true, &wired_request));
  assert(master.display_async_response_[1].payload[0] == STATUS_OK);
  wifi.display_async_pending = false; master.display_async_response_ready_[1] = false;
  auto& diag = master.bus_module_diag_[1];
  diag.last_timeout_ms = now_ms; diag.last_timeout_seq = 0xA2; diag.last_timeout_cmd = CMD_DISPLAY_STATUS;
  assert(master.handleAsyncDisplayFrame(response, true));
  assert(diag.late_responses == 1 && master.request_late_response_total_ == 1);
  assert(!master.request_bad_seq_total_);
  now_ms += 1201;
  assert(master.handleAsyncDisplayFrame(response, true));
  assert(master.request_bad_seq_total_ == 1 && diag.bad_seq == 1);
  const auto late = diag.late_responses;
  diag.last_timeout_ms = now_ms;
  assert(!master.handleAsyncDisplayFrame(response, false));
  assert(diag.late_responses == late && master.request_bad_seq_total_ == 1);
  const auto non_async = reply(0x40, CMD_INFO, 7);
  assert(!master.handleAsyncDisplayFrame(non_async, true));
  puts("PASS: actual display reply dispatcher: 3600 wired replies, sequence wrap, stale WLAN slots, synchronous WLAN ownership, mixed transports, UDP duplicate/late/error handling");
}
