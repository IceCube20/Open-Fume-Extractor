#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../../OpenFumeExtractorMaster/src/OfeAutomationState.h"
#include "../../../OpenFumeExtractorMaster/src/AutomationInputSignals.h"

static uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }
enum { CAP_JBC_BUS=1, CAP_JBC_USB=1<<24, CAP_JBC_ACTIVITY=CAP_JBC_BUS|CAP_JBC_USB,
  CAP_DISPLAY=1<<10, CAP_DISPLAY_AUTOMATION=1<<29, FAST_FLAG_CONNECTED=1, MODULE_FAN_IO_PRO=3,
  MODULE_WELLER_ZERO_SMOG=5, MODULE_UNIVERSAL_RS232=7, MODULE_MODBUS_RTU=8 };
struct Entity { uint8_t id=1, len=1, data[1]={0}; };
struct ModuleRecord {
  uint8_t addr=0, type=0, jbc_link_flags=0, station_addr=0, jbc_stand_mask=0, jbc_work_mask=0;
  bool online=true, telemetry_valid=false, universal_entities_valid=false, fanio_filter_runtime_valid=false;
  uint32_t caps=0, telemetry_last_ms=0, universal_entities_last_ms=0;
  uint16_t weller_uart_age_sec=0xFFFF, output_fault_mask=0, io_fault_mask=0;
  uint8_t weller_filter_status=0, fanio_filter_mode=0, fanio_filter_flags=0, universal_entity_count=1;
  uint16_t display_automation_wake_sequence=0;
  Entity universal_entities[1];
};
struct Registry {
  std::vector<ModuleRecord> records;
  uint8_t count() const { return uint8_t(records.size()); }
  ModuleRecord& at(uint8_t i) { return records[i]; }
  const ModuleRecord& at(uint8_t i) const { return records[i]; }
};
class MasterScheduler {
 public:
#include "scheduler_enums.inc"
  struct DisplayAlarmSnapshot { uint8_t alarm_count=0, warning_count=0, error_count=0; };
  struct Extractor {
    bool enabled=false;
    uint32_t afterrun_ms=0;
    bool outputEnabled() const { return enabled; }
    bool continuous() const { return false; }
    uint32_t afterrunLeftMs() const { return afterrun_ms; }
  } extractor_;
  struct Settings { uint8_t suction_level=0; uint16_t select_flow=0; } desired_jbc_settings_;
  uint8_t preferred_output_addr_=0, active_output_addr_=0;
  bool power_save_active_=false;
  mutable uint32_t system_alarm_sample_ms_=0, system_alarm_mask_=0;
  mutable bool system_alarm_sample_valid_=false;
  mutable unsigned alarm_samples=0;
  DisplayAlarmSnapshot alarms;
  OfeAutomationRequests logic_system_requests_;
  Registry registry_;
  void buildDisplayAlarmSnapshot(bool, DisplayAlarmSnapshot& snapshot) const { snapshot=alarms; ++alarm_samples; }
  uint32_t systemInputMask(uint8_t addr=0) const;
  bool systemInputActive(uint8_t state, uint8_t addr=0) const;
  void setLogicSystemOutput(uint8_t action, uint8_t addr, bool active);
  void requestDisplayWake(uint8_t addr);
  void setLogicMarker(uint8_t bit, bool active) { logic_system_requests_.setMarker(bit,active); }
  void queueExtractorAction(uint8_t) {}
  void setLogicExternalInput(bool) {}
  void setLogicContinuous(bool) {}
  void setPreferredOutputAddr(uint8_t,bool) {}
  bool setModuleOutput(uint8_t,bool,uint16_t) { return true; }
  bool setIoOutput(uint8_t,uint16_t,uint16_t) { return true; }
  bool setUniversalEntity(uint8_t,uint8_t,const uint8_t*,uint8_t) { return true; }
  const Settings& controlSettings() const { return desired_jbc_settings_; }
};
#include "scheduler_methods.inc"
static MasterScheduler scheduler;
static bool logic_rt_last_main_output = false;
#include "logic_methods.inc"

