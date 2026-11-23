#include "ModuleAlarmPolicy.h"
#include "AutomationInputSignals.h"
#include "DisplayHomeMetrics.h"
#include "EntityPresentation.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace jbc_rs485;
#include "alarm_codes.inc"
class MasterScheduler {
public:
  static constexpr uint8_t DISPLAY_ALARM_MAX_ITEMS = 6;
#include "alarm_types.inc"
  ModuleRegistry registry_;
  uint8_t active_output_addr_ = 0x20, preferred_output_addr_ = 0;
  bool mainInputSourceAvailable() const { return true; }
  uint16_t systemJbcError() const { return 0; }
  ofe_module_alarm::State moduleAlarmState(const ModuleRecord& rec) const {
    return ofe_module_alarm::evaluate(rec, active_output_addr_, preferred_output_addr_);
  }
  void buildDisplayAlarmSnapshot(bool, DisplayAlarmSnapshot&) const;
};
#include "alarm_snapshot.inc"
#include "home_metrics.inc"

int main() {
  MasterScheduler scheduler;
  assert(scheduler.registry_.begin());
  auto& rec = *scheduler.registry_.upsert(0x30);
  rec.online = true; rec.caps = CAP_WELLER_INTERFACE; rec.type = MODULE_WELLER_ZERO_SMOG;
  rec.weller_uart_age_sec = 0xFFFF;
  MasterScheduler::DisplayAlarmSnapshot state;
  auto check = [&](unsigned warning, unsigned error) {
    scheduler.buildDisplayAlarmSnapshot(false, state);
    assert(state.warning_count == warning && state.error_count == error);
    assert(state.alarm_count == warning + error);
    assert((state.critical_mask != 0) == (error != 0));
    const auto metrics = display_home_metrics(rec, true, scheduler.moduleAlarmState(rec));
    assert(bool(metrics.flags & ofe_module_health::METRICS_WARNING) == bool(warning));
    assert(bool(metrics.flags & ofe_module_health::METRICS_ERROR) == bool(error));
    if (!warning && !error) assert(metrics.faults == 0);
  };
  check(1, 0);
  assert(state.items[0].code == DISPLAY_ALARM_WELLER_LINK);
  rec.connection_warnings_muted = true; check(0, 0);
  scheduler.active_output_addr_ = rec.addr; check(0, 1);
  scheduler.active_output_addr_ = 0x20; scheduler.preferred_output_addr_ = rec.addr; check(0, 1);
  scheduler.preferred_output_addr_ = 0;
  rec.io_fault_mask = 1; check(0, 0);
  rec.connection_warnings_muted = false; check(1, 0);
  assert(state.items[0].code == DISPLAY_ALARM_WELLER_LINK);
  rec.connection_warnings_muted = true;
  scheduler.active_output_addr_ = rec.addr; check(0, 1); // No duplicate connection error.
  assert(state.items[0].code == DISPLAY_ALARM_OUTPUT_FAULT);
  rec.weller_uart_age_sec = 0; rec.io_fault_mask = 2; check(1, 0);
  rec.io_fault_mask = 6; check(1, 1);
  rec.io_fault_mask = 2; rec.weller_uart_age_sec = 0xFFFF; check(1, 1);
  rec.online = false; check(0, 1);
  assert(state.items[0].code == DISPLAY_ALARM_MODULE_OFFLINE);
  rec.online = true; rec.io_fault_mask = 0; rec.caps = CAP_JBC_USB; rec.type = MODULE_JBC_USB;
  scheduler.active_output_addr_ = 0x20; check(0, 0);
  rec.connection_warnings_muted = false; check(1, 0);
  rec.jbc_link_flags = FAST_FLAG_CONNECTED | FAST_FLAG_ERROR_PENDING; check(0, 0);
  for (auto type : {MODULE_WELLER_ZERO_SMOG, MODULE_UNIVERSAL_RS232, MODULE_MODBUS_RTU}) {
    rec.type = type; rec.caps = type == MODULE_WELLER_ZERO_SMOG ? CAP_WELLER_INTERFACE : 0;
    rec.weller_uart_age_sec = 0; // The explicit bus fault also works before age expires.
    rec.role_output = true; rec.output_enabled = true;
    rec.io_fault_mask = 0; rec.output_fault_mask = 1;
    scheduler.active_output_addr_ = 0x20; scheduler.preferred_output_addr_ = 0;
    rec.connection_warnings_muted = false; check(1, 0);
    rec.connection_warnings_muted = true; check(0, 0);
    scheduler.preferred_output_addr_ = rec.addr; check(0, 1);
    scheduler.preferred_output_addr_ = 0;
    rec.io_fault_mask = 2; check(1, 0);
    rec.io_fault_mask = 4; check(0, 1);
    rec.output_fault_mask = 0; rec.io_fault_mask = 0; check(0, 0);
  }
  puts("PASS: actual MCU alarms and Home packets: optional/muted Weller/Universal/Modbus links, required outputs, stale roles, other faults and recovery");
}
