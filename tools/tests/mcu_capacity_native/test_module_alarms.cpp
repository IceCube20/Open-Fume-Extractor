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
#include "bridge_links.inc"

static void check_bridge_link(uint8_t type, uint32_t& last_rx, uint16_t& faults,
                              bool (*online)(uint32_t), void (*update)(uint32_t),
                              ofe_local_diag::State& diagnostics) {
  last_rx=0;
  for(uint32_t now:{100U,200U,300U})diagnostics.timeout.fail(now);
  update(300);assert((faults&0x0800)==0 && (faults&1));
  last_rx=100;
  for(uint32_t now:{400U,500U,600U})diagnostics.timeout.fail(now);
  update(600);assert(faults&0x0800);
  update(15101);assert((faults&0x0800)==0 && (faults&1));
  diagnostics.reset();
  ModuleRecord rec;
  rec.addr = 0x50; rec.online = true; rec.type = type;
  auto check = [&](unsigned warning, unsigned error) {
    rec.output_fault_mask = faults;
    auto alarm = ofe_module_alarm::evaluate(rec, 0x20, 0);
    assert(alarm.warning() == bool(warning) && alarm.error() == bool(error));
  };
  for (uint32_t now : {0U, 1000U, 20000U}) {
    last_rx = 0; faults = 0; update(now); // Never answered, including first boot.
    assert(!online(now)); check(1, 0);
    rec.connection_warnings_muted = true; check(0, 0);
    auto required = ofe_module_alarm::evaluate(rec, rec.addr, 0);
    assert(required.error() && required.connection_error);
    required = ofe_module_alarm::evaluate(rec, 0, rec.addr);
    assert(required.error() && required.connection_error);
    rec.connection_warnings_muted = false;
  }
  last_rx = 100; update(100); assert(online(100)); check(0, 0);
  update(15100); assert(online(15100)); check(0, 0);
  update(15101); assert(!online(15101)); check(1, 0);
  last_rx = 16000; update(16000); assert(online(16000)); check(0, 0);
  faults = 4; last_rx = 0; update(17000); assert(faults == 5);
  last_rx = 17000; update(17000); assert(faults == 4); // Other faults survive recovery.
  faults = 0; last_rx = 0xFFFFF000U;
  update(0x00002A98U); assert(online(0x00002A98U)); check(0, 0);
  update(0x00002A99U); assert(!online(0x00002A99U)); check(1, 0);
}

int main() {
  for(uint8_t type:{MODULE_UNIVERSAL_RS232,MODULE_MODBUS_RTU}) {
    ModuleRecord diag;diag.addr=0x50;diag.online=true;diag.type=type;
    for(uint16_t mask:{uint16_t(0x0800),uint16_t(0x1000),uint16_t(0x2000),uint16_t(0x3800)}) {
      diag.output_fault_mask=mask;
      for(bool muted:{false,true}) {
        diag.connection_warnings_muted=muted;
        auto alarm=ofe_module_alarm::evaluate(diag,0,0);
        assert(alarm.warning()==bool(!muted || (mask & (uint16_t)~ofe_local_diag::RESPONSE_TIMEOUT)));
        assert(!alarm.error());
        diag.output_fault_mask=mask|1;
        alarm=ofe_module_alarm::evaluate(diag,diag.addr,0);
        assert(alarm.error()&&alarm.connection_error);
        diag.output_fault_mask=mask;
      }
    }
  }
  check_bridge_link(MODULE_UNIVERSAL_RS232, UniversalRs232Module::last_local_rx_ms,
    UniversalRs232Module::fault_mask, UniversalRs232Module::local_device_online_now,
    UniversalRs232Module::update_local_fault_state, UniversalRs232Module::local_diagnostics);
  check_bridge_link(MODULE_MODBUS_RTU, ModbusRtuModule::last_local_rx_ms,
    ModbusRtuModule::fault_mask, ModbusRtuModule::local_device_online_now,
    ModbusRtuModule::update_local_fault_state, ModbusRtuModule::local_diagnostics);
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