int main() {
  using S=MasterScheduler;
  S scheduler;
  scheduler.power_save_active_=true;
  scheduler.alarms={20, 10, 10};
  assert(scheduler.systemInputActive(S::INPUT_STATE_ECO_ACTIVE));
  assert(scheduler.systemInputActive(S::INPUT_STATE_ALARM_ACTIVE));
  assert(scheduler.systemInputActive(S::INPUT_STATE_WARNING_ACTIVE));
  assert(scheduler.systemInputActive(S::INPUT_STATE_ERROR_ACTIVE));
  assert(scheduler.alarm_samples==1); // Shared sample, not one alarm scan per predicate.
  now_ms+=50; scheduler.systemInputMask(); assert(scheduler.alarm_samples==2);
  // A preferred/assigned module is selected while idle, but not active.
  scheduler.preferred_output_addr_=0x20;
  scheduler.active_output_addr_=0x20;
  const auto check_output = [&](uint8_t addr, bool selected, bool active) {
    assert(scheduler.systemInputActive(S::INPUT_STATE_OUTPUT_SELECTED,addr)==selected);
    assert(scheduler.systemInputActive(S::INPUT_STATE_OUTPUT_ACTIVE,addr)==active);
    const uint32_t mask=scheduler.systemInputMask(addr);
    assert(bool(mask & (1UL << S::INPUT_STATE_OUTPUT_SELECTED))==selected);
    assert(bool(mask & (1UL << S::INPUT_STATE_OUTPUT_ACTIVE))==active);
  };
  check_output(0x20,true,false);
  check_output(0x30,false,false);
  scheduler.extractor_.enabled=true;
  check_output(0x20,true,true);
  scheduler.extractor_.afterrun_ms=1000;
  check_output(0x20,true,true); // Afterrun keeps the extractor output enabled.
  scheduler.extractor_.enabled=false;
  scheduler.extractor_.afterrun_ms=0;
  check_output(0x20,true,false);
  scheduler.preferred_output_addr_=0;
  assert(scheduler.systemInputActive(S::INPUT_STATE_OUTPUT_SELECTED_AUTO));
  check_output(0x20,false,false);
  scheduler.extractor_.enabled=true;
  check_output(0x20,false,true);
  // An automatic/runtime override can use a different output from the preference.
  scheduler.preferred_output_addr_=0x20;
  scheduler.active_output_addr_=0x30;
  check_output(0x20,true,false);
  check_output(0x30,false,true);
  scheduler.extractor_.enabled=false;
  check_output(0x30,false,false);
  scheduler.preferred_output_addr_=0;
  scheduler.active_output_addr_=0;
  ModuleRecord weller;
  weller.addr=0x30; weller.type=MODULE_WELLER_ZERO_SMOG; weller.telemetry_valid=true;
  weller.telemetry_last_ms=now_ms; weller.weller_uart_age_sec=0; weller.weller_filter_status=10;
  scheduler.registry_.records.push_back(weller);
  assert(scheduler.systemInputActive(S::INPUT_STATE_DEVICE_CONNECTED,0x30));
  assert(scheduler.systemInputActive(S::INPUT_STATE_FILTER_VALID,0x30));
  assert(scheduler.systemInputActive(S::INPUT_STATE_FILTER_WARNING,0x30));
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_CHANGE,0x30));
  scheduler.registry_.records[0].weller_filter_status=100;
  assert(scheduler.systemInputActive(S::INPUT_STATE_FILTER_CHANGE));
  scheduler.registry_.records[0].weller_uart_age_sec=11;
  assert(!scheduler.systemInputActive(S::INPUT_STATE_DEVICE_CONNECTED,0x30));
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_VALID,0x30));
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_CHANGE,0x30));
  scheduler.registry_.records[0].weller_uart_age_sec=0;
  now_ms+=10001;
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_VALID,0x30));
  scheduler.registry_.records[0].online=false;
  assert(!scheduler.systemInputActive(S::INPUT_STATE_MODULE_ONLINE,0x30));
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_CHANGE));
  ModuleRecord fan;
  fan.addr=0x20; fan.type=MODULE_FAN_IO_PRO; fan.telemetry_valid=true; fan.telemetry_last_ms=now_ms;
  fan.fanio_filter_runtime_valid=true; fan.fanio_filter_mode=3; fan.output_fault_mask=4;
  scheduler.registry_.records.push_back(fan);
  assert(!scheduler.systemInputActive(S::INPUT_STATE_FILTER_VALID,0x20));
  scheduler.registry_.records[1].fanio_filter_flags=2;
  assert(scheduler.systemInputActive(S::INPUT_STATE_FILTER_VALID,0x20));
  assert(scheduler.systemInputActive(S::INPUT_STATE_FILTER_CHANGE,0x20));
  ModuleRecord jbc;
  jbc.addr=0x11; jbc.caps=CAP_JBC_USB; jbc.jbc_link_flags=FAST_FLAG_CONNECTED; jbc.jbc_stand_mask=3;
  scheduler.registry_.records.push_back(jbc);
  assert(scheduler.systemInputActive(S::INPUT_STATE_JBC_STAND,0x11));
  scheduler.registry_.records[2].jbc_link_flags=0;
  assert(!scheduler.systemInputActive(S::INPUT_STATE_JBC_STAND));
  ModuleRecord bridge;
  bridge.addr=0x50; bridge.type=MODULE_UNIVERSAL_RS232; bridge.universal_entities_valid=true;
  bridge.universal_entities_last_ms=now_ms; bridge.universal_entities[0].data[0]=1;
  scheduler.registry_.records.push_back(bridge);
  assert(scheduler.systemInputActive(S::INPUT_STATE_DEVICE_CONNECTED,0x50));
  scheduler.registry_.records[3].universal_entities[0].data[0]='0';
  assert(!scheduler.systemInputActive(S::INPUT_STATE_DEVICE_CONNECTED,0x50));
  ModuleRecord display;
  display.addr=0x40; display.caps=CAP_DISPLAY|CAP_DISPLAY_AUTOMATION;
  scheduler.registry_.records.push_back(display);
  display.addr=0x41; display.online=false;
  scheduler.registry_.records.push_back(display);
  scheduler.requestDisplayWake(0);
  assert(scheduler.registry_.records[4].display_automation_wake_sequence==1);
  assert(scheduler.registry_.records[5].display_automation_wake_sequence==0);
  scheduler.registry_.records[4].display_automation_wake_sequence=0xFFFF;
  scheduler.requestDisplayWake(0x40);
  assert(scheduler.registry_.records[4].display_automation_wake_sequence==1);
  scheduler.requestDisplayWake(255);
  assert(scheduler.registry_.records[4].display_automation_wake_sequence==1);
  scheduler.setLogicSystemOutput(S::SYSTEM_OUT_ECO_INHIBIT,0,true);
  assert(scheduler.logic_system_requests_.eco_inhibit);
  scheduler.setLogicSystemOutput(S::SYSTEM_OUT_DISPLAY_HOLD,0x40,true);
  assert(scheduler.logic_system_requests_.holdsDisplay(0x40));
  assert(!scheduler.systemInputActive(255));
  assert(logic_apply_output_signal("system:eco_inhibit",true));
  assert(::scheduler.logic_system_requests_.eco_inhibit);
  assert(logic_apply_output_signal("system:eco_inhibit",false));
  assert(!::scheduler.logic_system_requests_.eco_inhibit);
  assert(logic_apply_output_signal("system:display:hold:0",true));
  assert(::scheduler.logic_system_requests_.holdsDisplay(0x40));
  assert(logic_apply_output_signal("system:display:hold:0",false));
  assert(!::scheduler.logic_system_requests_.holdsDisplay(0x40));
  assert(!logic_apply_output_signal("system:display:hold:256",true));
  for (unsigned i=1; i<=8; ++i) {
    char signal[30]; std::snprintf(signal,sizeof(signal),"system:marker:%u",i);
    assert(logic_apply_output_signal(signal,true));
  }
  assert(::scheduler.logic_system_requests_.markers==255);
  assert(!logic_apply_output_signal("system:marker:0",true));
  assert(!logic_apply_output_signal("system:marker:9",true));
  assert(logic_apply_output_signal("system:marker:1",false));
  assert(::scheduler.logic_system_requests_.markers==254);
  assert(logic_output_is_event_signal("system:display:wake:0"));
  assert(!logic_output_is_event_signal("system:display:hold:0"));
  assert(!logic_output_is_event_signal("system:marker:1"));
  ::scheduler.registry_.records.push_back(scheduler.registry_.records[4]);
  assert(logic_apply_output_signal("system:display:wake:64",false));
  assert(::scheduler.registry_.records[0].display_automation_wake_sequence==1);
  assert(logic_apply_output_signal("system:display:wake:64",true));
  assert(::scheduler.registry_.records[0].display_automation_wake_sequence==2);
  std::puts("PASS: actual scheduler predicates, cached alarm severities, stale/disconnected filter data, JBC Stand, local bridge link and addressed wake events");
}
