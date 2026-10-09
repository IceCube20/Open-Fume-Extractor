#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "../../../OpenFumeExtractorMaster/src/RequestWaitProbe.h"
#include "../../../OpenFumeExtractorMaster/src/bus/Rs485PeripheralBus.h"
using namespace jbc_rs485;
static uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
static constexpr int TRACE_TIMEOUT = 3;

struct ModuleRecord {
  bool online = true, firmware_recovery_pending = false;
  uint32_t firmware_recovery_started_ms = 0, last_seen_ms = 0;
  uint8_t type = 0, consecutive_timeouts = 0, last_timeout_cmd = 0;
  uint32_t caps = 0, miss_count = 0, last_timeout_ms = 0;
  uint16_t timeout_count = 0;
  uint8_t display_cache_pending_mask = 0;
  uint32_t display_cache_next_ms = 0, display_alarm_signature = 0, display_alarm_last_ms = 0;
  bool eco_mode = false, light_sleep = false;
  uint32_t eco_since_ms = 0;
};
struct ModuleRegistry {
  ModuleRecord records[256];
  ModuleRecord* find(uint8_t addr) { return &records[addr]; }
  const ModuleRecord* find(uint8_t addr) const { return &records[addr]; }
};
#include "generated_maintenance.inc.h"

int main() {
  using Kind = MasterScheduler::RequestDiagnosticsKind;
  static MasterScheduler s;
  clock_ms = 100;
  auto& target = s.registry_.records[0x40];
  auto& diag = s.bus_module_diag_[0x40];
  diag.last_timeout_ms = 7;
  diag.last_timeout_seq = 8;
  diag.last_timeout_cmd = CMD_GET_IO;
  diag.last_timeout_probe_valid = true;
  s.module_fw_active_ = true;
  s.module_fw_target_ = 0x40;
  for (uint8_t cmd : {CMD_FW_BEGIN, CMD_FW_CHUNK, CMD_FW_END, CMD_FW_ABORT, CMD_FW_REBOOT, CMD_DISPLAY_STATUS}) {
    assert(s.requestDiagnosticsKind(0x40, cmd) == Kind::Update);
    s.recordRequestTimeout(0x40, cmd, 17, 70000, false);
    assert(target.online && target.miss_count == 0 && target.timeout_count == 0);
    assert(target.consecutive_timeouts == 0 && diag.timeouts == 0);
  }
  assert(diag.update_timeouts == 6 && s.trace_stats_.update_timeouts == 6);
  assert(s.trace_stats_.timeouts == 0 && s.trace_budget == 65535);
  assert(diag.last_timeout_ms == 7 && diag.last_timeout_seq == 8);
  assert(diag.last_timeout_cmd == CMD_GET_IO && diag.last_timeout_probe_valid);
  assert(diag.last_maintenance_timeout_seq == 17);
  assert(std::strcmp(s.trace_text, "firmware update timeout") == 0);

  // Another module still becomes offline after its usual five misses.
  for (int i = 0; i < 5; ++i) s.recordRequestTimeout(0x20, CMD_GET_IO, 3, 70, false);
  const auto& other = s.registry_.records[0x20];
  assert(!other.online && other.miss_count == 5 && other.timeout_count == 1);
  assert(s.bus_module_diag_[0x20].timeouts == 5 && s.offline_callbacks == 1);

  s.module_fw_active_ = false;
  assert(s.requestDiagnosticsKind(0x40, CMD_FW_REBOOT) == Kind::Normal);
  target.firmware_recovery_pending = true;
  target.firmware_recovery_started_ms = 0xFFFFFF00U;
  clock_ms = target.firmware_recovery_started_ms + 14999U;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Recovery);
  s.recordRequestTimeout(0x40, CMD_INFO, 21, 30, false);
  assert(diag.recovery_timeouts == 1 && target.miss_count == 0);
  assert(s.trace_stats_.recovery_timeouts == 1);
  clock_ms++;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Normal);
  assert(!target.firmware_recovery_pending);
  const uint32_t expired_clock = clock_ms;
  clock_ms = target.firmware_recovery_started_ms;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Normal);
  clock_ms = expired_clock;
  s.recordRequestTimeout(0x40, CMD_INFO, 22, 30, false);
  assert(diag.timeouts == 1 && target.miss_count == 1);
  assert(diag.last_timeout_seq == 22 && !diag.last_timeout_probe_valid);

  // Reconnect and millis==0 must not leave a permanent grace period.
  target.firmware_recovery_started_ms = clock_ms;
  target.firmware_recovery_pending = false;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Normal);
  clock_ms = 0;
  target.firmware_recovery_pending = true;
  target.firmware_recovery_started_ms = 0;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Recovery);
  clock_ms = 15000;
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Normal);
  assert(s.requestDiagnosticsKind(0x20, CMD_GET_IO) == Kind::Normal);
  assert(!s.recordMaintenanceTimeout(0x20, CMD_GET_IO, 23, 70, Kind::Normal));
  auto& second = s.registry_.records[0x41];
  second.firmware_recovery_pending = true;
  second.firmware_recovery_started_ms = clock_ms;
  assert(s.requestDiagnosticsKind(0x41, CMD_INFO) == Kind::Recovery);
  assert(s.requestDiagnosticsKind(0x40, CMD_INFO) == Kind::Normal);
  puts("PASS: production OTA/recovery accounting, unaffected peers, offline events, normal snapshots, bounded recovery and millis rollover");
}
